namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    private int reconciling;

    private IElementPeer CreatePeers(Attachment current, Element element)
    {
        if (element is Image image) image.BeginAttachment();
        var peer = current.Backend.Create(element, new Events(this, current, element))
            ?? throw new InvalidOperationException("The backend returned a null peer.");
        if (current.Peers.Values.Any(existing => ReferenceEquals(existing, peer)))
            throw new InvalidOperationException("The backend reused an element peer.");
        current.Peers.Add(element, peer);
        current.Order.Add(element);
        if (element is KeyedStack && peer is not IMutableElementPeer)
            throw new NotSupportedException("KeyedStack requires an IMutableElementPeer backend.");
        if (element is PageView pages)
        {
            if (peer is not IPageViewElementPeer retained)
                throw new NotSupportedException("PageView requires native retained-page support.");
            retained.ValidatePages(pages.Pages, pages.Selected);
            retained.ValidateVisibility(pages.Visible);
        }
        if (element is PageSelector && peer is not IPageSelectorElementPeer)
            throw new NotSupportedException("Native page navigation requires an IPageSelectorElementPeer backend.");
        if (element is PasswordInput && peer is not IPasswordElementPeer)
            throw new NotSupportedException("PasswordInput requires an opaque IPasswordElementPeer backend.");
        if (element is TextInput { Purpose: not InputPurpose.Normal } && peer is not IInputPurposeElementPeer)
            throw new NotSupportedException("Input purpose hints require an IInputPurposeElementPeer backend.");
        if ((element.WidthConstraints.HasValue || element.HeightConstraints.HasValue) && peer is not IConstrainedElementPeer)
            throw new NotSupportedException("Per-axis constraints require an IConstrainedElementPeer backend.");
        ValidatePresentationPeer(element, peer);
        ValidateTextLayoutPeer(element, peer);
        ValidateSelectionPeer(element, peer);
        ValidateRevealPeer(element, peer);
        if (element is Image packaged)
        {
            if (peer is not IImageElementPeer nativeImage)
                throw new NotSupportedException("Packaged images require an IImageElementPeer backend.");
            nativeImage.ValidateImage(packaged.Source, packaged.DecodeOptions);
        }
        foreach (var child in element.Children) peer.AddChild(CreatePeers(current, child));
        return peer;
    }

    internal void Reconcile(KeyedStack container, IReadOnlyList<KeyedItem> items, PageView.Snapshot? pageSnapshot = null)
        => DeferInteractionDelivery(() => { ReconcileCore(container, items, pageSnapshot); return true; });

    private void ReconcileCore(KeyedStack container, IReadOnlyList<KeyedItem> items, PageView.Snapshot? pageSnapshot)
    {
        VerifyElementMutation(container);
        if (reconciling != 0 && building is null)
            throw new KeyedUpdateException(false, new InvalidOperationException("A keyed update callback cannot start another structural update."));
        bool committed = false;
        int start = elements.Count;
        var parentScope = building;
        var removed = new List<Element>();
        reconciling++;
        try
        {
            KeyedItem[] snapshot;
            transitioning = true;
            try
            {
                ArgumentNullException.ThrowIfNull(items);
                snapshot = items.ToArray();
                var keys = new HashSet<string>(StringComparer.Ordinal);
                foreach (var item in snapshot)
                {
                    ArgumentNullException.ThrowIfNull(item);
                    if (!keys.Add(item.Key)) throw new ArgumentException($"Duplicate key '{item.Key}'.", nameof(items));
                }
            }
            finally { transitioning = false; }
            var old = container.Entries.ToDictionary(entry => entry.Key, StringComparer.Ordinal);
            var survivors = snapshot.Where(item => old.TryGetValue(item.Key, out var entry) && entry.ComponentType == item.ComponentType)
                .Select(item => old[item.Key]).ToHashSet();
            var removedRoots = container.Entries.Where(entry => !survivors.Contains(entry)).Select(entry => entry.Root).ToArray();
            foreach (var element in removedRoots) VisitSubtree(element, removed);
            ValidatePageRemoval(removed.ToHashSet());
            if (container is PageView pages && pageSnapshot is not null) ValidatePagePeers(pages, pageSnapshot);
            var order = container.Entries.Where(survivors.Contains).Select(entry => entry.Key).ToList();
            var moves = new HashSet<string>(StringComparer.Ordinal);
            for (int i = 0; i < snapshot.Length; i++)
            {
                string key = snapshot[i].Key;
                int previous = order.IndexOf(key);
                if (previous == -1) order.Insert(i, key);
                else if (previous != i)
                {
                    moves.Add(key);
                    order.RemoveAt(previous);
                    order.Insert(i, key);
                }
            }
            var current = attachment;
            IMutableElementPeer? parentPeer = null;
            if (current is not null && current.Peers.TryGetValue(container, out var peer))
            {
                parentPeer = peer as IMutableElementPeer ?? throw new NotSupportedException("KeyedStack requires an IMutableElementPeer backend.");
                transitioning = true;
                try
                {
                    if ((removedRoots.Length != 0 || snapshot.Length != survivors.Count || moves.Count != 0) &&
                        parentPeer is IMutationPreflightPeer preflight)
                        preflight.ValidateMutation();
                    for (int i = 0; i < snapshot.Length; i++)
                        if (moves.Contains(snapshot[i].Key)) parentPeer.ValidateMove(current.Peers[old[snapshot[i].Key].Root], i);
                }
                finally { transitioning = false; }
            }
            var next = new List<KeyedStack.Entry>();
            foreach (var item in snapshot)
            {
                if (old.TryGetValue(item.Key, out var existing) && existing.ComponentType == item.ComponentType)
                {
                    next.Add(existing);
                    continue;
                }
                var scope = building = new BuildScope(this, parentScope, elements.Count, staged: true);
                Exception? constructionError = null;
                try
                {
                    var component = item.Factory(this) ?? throw new InvalidOperationException($"Factory for '{item.Key}' returned null.");
                    if (building != scope) throw new InvalidOperationException("A component factory left a build scope open.");
                    var candidate = component.Root;
                    if (candidate is not Stack stack) throw new InvalidOperationException("A keyed component requires a Stack root.");
                    SetContent(stack);
                    scope.Complete();
                    next.Add(new(item.Key, item.ComponentType, component, candidate));
                }
                catch (Exception error)
                {
                    constructionError = error;
                    throw;
                }
                finally
                {
                    var failures = new List<Exception>();
                    UnwindBuildScopes(parentScope, failures);
                    if (failures.Count != 0)
                    {
                        if (constructionError is not null) failures.Insert(0, constructionError);
                        throw new AggregateException(failures);
                    }
                }
            }
            if (parentPeer is not null && current?.Backend is IBackendTreePreflight treePreflight)
            {
                transitioning = true;
                try
                {
                    foreach (var entry in next)
                        if (!survivors.Contains(entry)) treePreflight.ValidateInsertion(container, entry.Root);
                }
                finally { transitioning = false; }
            }
            container.ReplaceChildren(next.Select(entry => entry.Root).ToArray());
            container.Entries = next;
            if (container is PageView pageHost && pageSnapshot is not null) pageHost.CommitSnapshot(pageSnapshot);
            committed = true;
            if (current is not null && current.Peers.ContainsKey(container)) NoteNativeModelMutation();
            transitioning = true;
            try
            {
                var failures = new List<Exception>();
                RetireComponents(removed, failures);
                if (failures.Count != 0) throw new AggregateException(failures);
            }
            finally { transitioning = false; }
            if (parentPeer is not null && current is not null)
            {
                transitioning = true;
                try
                {
                    var retired = removed.ToHashSet();
                    var failures = new List<Exception>();
                    RetireAttachmentResources(current, retired, failures);
                    if (failures.Count != 0) throw new AggregateException(failures);
                    foreach (var removedRoot in removedRoots) parentPeer.RemoveChild(current.Peers[removedRoot]);
                    foreach (var element in current.Order.Where(retired.Contains).Reverse().ToArray())
                    {
                        try { current.Peers[element].Dispose(); }
                        catch (Exception error) { failures.Add(error); }
                        current.Peers.Remove(element);
                        current.Order.Remove(element);
                        if (element is PageSelector selector) current.PageLinks.Remove(selector);
                    }
                    if (failures.Count != 0) throw new AggregateException(failures);
                    for (int i = 0; i < next.Count; i++)
                    {
                        var entry = next[i];
                        if (!survivors.Contains(entry))
                        {
                            var created = CreatePeers(current, entry.Root);
                            ConnectPageSelectors(current);
                            parentPeer.InsertChild(i, created);
                        }
                        else if (moves.Contains(entry.Key)) parentPeer.MoveChild(current.Peers[entry.Root], i);
                    }
                    if (container is PageView updatedPages) ApplyPages(current, updatedPages);
                    foreach (var image in current.Order.OfType<Image>()) QueueImageState(image);
                }
                finally { transitioning = false; }
            }
            ReleaseModel(removed);
            removed.Clear();
            for (int i = 0; i < snapshot.Length; i++) snapshot[i].Update?.Invoke(next[i].Component);
        }
        catch (Exception error)
        {
            var failures = new List<Exception> { error };
            if (committed)
            {
                transitioning = true;
                try { RetireComponents(removed, failures); }
                finally { transitioning = false; }
                if (attachment is { } failed)
                {
                    attachment = null;
                    transitioning = true;
                    try { ReleaseAttachment(failed, failures); }
                    finally { transitioning = false; }
                }
                ReleaseModel(removed);
            }
            else
            {
                UnwindBuildScopes(parentScope, failures);
                RollbackElements(start, failures);
            }
            throw new KeyedUpdateException(committed, failures.Count == 1 ? error : new AggregateException(failures));
        }
        finally { reconciling--; }
    }

    private static void VisitSubtree(Element element, List<Element> result)
    {
        result.Add(element);
        foreach (var child in element.Children) VisitSubtree(child, result);
    }

    private void ReleaseModel(List<Element> removed)
    {
        foreach (var element in removed.AsEnumerable().Reverse())
        {
            element.Release();
            elements.Remove(element);
        }
    }
}

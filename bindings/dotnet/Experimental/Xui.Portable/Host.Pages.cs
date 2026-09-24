using System.Runtime.ExceptionServices;

namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    public PageView PageView(string name) => Create(() => new PageView(this, name));
    public TabStrip TabStrip(string name) => Create(() => new TabStrip(this, name));
    public NavigationView NavigationView(string name) => Create(() => new NavigationView(this, name));
    internal T PageOperation<T>(Func<T> operation) => DeferInteractionDelivery(operation);

    private void ValidatePagePeers(PageView pages, PageView.Snapshot snapshot)
    {
        if (attachment is not { } current || !current.Peers.TryGetValue(pages, out var peer)) return;
        InputOperation(() =>
        {
            if (peer is not IPageViewElementPeer pagePeer)
                throw new NotSupportedException("Retained pages require an IPageViewElementPeer backend.");
            pagePeer.ValidatePages(snapshot.Pages, snapshot.Selected);
            foreach (var selector in pages.Selectors)
            {
                if (!current.Peers.TryGetValue(selector, out var selectorPeer)) continue;
                if (selectorPeer is not IPageSelectorElementPeer linked)
                    throw new NotSupportedException("Linked page navigation requires an IPageSelectorElementPeer backend.");
                linked.ValidatePages(snapshot.Pages, snapshot.Selected);
            }
            return true;
        });
    }

    internal void SelectPage(PageView pages, PageView.Snapshot snapshot)
        => DeferInteractionDelivery(() => { SelectPageCore(pages, snapshot); return true; });

    private void SelectPageCore(PageView pages, PageView.Snapshot snapshot)
    {
        if (reconciling != 0) throw new InvalidOperationException("Page selection cannot reenter keyed reconciliation.");
        bool committed = false;
        try
        {
            ValidatePagePeers(pages, snapshot);
            pages.CommitSnapshot(snapshot);
            committed = true;
            if (attachment is { } current) ApplyPages(current, pages);
        }
        catch (Exception error)
        {
            if (committed && attachment is { } current) error = FailAttachment(current, error);
            throw new KeyedUpdateException(committed, error);
        }
    }

    internal void SetPageVisibility(PageView pages, bool visible)
        => DeferInteractionDelivery(() => { SetPageVisibilityCore(pages, visible); return true; });

    private void SetPageVisibilityCore(PageView pages, bool visible)
    {
        bool committed = false;
        try
        {
            IElementPeer? peer = null;
            var current = attachment;
            if (current is not null && current.Peers.TryGetValue(pages, out peer))
            {
                if (peer is not IPageViewElementPeer pagePeer)
                    throw new NotSupportedException("Retained page visibility requires an IPageViewElementPeer backend.");
                InputOperation(() => { pagePeer.ValidateVisibility(visible); return true; });
            }
            pages.CommitVisibility(visible);
            committed = true;
            if (current is not null && peer is not null)
                UpdateAttachment(current, () => peer.Update(ElementProperty.Visible));
        }
        catch (Exception error) { throw new KeyedUpdateException(committed, error); }
    }

    private void ApplyPages(Attachment current, PageView pages)
    {
        if (!current.Peers.TryGetValue(pages, out var peer)) return;
        UpdateAttachment(current, () =>
        {
            peer.Update(ElementProperty.Pages);
            foreach (var selector in pages.Selectors)
                if (current.Peers.TryGetValue(selector, out var linked)) linked.Update(ElementProperty.Pages);
        });
    }

    internal void RestorePageSelector(PageSelector selector, Exception original)
    {
        if (attachment is { } current && current.Peers.TryGetValue(selector, out var peer))
        {
            try { UpdateAttachment(current, () => peer.Update(ElementProperty.Pages)); }
            catch (Exception restore) { throw new AggregateException(original, restore); }
        }
        ExceptionDispatchInfo.Capture(original).Throw();
    }

    private static void ValidatePageRemoval(ISet<Element> removed)
    {
        foreach (var pages in removed.OfType<PageView>())
            if (pages.Selectors.Any(selector => !removed.Contains(selector)))
                throw new InvalidOperationException("A PageView cannot retire while a surviving selector is linked to it.");
    }

    private static void ConnectPageSelectors(Attachment current)
    {
        foreach (var selector in current.Order.OfType<PageSelector>())
        {
            if (current.PageLinks.Contains(selector)) continue;
            if (!current.Peers.TryGetValue(selector.Pages, out var pages) || pages is not IPageViewElementPeer pagePeer)
                throw new InvalidOperationException("A linked PageView must belong to the same mounted attachment.");
            if (current.Peers[selector] is not IPageSelectorElementPeer selectorPeer)
                throw new NotSupportedException("Native page selectors require an IPageSelectorElementPeer backend.");
            selectorPeer.ValidatePages(selector.Pages.Pages, selector.Pages.Selected);
            selectorPeer.ConnectPages(pagePeer);
            current.PageLinks.Add(selector);
        }
    }
}

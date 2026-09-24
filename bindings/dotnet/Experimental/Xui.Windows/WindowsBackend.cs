using Xui.Experimental.Portable;
using System.Runtime.CompilerServices;
using P = Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

/// <summary>Maps portable elements to native XUI controls in one owned content scope.</summary>
/// <remarks>The caller owns the ContentHost and window. Detach releases the scope, not the window.
/// Peers borrow scope-owned native controls. Mutation-capable runtimes also release removed element handles.
/// Native events are delivered in a window-owned queue outside the native content callback.</remarks>
public sealed partial class WindowsBackend : IThemeBackend, IHostViewportBackend, IBackendTreePreflight
{
    private static readonly ConditionalWeakTable<ContentHost, SurfaceOwner> owners = new();
    private sealed class SurfaceOwner { internal WindowsBackend? Backend; }
    private readonly ContentHost surface;
    private readonly WindowsDispatcher dispatcher;
    private readonly List<WindowsPeer> peers = [];
    private readonly List<WindowsViewportLease> viewports = [];
    private readonly List<ViewportObservation> viewportObservations = [];
    private sealed class ViewportObservation(WindowsBackend owner) : IDisposable
    {
        internal ContentViewportSubscription? Subscription;
        public void Dispose()
        {
            owner.VerifyAccess();
            if (Subscription is null) return;
            Subscription.Dispose();
            Subscription = null;
            owner.viewportObservations.Remove(this);
        }
    }
    private ContentUpdate? scope;
    private ContentUpdate.ContentAppend? append;
    private bool mounted;
    private bool disposed;
    private Exception? callbackError;
    private P.ImageResourceCache? imageCache;
    private P.ImagePixelBudget? imagePixels;
    internal Window Window => surface.OwnerWindow;
    internal WindowsDispatcher Dispatcher => dispatcher;
    internal P.ImageResourceCache Images => imageCache ??= new();
    internal P.ImagePixelBudget ImagePixels => imagePixels ??= new();
    internal bool Mounted => mounted && !disposed && callbackError is null;
    public bool SupportsMutation { get; }
    public bool SupportsConstraints { get; }
    public bool SupportsGrid { get; }
    public bool SupportsVirtualization { get; }
    public bool SupportsForms { get; }
    public bool SupportsRetainedPages { get; }
    public bool SupportsSingleChoice => ComboBox.SupportsSelectionQuery;
    public bool SupportsHostViewport => ContentHost.SupportsViewportObservation;
    public bool SupportsReveal => Reveal.SupportsPortableState;
    public bool SupportsMemoryImages => SupportsConstraints && Image.SupportsMemorySource;

    public WindowsBackend(ContentHost surface, WindowsDispatcher dispatcher,
        WindowsThemeAuthority themeAuthority = WindowsThemeAuthority.BorrowedSurface)
    {
        ArgumentNullException.ThrowIfNull(surface);
        ArgumentNullException.ThrowIfNull(dispatcher);
        dispatcher.VerifyAccess();
        if (!ReferenceEquals(surface.OwnerWindow, dispatcher.Window))
            throw new ArgumentException("The surface and dispatcher must belong to the same window.", nameof(surface));
        this.surface = surface;
        this.dispatcher = dispatcher;
        if (!Enum.IsDefined(themeAuthority)) throw new ArgumentOutOfRangeException(nameof(themeAuthority));
        this.themeAuthority = themeAuthority;
        Window.VerifyAccess();
        SupportsMutation = ContentUpdate.SupportsMutation;
        SupportsConstraints = Xui.Element.SupportsAxisConstraints && ScrollView.SupportsFillViewport;
        SupportsGrid = Grid.SupportsPortableLayout;
        SupportsVirtualization = SupportsMutation && SupportsConstraints &&
            ScrollView.SupportsVirtualViewport && Control.SupportsInteraction;
        SupportsForms = SupportsConstraints && FormSupport.Available;
        SupportsRetainedPages = SupportsMutation && SupportsConstraints && RetainedPages.Available;
    }

    /// <summary>The last authored callback failure. Failures also propagate through Window.Run/Application.Run.</summary>
    public Exception? CallbackError { get { VerifyAccess(); return callbackError; } }

    internal void VerifyAccess() => dispatcher.VerifyAccess();

    public IDisposable ObserveViewport(Action<P.Size> changed)
    {
        ArgumentNullException.ThrowIfNull(changed);
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (!Mounted) throw new InvalidOperationException("Viewport observation requires a mounted Windows attachment.");
        viewportObservations.EnsureCapacity(checked(viewportObservations.Count + 1));
        var observation = new ViewportObservation(this);
        observation.Subscription = surface.ObserveViewport(value =>
        {
            if (Mounted) changed(new(value.Width, value.Height));
        });
        viewportObservations.Add(observation);
        return observation;
    }

    public IElementPeer Create(P.Element element, IControlEvents events)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(element);
        ArgumentNullException.ThrowIfNull(events);
        if (!SupportsRetainedPages && element.Kind is (P.ElementKind.PageView or P.ElementKind.TabStrip or P.ElementKind.NavigationView))
            throw new NotSupportedException("This Windows adapter requires the qualified retained-page and linked-selector native contract.");
        if (element is P.Grid && !SupportsGrid)
            throw new NotSupportedException("The Windows runtime does not provide the portable Grid layout contract.");
        if (!SupportsForms && (element is P.MultilineText or P.PasswordInput ||
            element is P.TextInput { Purpose: not P.InputPurpose.Normal }))
            throw new NotSupportedException("The Windows runtime does not provide the bounded Forms contract.");
        if (element is P.SingleChoice && !SupportsSingleChoice)
            throw new NotSupportedException("The Windows runtime does not expose actual native ComboBox selection.");
        if (element is P.Image && !SupportsMemoryImages)
            throw new NotSupportedException("The Windows runtime does not provide bounded packaged-memory Image decoding.");
        if (mounted && !SupportsMutation) throw new NotSupportedException("The Windows runtime cannot append native elements.");
        try
        {
            if (scope is null)
            {
                var owner = owners.GetValue(surface, static _ => new SurfaceOwner());
                if (owner.Backend is not null && owner.Backend != this)
                    throw new InvalidOperationException("The Windows surface already has a portable attachment.");
                owner.Backend = this;
                scope = surface.BeginUpdate();
            }
            else if (mounted) append ??= scope.BeginAppend();
            WindowsPeer peer = element is P.Image
                ? new WindowsImagePeer(this, element, events)
                : element is P.Reveal
                ? new WindowsRevealPeer(this, element, events)
                : element is P.SingleChoice
                ? new WindowsSingleChoicePeer(this, element, events)
                : element is P.RangeInput
                ? new WindowsRangePeer(this, element, events)
                : element is P.PageView
                ? new WindowsPagePeer(this, element, events)
                : element is P.PageSelector
                ? new WindowsPageSelectorPeer(this, element, events)
                : element is P.PasswordInput
                ? new WindowsPasswordPeer(this, element, events)
                : element is P.Stack && SupportsVirtualization
                ? new WindowsVirtualItemPeer(this, element, events)
                : element is P.ScrollView && SupportsVirtualization
                ? new WindowsViewportPeer(this, element, events)
                : element is P.TextInput
                ? SupportsForms
                    ? new WindowsPurposeTextPeer(this, element, events)
                    : SupportsConstraints
                    ? new WindowsConstrainedTextPeer(this, element, events)
                    : new WindowsTextPeer(this, element, events)
                : (SupportsMutation && element is P.Stack, SupportsConstraints) switch
            {
                (true, true) => new WindowsConstrainedMutablePeer(this, element, events),
                (true, false) => new WindowsMutablePeer(this, element, events),
                (false, true) => new WindowsConstrainedPeer(this, element, events),
                _ => new WindowsPeer(this, element, events)
            };
            peers.Add(peer);
            return peer;
        }
        catch (Exception error)
        {
            // A constructor that never returned a peer still allocated into this candidate.
            try { Dispose(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    public void Mount(IElementPeer root)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (mounted || scope is null) throw new InvalidOperationException("Mount requires an uncommitted Windows tree.");
        if (root is not WindowsPeer peer || peer.Backend != this || peer.Element.Parent is not null)
            throw new ArgumentException("The root belongs to another tree.", nameof(root));
        scope.Commit(peer.Native);
        mounted = true;
        foreach (var current in peers) current.FlushInteraction();
    }

    /// <summary>Returns borrowed controls for this attachment, including duplicate automation identities.</summary>
    /// <remarks>Do not retain these controls across detach, or mutate them instead of the portable model.</remarks>
    public IReadOnlyList<Control> FindControls(string automationId)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(automationId);
        return peers.Where(peer => peer.Element is P.Control control && control.AutomationId == automationId)
            .Select(peer => peer.Native).OfType<Control>().ToArray();
    }
    public IReadOnlyList<Element> FindElements(string automationId)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(automationId);
        return peers.Where(peer => peer.Element is P.Control control && control.AutomationId == automationId)
            .Select(peer => peer.Native).ToArray();
    }

    internal void Deliver(WindowsPeer peer, Func<bool> callback, bool valueEvent = false)
    {
        if (!Mounted || peer.Disposed) return;
        long revision = peer.Revision;
        if (valueEvent) peer.BeginValueDelivery();
        try
        {
            dispatcher.Post(() =>
            {
                try
                {
                    if (!Mounted || peer.Disposed || peer.Revision != revision) return;
                    callback();
                }
                catch (Exception error)
                {
                    ReportCallbackFailure(error);
                    throw;
                }
                finally { if (valueEvent) peer.EndValueDelivery(); }
            }, _ => { if (valueEvent) peer.EndValueDelivery(); });
        }
        catch
        {
            if (valueEvent) peer.EndValueDelivery();
            throw;
        }
    }

    private void ReportCallbackFailure(Exception error)
    {
        callbackError = error;
        Console.Error.WriteLine($"Xui.Windows callback failed: {error}");
    }
    internal void ReportImageCleanup(Exception error) =>
        Console.Error.WriteLine($"Xui.Windows Image cleanup failed: {error}");

    internal void PostNotification(WindowsPeer peer, Action action, Action canceled)
    {
        dispatcher.Post(() =>
        {
            if (!Mounted || peer.Disposed) { canceled(); return; }
            try { action(); }
            catch (Exception error) { ReportCallbackFailure(error); throw; }
        }, _ => canceled());
    }

    internal void TrackViewport(WindowsViewportLease lease) => viewports.Add(lease);
    internal void ForgetViewport(WindowsViewportLease lease) => viewports.Remove(lease);
    internal WindowsPeer Peer(P.Element element) => peers.Single(peer => ReferenceEquals(peer.Element, element));
    internal void SetVirtualItemInfo(WindowsPeer row, P.VirtualItemInfo info)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(row.Disposed, row);
        info.Validate();
        for (P.Element? ancestor = row.Element.Parent; ancestor is not null; ancestor = ancestor.Parent)
        {
            if (ancestor is not P.ScrollView) continue;
            var viewport = viewports.FirstOrDefault(viewport => !viewport.IsDisposed && ReferenceEquals(viewport.Owner, ancestor))
                ?? throw new InvalidOperationException("The row is not inside an active leased Windows viewport.");
            viewport.SetItemInfo(row, info);
            return;
        }
        throw new InvalidOperationException("Virtual row metadata requires a leased scroll ancestor.");
    }

    internal void ValidateMutation()
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        (scope ?? throw new InvalidOperationException("There is no content attachment.")).ValidateMutation();
    }

    internal void FinishAppend()
    {
        if (append is null) throw new InvalidOperationException("Insertion requires freshly constructed Windows content.");
        append.Complete();
        append = null;
    }

    internal void ReleasePeer(WindowsPeer peer)
    {
        if (scope is not null) scope.ReleaseElement(peer.Native);
        peers.Remove(peer);
    }

    public void Dispose()
    {
        VerifyAccess();
        if (disposed) return;
        mounted = false;
        var failures = new List<Exception>();
        foreach (var image in peers.OfType<WindowsImagePeer>())
        {
            try { image.CancelImage(); }
            catch (Exception error) { failures.Add(error); }
        }
        try { imageCache?.Dispose(); }
        catch (Exception error) { failures.Add(error); }
        foreach (var observation in viewportObservations.ToArray())
        {
            try { observation.Dispose(); }
            catch (Exception error) { failures.Add(error); }
        }
        viewportObservations.Clear();
        try { RestoreTheme(); }
        catch (Exception error) { failures.Add(error); }
        foreach (var password in peers.OfType<WindowsPasswordPeer>())
        {
            try { password.ClearPassword(); }
            catch (Exception error) { failures.Add(error); }
        }
        foreach (var reveal in peers.OfType<WindowsRevealPeer>())
        {
            try { reveal.CancelMotion(); }
            catch (Exception error) { failures.Add(error); }
        }
        foreach (var viewport in viewports.ToArray())
        {
            try { viewport.Dispose(); }
            catch (Exception error) { failures.Add(error); }
        }
        bool released = false;
        try { scope?.Dispose(); released = true; }
        catch (Exception error) { failures.Add(error); }
        if (released)
        {
            foreach (var image in peers.OfType<WindowsImagePeer>())
            {
                try { image.CompleteNativeRetirement(); }
                catch (Exception error) { failures.Add(error); }
            }
            scope = null;
            append = null;
            disposed = true;
            peers.Clear();
            viewports.Clear();
            if (owners.TryGetValue(surface, out var owner) && owner.Backend == this) owner.Backend = null;
        }
        if (failures.Count != 0) throw new AggregateException("Windows attachment cleanup failed.", failures);
    }
}

internal class WindowsPeer : IFocusableElementPeer, IPresentationPeer, ITextLayoutPeer
{
    private P.Element? element;
    private IControlEvents? events;
    private Element? native;
    protected readonly List<WindowsPeer> children = [];
    private bool settingText;
    private bool settingValue;
    private int pendingValues;
    private ControlInteractionSubscription? interaction;
    private ControlInteraction? pendingInteraction;
    private bool interactionScheduled;
    private Dictionary<StylePart, PartStyleValues>? typographyBaseline;
    private LabelLayout? labelLayoutBaseline;
    private bool ownsLabelLayout;
    internal WindowsBackend Backend { get; }
    internal P.Element Element => element ?? throw new ObjectDisposedException(nameof(WindowsPeer));
    internal Element Native => native ?? throw new InvalidOperationException("The Windows peer is not ready or was disposed.");
    protected bool NativeReady => native is not null;
    internal bool Disposed { get; private set; }
    internal long Revision { get; private set; }

    internal WindowsPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    {
        Backend = backend;
        this.element = element;
        this.events = events;
        if (element is P.Toggle or P.CheckBox && events is not IValueControlEvents)
            throw new ArgumentException("Value controls require value event delivery.", nameof(events));
        native = element switch
        {
            P.PageView pages => backend.Window.RetainedPages(pages.Name),
            P.TabStrip tabs => backend.Window.TabStrip(tabs.Name),
            P.NavigationView navigation => backend.Window.NavigationView(navigation.Name),
            P.SingleChoice choice => backend.Window.ComboBox(choice.Name, false),
            P.RangeInput range => backend.Window.RangeInput(range.Name),
            P.Image image => backend.Window.Image(image.Name),
            P.Stack stack => backend.Window.Stack(stack.Axis == P.Axis.Horizontal ? Axis.Horizontal : Axis.Vertical)
                .Spacing(stack.SpacingValue).Padding(stack.PaddingValue),
            P.Grid grid => backend.Window.Grid(grid.Name),
            P.Label label => backend.Window.Label(label.Text),
            P.Button button => backend.Window.Button(button.Text),
            P.TextInput input => backend.SupportsForms
                ? backend.Window.TextInput(input.Name, (Xui.InputPurpose)input.Purpose)
                : backend.Window.TextInput(input.Name),
            P.MultilineText document => backend.Window.MultilineText(document.Name),
            P.PasswordInput password => backend.Window.PasswordInput(password.Name),
            P.Toggle toggle => backend.Window.Toggle(toggle.Name),
            P.CheckBox check => backend.Window.CheckBox(check.Name),
            P.Progress progress => backend.Window.Progress(progress.Name),
            P.ScrollView => null,
            P.Reveal => null,
            _ => throw new NotSupportedException($"Unsupported Windows portable element: {element.Kind}.")
        };
        // Native ScrollView construction needs its completed child, whereas portable creation is parent-first.
        if (native is not null) Initialize();
    }

    private void Initialize()
    {
        if (Native is Image image) image.CancelMemorySource();
        if (Element is P.Grid) Update(ElementProperty.Tracks);
        if (Element is P.Label { TextLayout: not null }) Update(ElementProperty.TextLayout);
        if (Element is P.PageView pages)
        {
            ((RetainedPages)Native).SetPages(WindowsPagePeer.Entries(pages.Pages), pages.Selected);
            ((RetainedPages)Native).SetVisible(pages.Visible);
        }
        if (Element is P.Reveal reveal)
            ((Reveal)Native).ApplyPortableState(WindowsRevealPeer.State(reveal.Open, reveal.Motion), initial: true);
        if (Element.FixedSize is { } fixedSize) Native.FixedSize(fixedSize.Width, fixedSize.Height);
        if (Element.PreferredSize is { } preferred) Native.PreferredSize(preferred.Width, preferred.Height);
        if (Element.WidthConstraints is not null || Element.HeightConstraints is not null)
            Update(ElementProperty.Constraints);
        if (Element is P.Control)
        {
            Update(ElementProperty.Name);
            Update(ElementProperty.AutomationId);
            Update(ElementProperty.Help);
            Update(ElementProperty.Visible);
            Update(ElementProperty.Enabled);
            if (((P.Control)Element).Typography is not null) Update(ElementProperty.Typography);
        }
        if (native is TextInput input)
        {
            Update(ElementProperty.Text);
            Update(ElementProperty.CaptionVisible);
            Update(ElementProperty.Placeholder);
            input.Changed += OnChanged;
            input.Submitted += OnSubmit;
            if (Control.SupportsInteraction && events is ITextInteractionEvents)
                interaction = input.ObserveInteraction(OnInteraction);
        }
        else if (native is Button button) button.Click += OnClick;
        else if (native is Toggle toggle)
        {
            Update(ElementProperty.Checked);
            toggle.Changed += OnToggleChanged;
        }
        else if (native is CheckBox check)
        {
            Update(ElementProperty.ThreeState);
            Update(ElementProperty.CheckState);
            check.Changed += OnCheckChanged;
        }
        else if (native is Progress)
        {
            Update(ElementProperty.Range);
            Update(ElementProperty.Value);
            Update(ElementProperty.ProgressState);
        }
        else if (native is MultilineText document)
        {
            document.MaximumLength = checked((ulong)((P.MultilineText)Element).MaximumLength);
            Update(ElementProperty.Text);
            Update(ElementProperty.ReadOnly);
            document.Event += e =>
            {
                if (e.Kind == EventKind.Change && !settingText && events is { } sink)
                {
                    string text = P.FormValues.NormalizeMultiline(document.Text, ((P.MultilineText)Element).MaximumLength);
                    Backend.Deliver(this, () => sink.Change(text), valueEvent: true);
                }
            };
        }
        else if (native is PasswordInput password)
        {
            password.MaximumLength = checked((ulong)((P.PasswordInput)Element).MaximumLength);
            password.RevealAllowed = false;
            password.OnChange(() =>
            {
                if (events is IPasswordControlEvents sink) Backend.Deliver(this, sink.PasswordChanged, valueEvent: true);
            });
        }
        else if (Element is P.PageSelector selector && native is Control selectorControl)
        {
            if (selector is P.TabStrip tabs) PageSelector.SetClosable((TabStrip)Native, tabs.Closable);
            else ((NavigationView)Native).Expanded = ((P.NavigationView)selector).Expanded;
            selectorControl.Event += e =>
            {
                if (events is not IPageControlEvents sink) return;
                if (e.Kind == EventKind.Selection) Backend.Deliver(this, () => sink.PageSelected(e.Value));
                else if (e.Kind == EventKind.Click) Backend.Deliver(this, () => sink.PageActivated(e.Value));
                else if (e.Kind == EventKind.Cancel && selector is P.TabStrip)
                    Backend.Deliver(this, () => sink.PageCloseRequested(e.Value));
            };
        }
        else if (native is ComboBox combo)
        {
            Update(ElementProperty.Choices);
            combo.Event += e =>
            {
                if (e.Kind == EventKind.Selection && !settingValue && events is ISelectionControlEvents sink)
                    Backend.Deliver(this, () => sink.SelectionChanged(e.Value), valueEvent: true);
            };
        }
        else if (native is RangeInput range)
        {
            range.Orientation = Axis.Horizontal;
            range.Reversed = false;
            Update(ElementProperty.Range);
            range.Event += e =>
            {
                if (settingValue || events is not IRangeControlEvents sink) return;
                double value = BitConverter.UInt64BitsToDouble(e.Value);
                if (e.Kind == EventKind.Preview) Backend.Deliver(this, () => sink.RangePreviewed(value), valueEvent: true);
                else if (e.Kind == EventKind.Change) Backend.Deliver(this, () => sink.RangeChanged(value), valueEvent: true);
                else if (e.Kind == EventKind.Cancel) Backend.Deliver(this, () => sink.RangeCanceled(value), valueEvent: true);
            };
        }
    }

    public void AddChild(IElementPeer child)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (child is not WindowsPeer peer || peer.Backend != Backend || peer.Element.Parent != Element ||
            children.Contains(peer))
            throw new ArgumentException("The native child does not belong to this element.", nameof(child));
        if (native is RetainedPages pages && Element is P.PageView modelPages)
            pages.Insert(children.Count, modelPages.Pages[children.Count].Id, peer.Native);
        else if (native is Stack stack) stack.Add(peer.Native, peer.Element.Flex);
        else if (native is Grid grid && Element is P.Grid)
        {
            var cell = peer.Element.Cell ?? throw new InvalidOperationException("A Grid child requires a cell placement.");
            grid.Add(peer.Native, cell.Row, cell.Column, cell.RowSpan, cell.ColumnSpan);
        }
        else if (Element is P.ScrollView scroll && native is null)
        {
            var view = Backend.Window.ScrollView(peer.Native, scroll.Name);
            native = view;
            if (ScrollView.SupportsFillViewport) view.FillViewport = false;
            Initialize();
        }
        else if (Element is P.Reveal reveal && native is null)
        {
            if (children.Count != 0) throw new InvalidOperationException("A retained reveal accepts one child.");
            native = Backend.Window.Reveal(peer.Native, reveal.Name);
            Initialize();
        }
        else throw new InvalidOperationException("This Windows element cannot accept the child.");
        children.Add(peer);
    }

    public virtual void Update(ElementProperty property)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        var control = Element as P.Control;
        switch (property)
        {
            case ElementProperty.Name:
                if (Native is PasswordInput passwordName) passwordName.Name = control!.Name;
                else ((Control)Native).Name = control!.Name;
                break;
            case ElementProperty.AutomationId:
                if (Native is PasswordInput passwordId) passwordId.AutomationId = control!.AutomationId;
                else ((Control)Native).AutomationId = control!.AutomationId;
                break;
            case ElementProperty.Help:
                if (Native is PasswordInput passwordHelp) passwordHelp.SetHelp(control!.Help);
                else ((Control)Native).Help(control!.Help);
                break;
            case ElementProperty.Visible:
                InvalidateEvents();
                if (Element is P.PageView pagesVisible) ((RetainedPages)Native).SetVisible(pagesVisible.Visible);
                else if (Native is PasswordInput passwordVisible) passwordVisible.SetVisible(control!.Visible);
                else ((Control)Native).Visible(control!.Visible);
                break;
            case ElementProperty.Enabled:
                InvalidateEvents();
                UpdateEnabled(); break;
            case ElementProperty.Text:
                Revision++;
                SetText(Element is P.TextInput inputText ? inputText.Text : ((P.MultilineText)Element).Text); break;
            case ElementProperty.ReadOnly:
                Revision++;
                ((MultilineText)Native).ReadOnly = ((P.MultilineText)Element).ReadOnly;
                break;
            case ElementProperty.CaptionVisible:
                ((TextInput)Native).SetCaptionVisible(((P.TextInput)Element).CaptionVisible); break;
            case ElementProperty.Placeholder:
                ((TextInput)Native).SetPlaceholder(((P.TextInput)Element).Placeholder); break;
            case ElementProperty.Checked:
            case ElementProperty.CheckState:
                Revision++;
                SynchronizeValue(); break;
            case ElementProperty.ThreeState:
                Revision++;
                ((CheckBox)Native).ThreeState = ((P.CheckBox)Element).ThreeState;
                SynchronizeValue(); break;
            case ElementProperty.Range:
                if (Element is P.RangeInput) { Revision++; SetRange(); break; }
                var progress = (P.Progress)Element;
                var range = progress.Range;
                ((Progress)Native).Range = new(range.Minimum, range.Maximum, range.SmallStep, range.LargeStep);
                ((Progress)Native).Value = progress.Value; break;
            case ElementProperty.Value:
                if (Element is P.RangeInput) { Revision++; SetRange(); break; }
                ((Progress)Native).Value = ((P.Progress)Element).Value;
                break;
            case ElementProperty.Choices:
                Revision++;
                SetChoices();
                break;
            case ElementProperty.ProgressState:
                ((Progress)Native).State = ((P.Progress)Element).State switch
                {
                    P.ProgressState.Determinate => ProgressState.Determinate,
                    P.ProgressState.Indeterminate => ProgressState.Indeterminate,
                    _ => throw new NotSupportedException("Unsupported portable progress state.")
                };
                break;
            case ElementProperty.Spacing: ((Stack)Native).Spacing(((P.Stack)Element).SpacingValue); break;
            case ElementProperty.Padding: ((Stack)Native).Padding(((P.Stack)Element).PaddingValue); break;
            case ElementProperty.FixedSize:
                var size = Element.FixedSize ?? throw new InvalidOperationException("Missing fixed size.");
                Native.FixedSize(size.Width, size.Height); break;
            case ElementProperty.PreferredSize:
                var preferred = Element.PreferredSize ?? throw new InvalidOperationException("Missing preferred size.");
                Native.PreferredSize(preferred.Width, preferred.Height); break;
            case ElementProperty.Constraints:
                static Xui.AxisConstraints? Convert(P.AxisConstraints? value) => value is { } axis
                    ? new(axis.Length, axis.Minimum, axis.Maximum) : null;
                Native.SetAxisConstraints(Convert(Element.WidthConstraints), Convert(Element.HeightConstraints));
                break;
            case ElementProperty.Tracks:
                var grid = (P.Grid)Element;
                ((Grid)Native).SetTracks(ConvertTracks(grid.Rows), ConvertTracks(grid.Columns));
                break;
            case ElementProperty.Typography:
                ApplyTypography(((P.Control)Element).Typography);
                break;
            case ElementProperty.Pages:
                if (Element is P.PageView pagesModel)
                    ((RetainedPages)Native).SetPages(WindowsPagePeer.Entries(pagesModel.Pages), pagesModel.Selected);
                else
                {
                    var selector = (P.PageSelector)Element;
                    PageSelector.Set((Control)Native, WindowsPagePeer.Entries(selector.Pages.Pages), selector.Selected);
                }
                break;
            case ElementProperty.Expanded:
                ((NavigationView)Native).Expanded = ((P.NavigationView)Element).Expanded;
                break;
            case ElementProperty.Closable:
                PageSelector.SetClosable((TabStrip)Native, ((P.TabStrip)Element).Closable);
                break;
            case ElementProperty.TextLayout:
                var textLayout = ((P.Label)Element).TextLayout;
                ValidateTextLayout(textLayout);
                var label = (Label)Native;
                if (textLayout is null)
                {
                    if (!ownsLabelLayout) break;
                    label.SetTextLayout(labelLayoutBaseline);
                    ownsLabelLayout = false;
                    labelLayoutBaseline = null;
                    break;
                }
                if (!ownsLabelLayout)
                {
                    labelLayoutBaseline = label.GetTextLayout();
                    ownsLabelLayout = true;
                }
                label.SetTextLayout(new LabelLayout(textLayout.Wrapping, textLayout.MaximumLines, textLayout.Overflow switch
                    {
                        P.TextOverflow.Clip => LabelOverflow.Clip,
                        P.TextOverflow.CharacterEllipsis => LabelOverflow.CharacterEllipsis,
                        _ => throw new ArgumentOutOfRangeException(nameof(textLayout))
                    }));
                break;
            case ElementProperty.RevealState:
                var revealModel = (P.Reveal)Element;
                ((Reveal)Native).ApplyPortableState(WindowsRevealPeer.State(revealModel.Open, revealModel.Motion));
                break;
            default: throw new NotSupportedException($"Unsupported Windows update: {property}.");
        }
    }

    public void ValidateTypography(P.Typography? typography)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (typography is null) return;
        if (Element is not (P.Label or P.Button or P.TextInput or P.Toggle or P.CheckBox))
            throw new NotSupportedException("Windows portable typography is limited to labels, buttons, single-line inputs, toggles, and check boxes.");
        if (typography.ResolvedFontSize is < 8 or > 32 || typography.ResolvedFontWeight is not (400 or 700))
            throw new ArgumentOutOfRangeException(nameof(typography));
    }

    public void ValidateTextLayout(P.LabelTextLayout? layout)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (layout is null) return;
        if (Element is not P.Label label || !Label.SupportsTextLayout)
            throw new NotSupportedException("The Windows runtime does not provide the explicit native Label text layout contract.");
        layout.ValidateText(label.Text);
    }

    private void ApplyTypography(P.Typography? typography)
    {
        ValidateTypography(typography);
        if (typography is null)
        {
            if (typographyBaseline is null) return;
            foreach (var pair in typographyBaseline) Native.SetControlStyleValues(pair.Key, pair.Value);
            typographyBaseline = null;
            return;
        }
        if (typographyBaseline is null)
        {
            StylePart[] parts = Element is P.TextInput ? [StylePart.Text, StylePart.Header] : [StylePart.Label];
            typographyBaseline = parts.ToDictionary(part => part, part => Native.GetControlStyleValues(part));
        }
        foreach (var pair in typographyBaseline)
            Native.SetControlStyleValues(pair.Key, pair.Value with
            {
                FontSize = typography.ResolvedFontSize,
                FontWeight = typography.ResolvedFontWeight
            });
    }

    public bool TryFocus()
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (Element is P.Label or P.Progress or P.Image) return false;
        try
        {
            if (Native is PasswordInput password) password.Focus();
            else if (Native is Control control) control.Focus();
            else return false;
            return true;
        }
        catch (XuiException error) when (error.Status == 1) { return false; }
    }

    public bool HasFocus
    {
        get
        {
            Backend.VerifyAccess();
            ObjectDisposedException.ThrowIf(Disposed, this);
            return Native is PasswordInput password ? password.Focused :
                Native is Control control && Element is not (P.Label or P.Progress or P.Image) && control.Focused;
        }
    }

    private static GridTrack[] ConvertTracks(IReadOnlyList<P.GridTrack> tracks) =>
        tracks.Select(track => new GridTrack(track.Sizing switch
        {
            P.TrackSizing.Fixed => TrackSizing.Fixed,
            P.TrackSizing.Automatic => TrackSizing.Automatic,
            P.TrackSizing.Star => TrackSizing.Star,
            _ => throw new NotSupportedException("Unsupported portable Grid track sizing.")
        }, track.Value, track.Minimum, track.Maximum)).ToArray();

    internal void InvalidateEvents()
    {
        Revision++;
        if (pendingValues != 0 || Element is P.RangeInput) SynchronizeValue();
        foreach (var child in children) child.InvalidateEvents();
    }

    internal void PrepareRemoval()
    {
        if (this is WindowsRevealPeer reveal) reveal.CancelMotion();
        if (this is WindowsImagePeer image) image.CancelImage();
        foreach (var child in children) child.PrepareRemoval();
    }

    internal void BeginValueDelivery() => pendingValues++;
    internal void EndValueDelivery() => pendingValues--;

    private void SynchronizeValue()
    {
        settingValue = true;
        try
        {
            if (Element is P.TextInput input) SetText(input.Text);
            else if (Element is P.MultilineText document) SetText(document.Text);
            else if (Element is P.SingleChoice) SetChoices();
            else if (Element is P.RangeInput) SetRange();
            else if (Element is P.Toggle toggle) ((Toggle)Native).Checked = toggle.Checked;
            else if (Element is P.CheckBox check)
                ((CheckBox)Native).State = check.State switch
                {
                    P.CheckState.Unchecked => CheckState.Unchecked,
                    P.CheckState.Checked => CheckState.Checked,
                    P.CheckState.Indeterminate => CheckState.Indeterminate,
                    _ => throw new NotSupportedException("Unsupported portable check state.")
                };
        }
        finally { settingValue = false; }
    }

    private void SetChoices()
    {
        var model = (P.SingleChoice)Element;
        settingValue = true;
        try { ((ComboBox)Native).SetItems(model.Items.Select(item => new Choice(item.Id, item.Text, item.Enabled)).ToArray(), model.Selected); }
        finally { settingValue = false; }
    }

    private void SetRange()
    {
        var model = (P.RangeInput)Element;
        var range = (RangeInput)Native;
        settingValue = true;
        try
        {
            range.Range = new NumericRange(model.Range.Minimum, model.Range.Maximum, model.Range.SmallStep, model.Range.LargeStep);
            range.Value = model.Value;
        }
        finally { settingValue = false; }
    }

    private void UpdateEnabled()
    {
        if (Native is Control or PasswordInput)
        {
            bool enabled = true;
            for (P.Element? current = Element; current is not null; current = current.Parent)
                if (current is P.Control ancestor && !ancestor.Enabled) enabled = false;
            if (Native is PasswordInput password) password.Enabled = enabled;
            else ((Control)Native).Enabled = enabled;
        }
        foreach (var child in children) child.UpdateEnabled();
    }

    private void SetText(string text)
    {
        if (Native is MultilineText document)
        {
            if (P.FormValues.NormalizeMultiline(document.Text, ((P.MultilineText)Element).MaximumLength) == text) return;
            settingText = true;
            try { document.Text = text; }
            finally { settingText = false; }
            return;
        }
        var input = (TextInput)Native;
        if (input.Text == text) return;
        var selection = input.Selection;
        settingText = true;
        try
        {
            input.Text = text;
            input.Selection = selection;
        }
        finally { settingText = false; }
    }

    private void OnClick()
    {
        if (events is { } sink) Backend.Deliver(this, sink.Click);
    }

    private void OnChanged(string text)
    {
        if (!settingText && events is { } sink) Backend.Deliver(this, () => sink.Change(text), valueEvent: true);
    }

    private void OnToggleChanged(bool value)
    {
        if (!settingValue && events is IValueControlEvents sink)
            Backend.Deliver(this, () => sink.ToggleChanged(value), valueEvent: true);
    }

    private void OnCheckChanged(CheckState value)
    {
        if (settingValue || events is not IValueControlEvents sink) return;
        var state = value switch
        {
            CheckState.Unchecked => P.CheckState.Unchecked,
            CheckState.Checked => P.CheckState.Checked,
            CheckState.Indeterminate => P.CheckState.Indeterminate,
            _ => throw new InvalidOperationException("Native Windows delivered an unknown check state.")
        };
        Backend.Deliver(this, () => sink.CheckChanged(state), valueEvent: true);
    }

    private void OnSubmit()
    {
        if (events is { } sink) Backend.Deliver(this, sink.Submit);
    }

    private void OnInteraction(ControlInteraction value)
    {
        if (Disposed) return;
        pendingInteraction = value;
        FlushInteraction();
    }

    internal void FlushInteraction()
    {
        if (Disposed || !Backend.Mounted || pendingInteraction is null || interactionScheduled) return;
        interactionScheduled = true;
        try
        {
            Backend.PostNotification(this, () =>
            {
                var value = pendingInteraction;
                pendingInteraction = null;
                interactionScheduled = false;
                if (value is { } snapshot && events is ITextInteractionEvents sink)
                    sink.InteractionChanged(new(snapshot.HasFocus, snapshot.IsComposing));
            }, () => { pendingInteraction = null; interactionScheduled = false; });
        }
        catch
        {
            interactionScheduled = false;
            throw;
        }
    }

    public virtual void Dispose()
    {
        Backend.VerifyAccess();
        if (Disposed) return;
        interaction?.Dispose();
        interaction = null;
        pendingInteraction = null;
        typographyBaseline = null;
        labelLayoutBaseline = null;
        ownsLabelLayout = false;
        Backend.ReleasePeer(this);
        Disposed = true;
        native = null;
        events = null;
        element = null;
        children.Clear();
    }
}

internal class WindowsTextPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsPeer(backend, element, events), ITextSelectionPeer
{
    public P.TextSelection Selection
    {
        get
        {
            Backend.VerifyAccess();
            ObjectDisposedException.ThrowIf(Disposed, this);
            var selection = ((TextInput)Native).Selection;
            return new(checked((int)selection.Start), checked((int)selection.End));
        }
        set
        {
            Backend.VerifyAccess();
            ObjectDisposedException.ThrowIf(Disposed, this);
            var input = (TextInput)Native;
            var selection = value.ClampTo(input.Text);
            input.Selection = new(checked((ulong)selection.Start), checked((ulong)selection.End));
        }
    }
}

internal class WindowsConstrainedTextPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsTextPeer(backend, element, events), IConstrainedElementPeer { }

internal class WindowsConstrainedPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsPeer(backend, element, events), IConstrainedElementPeer { }

internal class WindowsConstrainedMutablePeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsMutablePeer(backend, element, events), IConstrainedElementPeer { }

internal class WindowsMutablePeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsPeer(backend, element, events), IMutationPreflightPeer
{
    public void ValidateMutation()
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        Backend.ValidateMutation();
    }

    private WindowsPeer Child(IElementPeer child, bool existing)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (child is not WindowsPeer peer || peer.Backend != Backend || peer.Disposed ||
            children.Contains(peer) != existing || (!existing && peer.Element.Parent != Element))
            throw new ArgumentException("The native child does not belong to this mutable stack.", nameof(child));
        return peer;
    }

    public void InsertChild(int index, IElementPeer child)
    {
        var peer = Child(child, existing: false);
        Backend.FinishAppend();
        ((Stack)Native).Insert(index, peer.Native, peer.Element.Flex);
        children.Insert(index, peer);
    }

    public void RemoveChild(IElementPeer child)
    {
        var peer = Child(child, existing: true);
        peer.InvalidateEvents();
        peer.PrepareRemoval();
        ((Stack)Native).Remove(peer.Native);
        children.Remove(peer);
    }

    public void ValidateMove(IElementPeer child, int index)
    {
        var peer = Child(child, existing: true);
        ((Stack)Native).ValidateMove(peer.Native, index);
    }

    public void MoveChild(IElementPeer child, int index)
    {
        var peer = Child(child, existing: true);
        ((Stack)Native).Move(peer.Native, index);
        children.Remove(peer);
        children.Insert(index, peer);
    }
}

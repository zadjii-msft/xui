using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal sealed partial class AndroidPeer
{
    internal bool OwnVisible => Element switch
    {
        Control control => control.Visible,
        PageView pages => pages.Visible,
        _ => true
    };

    private static bool PageIsPresented(PageView pages, Element child)
    {
        if (!pages.Visible) return false;
        for (int i = 0; i < pages.Children.Count; i++)
            if (ReferenceEquals(pages.Children[i], child))
                return i < pages.Pages.Count && pages.Pages[i].Enabled && pages.Pages[i].Id == pages.Selected;
        return false;
    }

    private bool DirectPageIsPresented() => Element.Parent is not PageView pages || PageIsPresented(pages, Element);

    internal bool HasComposingDescendant() => Interaction?.IsComposing == true || children.Any(child => child.HasComposingDescendant());

    public void ValidateMutation()
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (View.Content is NativePageLayout pages) pages.ValidateMutation();
    }

    public void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? selected)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(pages);
        if (View.Content is NativePageLayout native) native.ValidatePages(pages, selected);
        else if (Element is not PageSelector) throw new NotSupportedException("This native peer is not a page control.");
    }

    public void ValidateVisibility(bool visible)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (View.Content is not NativePageLayout pages) throw new NotSupportedException("This native peer is not a retained page host.");
        pages.ValidateVisibility(visible);
    }

    public void ConnectPages(IPageViewElementPeer pages)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (Element is not PageSelector selector || pages is not AndroidPeer peer || peer.Backend != Backend ||
            !ReferenceEquals(peer.Element, selector.Pages) || peer.View.Content is not NativePageLayout)
            throw new ArgumentException("Native page controls must connect within one attachment to their authored page host.", nameof(pages));
        ApplyPages();
    }

    private void ApplyPages()
    {
        if (View.Content is NativePageLayout pages) pages.ApplyPages();
        else if (Element is TabStrip strip) tabs!.Apply(strip.Pages.Pages, strip.Selected, strip.Closable);
        else if (Element is NavigationView view) navigation!.Apply(view.Pages.Pages, view.Selected, view.Expanded);
    }

    private IPageControlEvents PageEvents => events as IPageControlEvents
        ?? throw new InvalidOperationException("Native page controls require typed page events.");
    private bool SelectPage(ulong id) => Deliver(() => PageEvents.PageSelected(id));
    private bool ActivatePage(ulong id) => Deliver(() =>
    {
        if (navigation is not null && !Backend.HasComposition && !TryFocus())
            throw new InvalidOperationException("The native navigation control could not receive activation focus.");
        return PageEvents.PageActivated(id);
    });
    private bool ClosePage(ulong id) => Deliver(() => PageEvents.PageCloseRequested(id));
}

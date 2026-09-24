using Xui.Experimental.Portable;
using P = Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

internal sealed class WindowsPagePeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsConstrainedPeer(backend, element, events), IPageViewElementPeer
{
    internal static RetainedPageEntry[] Entries(IReadOnlyList<P.PageEntry> pages) =>
        pages.Select(page => new RetainedPageEntry(page.Id, page.Title, page.Enabled)).ToArray();
    private RetainedPages Pages => (RetainedPages)Native;
    private WindowsPeer Child(IElementPeer child, bool present)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (child is not WindowsPeer peer || peer.Backend != Backend || peer.Disposed ||
            children.Contains(peer) != present || (!present && peer.Element.Parent != Element))
            throw new ArgumentException("The page root does not belong to this native page host.", nameof(child));
        return peer;
    }
    public void ValidatePages(IReadOnlyList<P.PageEntry> pages, ulong? selected) => Pages.ValidatePages(Entries(pages), selected);
    public void ValidateVisibility(bool visible) => Pages.ValidateVisible(visible);
    public void InsertChild(int index, IElementPeer child)
    {
        var peer = Child(child, present: false);
        Backend.FinishAppend();
        Pages.Insert(index, ((P.PageView)Element).Pages[index].Id, peer.Native);
        children.Insert(index, peer);
    }
    public void RemoveChild(IElementPeer child)
    {
        var peer = Child(child, present: true);
        peer.InvalidateEvents();
        peer.PrepareRemoval();
        Pages.Remove(peer.Native);
        children.Remove(peer);
    }
    public void ValidateMove(IElementPeer child, int index) => Pages.ValidateMove(Child(child, present: true).Native, index);
    public void MoveChild(IElementPeer child, int index)
    {
        var peer = Child(child, present: true);
        Pages.Move(peer.Native, index);
        children.Remove(peer);
        children.Insert(index, peer);
    }
}

internal sealed class WindowsPageSelectorPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsConstrainedPeer(backend, element, events), IPageSelectorElementPeer
{
    public void ValidatePages(IReadOnlyList<P.PageEntry> pages, ulong? selected)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (Element is P.TabStrip && pages.Any(page => !page.Enabled))
            throw new NotSupportedException("This native tab strip does not support disabled page headers.");
        PageSelector.Validate((Control)Native, WindowsPagePeer.Entries(pages), selected);
    }
    public void ConnectPages(IPageViewElementPeer pages)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (pages is not WindowsPagePeer peer || peer.Backend != Backend || peer.Disposed)
            throw new ArgumentException("The selector and page host require the same native attachment.", nameof(pages));
        PageSelector.Connect((Control)Native, (RetainedPages)peer.Native);
        Update(ElementProperty.Pages);
    }
}

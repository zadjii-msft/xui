using System.Globalization;

namespace Xui.Experimental.Portable;

public readonly record struct PageEntry(ulong Id, string Title, bool Enabled = true);

public sealed class PageItem
{
    public PageEntry Entry { get; }
    internal KeyedItem Content { get; }

    private PageItem(PageEntry entry, KeyedItem content) { Entry = entry; Content = content; }

    public static PageItem Create<T>(ulong id, string title, Func<Host, T> create, Action<T>? update = null, bool enabled = true)
        where T : class, IPortableComponent
    {
        ValidateEntry(new(id, title, enabled));
        return new(new(id, title, enabled), KeyedItem.Create(id.ToString(CultureInfo.InvariantCulture), create, update));
    }

    internal static void ValidateEntry(PageEntry entry)
    {
        if (entry.Id == 0 || entry.Id > SingleChoice.MaximumId)
            throw new ArgumentOutOfRangeException(nameof(entry), "A page ID must be nonzero and within the supported 64-bit identity range.");
        ArgumentException.ThrowIfNullOrWhiteSpace(entry.Title);
        Values.Text(entry.Title);
        if (entry.Title.Length > 1024) throw new ArgumentOutOfRangeException(nameof(entry), "A page title supports at most 1024 UTF-16 code units.");
    }
}

public interface IPageViewElementPeer : IMutableElementPeer
{
    void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? selected);
    void ValidateVisibility(bool visible);
}

public interface IPageSelectorElementPeer : IElementPeer
{
    void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? selected);
    void ConnectPages(IPageViewElementPeer pages);
}

public interface IPageControlEvents : IControlEvents
{
    bool PageSelected(ulong id);
    bool PageActivated(ulong id);
    bool PageCloseRequested(ulong id);
}

public sealed class PageView : KeyedStack
{
    internal sealed record Snapshot(PageItem[] Items, IReadOnlyList<PageEntry> Pages, ulong? Selected);
    private Snapshot snapshot = new([], Array.AsReadOnly(Array.Empty<PageEntry>()), null);
    private readonly string name;
    private bool visible = true;
    internal List<PageSelector> Selectors { get; } = [];
    public string Name => Read(name);
    public IReadOnlyList<PageEntry> Pages => Read(snapshot).Pages;
    public ulong? Selected => Read(snapshot).Selected;
    public bool Visible
    {
        get => Read(visible);
        set
        {
            VerifyAccess();
            Owner.VerifyElementMutation(this);
            if (visible == value) return;
            Owner.SetPageVisibility(this, value);
        }
    }

    internal PageView(Host host, string name) : base(host, Axis.Vertical, ElementKind.PageView) { this.name = Values.Text(name); }

    public PageView SetPages(IReadOnlyList<PageItem> pages, ulong? selected = null)
        => Owner.PageOperation(() => SetPagesCore(pages, selected));

    private PageView SetPagesCore(IReadOnlyList<PageItem> pages, ulong? selected)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        Snapshot next;
        try
        {
            ArgumentNullException.ThrowIfNull(pages);
            var items = pages.ToArray();
            if (items.Length > 4096) throw new ArgumentOutOfRangeException(nameof(pages), "A page host supports at most 4096 open pages.");
            var entries = new PageEntry[items.Length];
            var ids = new HashSet<ulong>();
            for (int i = 0; i < items.Length; i++)
            {
                ArgumentNullException.ThrowIfNull(items[i]);
                entries[i] = items[i].Entry;
                PageItem.ValidateEntry(entries[i]);
                if (!ids.Add(entries[i].Id)) throw new ArgumentException("Page IDs must be unique within their host.", nameof(pages));
            }
            if (selected.HasValue) RequireEnabled(entries, selected.Value);
            else if (snapshot.Selected is ulong previous && entries.Any(entry => entry.Id == previous && entry.Enabled)) selected = previous;
            else
            {
                foreach (var entry in entries)
                    if (entry.Enabled) { selected = entry.Id; break; }
            }
            next = new(items, Array.AsReadOnly(entries), selected);
        }
        catch (Exception error) { throw new KeyedUpdateException(false, error); }
        Owner.Reconcile(this, next.Items.Select(item => item.Content).ToArray(), next);
        return this;
    }

    public PageView SetSelected(ulong selected)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        RequireEnabled(snapshot.Pages, selected);
        if (snapshot.Selected == selected) return this;
        Owner.SelectPage(this, snapshot with { Selected = selected });
        return this;
    }

    internal void CommitSnapshot(Snapshot next)
    {
        snapshot = next;
        ResetInactivePreview();
    }
    internal void CommitVisibility(bool value)
    {
        visible = value;
        ResetInactivePreview();
    }
    private void ResetInactivePreview()
    {
        foreach (var child in Children)
            if (!Presents(child)) Owner.ResetRangePreviews(child);
    }
    internal bool Presents(Element child)
    {
        int index = Entries.FindIndex(entry => ReferenceEquals(entry.Root, child));
        return visible && index >= 0 && index < snapshot.Pages.Count && snapshot.Pages[index].Enabled &&
            snapshot.Pages[index].Id == snapshot.Selected;
    }

    internal void RequireEnabled(ulong id) => RequireEnabled(snapshot.Pages, id);
    private static void RequireEnabled(IReadOnlyList<PageEntry> pages, ulong id)
    {
        if (!pages.Any(page => page.Id == id && page.Enabled))
            throw new ArgumentException("The selected page must exist and be enabled.", nameof(id));
    }

    internal override void Release()
    {
        snapshot = new([], Array.AsReadOnly(Array.Empty<PageEntry>()), null);
        Selectors.Clear();
        base.Release();
    }
}

public abstract class PageSelector : Control
{
    private PageView? pages;
    private Action<ulong>? changed;
    private Action<ulong>? activated;
    public PageView Pages { get { VerifyAccess(); return pages ?? throw new InvalidOperationException("Bind the selector to a PageView during construction."); } }
    public ulong? Selected => Pages.Selected;
    public event Action<ulong> Changed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); changed += value; }
        remove { Owner.VerifyEventRemoval(this); changed -= value; }
    }
    public event Action<ulong> Activated
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); activated += value; }
        remove { Owner.VerifyEventRemoval(this); activated -= value; }
    }

    private protected PageSelector(Host host, ElementKind kind, string name) : base(host, kind, name) { }

    public void BindPages(PageView value)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(value);
        Owner.VerifyBuilding();
        Owner.VerifyBuildElement(this);
        Owner.VerifyComponent(value);
        Owner.VerifyBuildElement(value);
        if (pages is not null) throw new InvalidOperationException("A page selector can bind only once.");
        pages = value;
        value.Selectors.Add(this);
    }

    internal void RaiseSelected(ulong id)
    {
        var target = Pages;
        ulong? previous = target.Selected;
        try { target.SetSelected(id); }
        catch (Exception error)
        {
            Owner.RestorePageSelector(this, error);
            throw;
        }
        if (previous != target.Selected) changed?.Invoke(id);
    }
    internal void RaiseActivated(ulong id)
    {
        RaiseSelected(id);
        if (!Disposed && Pages.Selected == id && AcceptsInput()) activated?.Invoke(id);
    }
    internal override void Release()
    {
        pages?.Selectors.Remove(this);
        pages = null;
        changed = null;
        activated = null;
        base.Release();
    }
}

public sealed class TabStrip : PageSelector
{
    private bool closable;
    private Action<ulong>? closeRequested;
    public bool Closable { get => Read(closable); set => Set(ref closable, value, ElementProperty.Closable); }
    public event Action<ulong> CloseRequested
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); closeRequested += value; }
        remove { Owner.VerifyEventRemoval(this); closeRequested -= value; }
    }
    internal TabStrip(Host host, string name) : base(host, ElementKind.TabStrip, name) { }
    internal void RaiseCloseRequested(ulong id)
    {
        Pages.RequireEnabled(id);
        if (!closable) throw new InvalidOperationException("This tab strip does not expose close requests.");
        closeRequested?.Invoke(id);
    }
    internal override void Release() { closeRequested = null; base.Release(); }
}

public sealed class NavigationView : PageSelector
{
    private bool expanded = true;
    public bool Expanded { get => Read(expanded); set => Set(ref expanded, value, ElementProperty.Expanded); }
    internal NavigationView(Host host, string name) : base(host, ElementKind.NavigationView, name) { }
}

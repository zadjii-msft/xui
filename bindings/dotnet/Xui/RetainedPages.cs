using System.Runtime.InteropServices;

namespace Xui;

public readonly record struct RetainedPageEntry(ulong Id, string Title, bool Enabled = true);
public readonly record struct RetainedPageState(ulong? Selected, bool Visible, uint Count);

public sealed unsafe class RetainedPages : Element
{
    internal RetainedPages(Window window, ulong handle) : base(window, handle) { }
    public static bool Available => Native.RetainedPagesAvailable.Value;

    internal static void Require()
    {
        if (!Available) throw new NotSupportedException("The native runtime does not provide retained pages and linked selectors.");
    }

    public void ValidatePages(IReadOnlyList<RetainedPageEntry> entries, ulong? selected) =>
        ApplyEntries(this, entries, selected, validate: true, selector: false);
    public void SetPages(IReadOnlyList<RetainedPageEntry> entries, ulong? selected) =>
        ApplyEntries(this, entries, selected, validate: false, selector: false);

    internal static void ApplyEntries(Element target, IReadOnlyList<RetainedPageEntry> entries, ulong? selected,
        bool validate, bool selector)
    {
        ArgumentNullException.ThrowIfNull(entries);
        target.Window.Guard();
        Require();
        if (entries.Count > 4096) throw new ArgumentOutOfRangeException(nameof(entries));
        var values = new Native.RetainedPageEntry[entries.Count];
        var ids = new HashSet<ulong>();
        using var pins = new Window.Pins();
        for (int i = 0; i < values.Length; i++)
        {
            var entry = entries[i];
            if (entry.Id == 0 || entry.Id > (ulong)long.MaxValue - 100 || !ids.Add(entry.Id))
                throw new ArgumentException("Page IDs must be unique, nonzero, and within the supported native range.", nameof(entries));
            ArgumentException.ThrowIfNullOrWhiteSpace(entry.Title);
            if (entry.Title.Length > 1024) throw new ArgumentOutOfRangeException(nameof(entries));
            values[i] = new() { Size = 32, Enabled = entry.Enabled ? 1u : 0u, Id = entry.Id, Title = pins.Text(entry.Title) };
        }
        if (selected.HasValue && !entries.Any(entry => entry.Id == selected.Value && entry.Enabled))
            throw new ArgumentException("The selected page must exist and be enabled.", nameof(selected));
        fixed (Native.RetainedPageEntry* pointer = values)
        {
            int status = selector
                ? validate ? Native.PageSelectorValidate(target.Handle, pointer, (uint)values.Length, selected ?? 0)
                    : Native.PageSelectorSet(target.Handle, pointer, (uint)values.Length, selected ?? 0)
                : validate ? Native.RetainedPagesValidate(target.Handle, pointer, (uint)values.Length, selected ?? 0)
                    : Native.RetainedPagesSet(target.Handle, pointer, (uint)values.Length, selected ?? 0);
            target.Window.Check(status);
        }
    }

    public void ValidateVisible(bool visible) { Window.Guard(); Window.Check(Native.RetainedPagesValidateVisible(Handle, visible ? 1u : 0u)); }
    public void SetVisible(bool visible) { Window.Guard(); Window.Check(Native.RetainedPagesSetVisible(Handle, visible ? 1u : 0u)); }
    public RetainedPageState State
    {
        get
        {
            Window.Guard();
            Window.Check(Native.RetainedPagesGetState(Handle, out ulong selected, out uint visible, out uint count));
            if (visible > 1 || count > 4096) throw new InvalidOperationException("The native retained-page state is invalid.");
            return new(selected == 0 ? null : selected, visible != 0, count);
        }
    }
    public ulong GetPageId(int index)
    {
        Window.Guard();
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Window.Check(Native.RetainedPagesGetPageId(Handle, checked((uint)index), out ulong id));
        return id;
    }
    public void Insert(int index, ulong id, Element child)
    {
        ArgumentNullException.ThrowIfNull(child);
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        if (id == 0 || id > (ulong)long.MaxValue - 100) throw new ArgumentOutOfRangeException(nameof(id));
        child.BelongsTo(Window);
        Window.Check(Native.RetainedPagesInsert(Handle, checked((uint)index), id, child.Handle));
    }
    public void Remove(Element child)
    {
        ArgumentNullException.ThrowIfNull(child);
        child.BelongsTo(Window);
        Window.Check(Native.RetainedPagesRemove(Handle, child.Handle));
    }
    public void ValidateMove(Element child, int index)
    {
        ArgumentNullException.ThrowIfNull(child);
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        child.BelongsTo(Window);
        Window.Check(Native.RetainedPagesValidateMove(Handle, child.Handle, checked((uint)index)));
    }
    public void Move(Element child, int index)
    {
        ArgumentNullException.ThrowIfNull(child);
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        child.BelongsTo(Window);
        Window.Check(Native.RetainedPagesMove(Handle, child.Handle, checked((uint)index)));
    }
}

public sealed partial class Window
{
    public RetainedPages RetainedPages(string name)
    {
        Guard();
        global::Xui.RetainedPages.Require();
        using var pins = new Pins();
        Check(Native.RetainedPagesCreate(Handle, pins.Text(name), out ulong handle));
        return new(this, handle);
    }
}

public static class PageSelector
{
    public static (ulong? Selected, uint Count) GetState(Control selector)
    {
        ArgumentNullException.ThrowIfNull(selector);
        selector.Window.Guard();
        RetainedPages.Require();
        selector.Window.Check(Native.PageSelectorGetState(selector.Handle, out ulong selected, out uint count));
        if (count > 4096) throw new InvalidOperationException("The native selector returned an invalid item count.");
        return (selected == 0 ? null : selected, count);
    }
    public static void SetClosable(TabStrip selector, bool value)
    {
        ArgumentNullException.ThrowIfNull(selector);
        selector.Window.Guard();
        RetainedPages.Require();
        selector.Window.Check(Native.PageSelectorSetClosable(selector.Handle, value ? 1u : 0u));
    }
    public static bool GetClosable(TabStrip selector)
    {
        ArgumentNullException.ThrowIfNull(selector);
        selector.Window.Guard();
        RetainedPages.Require();
        selector.Window.Check(Native.PageSelectorGetClosable(selector.Handle, out uint value));
        if (value > 1) throw new InvalidOperationException("The native tab strip returned an invalid close-affordance state.");
        return value != 0;
    }
    public static void Validate(Control selector, IReadOnlyList<RetainedPageEntry> entries, ulong? selected) =>
        RetainedPages.ApplyEntries(selector, entries, selected, validate: true, selector: true);
    public static void Set(Control selector, IReadOnlyList<RetainedPageEntry> entries, ulong? selected) =>
        RetainedPages.ApplyEntries(selector, entries, selected, validate: false, selector: true);
    public static void Connect(Control selector, RetainedPages pages)
    {
        ArgumentNullException.ThrowIfNull(selector);
        ArgumentNullException.ThrowIfNull(pages);
        RetainedPages.Require();
        pages.BelongsTo(selector.Window);
        selector.Window.Check(Native.PageSelectorConnect(selector.Handle, pages.Handle));
    }
}

internal static unsafe partial class Native
{
    internal static readonly Lazy<bool> RetainedPagesAvailable = new(() =>
        HasExports("xui_retained_pages_version", "xui_retained_pages_create", "xui_retained_pages_validate",
            "xui_retained_pages_set", "xui_retained_pages_validate_visible", "xui_retained_pages_set_visible",
            "xui_retained_pages_get_state", "xui_retained_pages_insert", "xui_retained_pages_remove",
            "xui_retained_pages_validate_move", "xui_retained_pages_move", "xui_page_selector_connect",
            "xui_page_selector_validate", "xui_page_selector_set", "xui_page_selector_set_closable",
            "xui_page_selector_get_closable", "xui_retained_pages_get_page_id",
            "xui_page_selector_get_state") && RetainedPagesVersion() == 0x10000);
    [StructLayout(LayoutKind.Sequential)]
    internal struct RetainedPageEntry { internal uint Size, Enabled; internal ulong Id; internal Text Title; }
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_version")] internal static partial uint RetainedPagesVersion();
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_create")] internal static partial int RetainedPagesCreate(ulong window, Text name, out ulong pages);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_validate")] internal static partial int RetainedPagesValidate(ulong pages, RetainedPageEntry* entries, uint count, ulong selected);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_set")] internal static partial int RetainedPagesSet(ulong pages, RetainedPageEntry* entries, uint count, ulong selected);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_validate_visible")] internal static partial int RetainedPagesValidateVisible(ulong pages, uint visible);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_set_visible")] internal static partial int RetainedPagesSetVisible(ulong pages, uint visible);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_get_state")] internal static partial int RetainedPagesGetState(ulong pages, out ulong selected, out uint visible, out uint count);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_insert")] internal static partial int RetainedPagesInsert(ulong pages, uint index, ulong id, ulong child);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_remove")] internal static partial int RetainedPagesRemove(ulong pages, ulong child);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_validate_move")] internal static partial int RetainedPagesValidateMove(ulong pages, ulong child, uint index);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_move")] internal static partial int RetainedPagesMove(ulong pages, ulong child, uint index);
    [LibraryImport("xui", EntryPoint = "xui_page_selector_connect")] internal static partial int PageSelectorConnect(ulong selector, ulong pages);
    [LibraryImport("xui", EntryPoint = "xui_page_selector_validate")] internal static partial int PageSelectorValidate(ulong selector, RetainedPageEntry* entries, uint count, ulong selected);
    [LibraryImport("xui", EntryPoint = "xui_page_selector_set")] internal static partial int PageSelectorSet(ulong selector, RetainedPageEntry* entries, uint count, ulong selected);
    [LibraryImport("xui", EntryPoint = "xui_page_selector_set_closable")] internal static partial int PageSelectorSetClosable(ulong selector, uint value);
    [LibraryImport("xui", EntryPoint = "xui_page_selector_get_closable")] internal static partial int PageSelectorGetClosable(ulong selector, out uint value);
    [LibraryImport("xui", EntryPoint = "xui_retained_pages_get_page_id")] internal static partial int RetainedPagesGetPageId(ulong pages, uint index, out ulong id);
    [LibraryImport("xui", EntryPoint = "xui_page_selector_get_state")] internal static partial int PageSelectorGetState(ulong selector, out ulong selected, out uint count);
}

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

/// <summary>A navigation entry with an optional vector icon and asynchronous Shell image path.</summary>
public readonly record struct NavigationEntry(ulong Id, string Label, ulong Parent = 0,
    string Keywords = "", bool Selectable = true, bool Expanded = true, bool Enabled = true,
    ButtonIcon Icon = ButtonIcon.None, string ImagePath = "");
public readonly record struct UiKeyEvent(uint VirtualKey, KeyModifiers Modifiers, ulong TargetId);
public enum NavigationDirection : uint { Back, Forward }
public readonly record struct NavigationPoint(float X, float Y);
/// <summary>Back/Forward input with a window-local pointer position or source control center, when available.</summary>
public readonly record struct UiNavigationEvent(NavigationDirection Direction, ulong TargetId, NavigationPoint? Position);
public enum GridNavigation : uint { Previous, Next, PagePrevious, PageNext, First, Last }
public enum ButtonIcon : uint
{
    None, Back, Forward, Up, Refresh, Split, Theme, Add, Minimize, Maximize, Restore, Close, More,
    Navigation, Home, Folder, Settings, Search, Library, History, Bookmark, Drive
}

public sealed unsafe partial class NavigationView
{
    private TextInput? search;
    public TextInput Search => search ??= new(Window, Features.Child(this, 0));
    public NavigationView Select(ulong id) { Features.Action(this, 1, id); return this; }
    public NavigationView SetItems(ReadOnlySpan<NavigationEntry> items)
    {
        Window.Guard();
        if (items.Length > 4096) throw new ArgumentOutOfRangeException(nameof(items));
        using var pins = new Window.Pins();
        var entries = new Native.NavigationEntry[items.Length];
        var visuals = new Native.ItemVisual[items.Length];
        for (int i = 0; i < items.Length; ++i)
        {
            var item = items[i];
            entries[i] = new() { Size = (uint)sizeof(Native.NavigationEntry), Id = item.Id, Parent = item.Parent,
                Label = pins.Text(item.Label), Keywords = pins.Text(item.Keywords),
                Flags = (item.Enabled ? 0u : 1u) | (item.Selectable ? 0u : 2u) | (item.Expanded ? 0u : 4u) };
            visuals[i] = new() { Size = (uint)sizeof(Native.ItemVisual), Icon = (uint)item.Icon, ImagePath = pins.Text(item.ImagePath) };
        }
        fixed (Native.NavigationEntry* p = entries)
        fixed (Native.ItemVisual* v = visuals) Window.Check(Native.NavigationItemsVisual(Handle, p, v, (uint)entries.Length));
        return this;
    }
}
public sealed partial class TabStrip
{
    public bool Visible { get => Features.Get(this, 37).First != 0; set => Features.Set(this, 37, first: value ? 1u : 0u); }
}
public sealed partial class Popup { public bool IsOpen => Features.Get(this, 43).First != 0; }
public sealed partial class SplitView
{
    /// <summary>True when the secondary pane fits and SecondVisible is true.</summary>
    /// <remarks>View events report changes after layout. Value is 1 for expanded or 0 for collapsed.</remarks>
    public bool Expanded => Features.Get(this, 5).First != 0;
}
public sealed partial class ItemsView
{
    public ItemsView Step(int delta) { Features.Action(this, 16, unchecked((uint)delta)); return this; }
}
public sealed partial class DataGrid
{
    /// <summary>Moves row selection without moving input focus. Supports Control and Shift selection gestures.</summary>
    public DataGrid Navigate(GridNavigation direction, KeyModifiers modifiers = KeyModifiers.None)
    { Features.Action(this, 17, (uint)direction, (uint)modifiers); return this; }
}
public sealed unsafe partial class Window
{
    private TabStrip? titlebarTabs, titlebarSecondaryTabs;
    private Button? titlebarLeading;
    private GCHandle keyRoot;
    private Func<UiKeyEvent, bool>? keyHandler;
    private GCHandle navigationRoot;
    private Func<UiNavigationEvent, bool>? navigationHandler;
    public TabStrip TitlebarTabs => titlebarTabs ??= new(this, TitlebarChild(0));
    public TabStrip TitlebarSecondaryTabs => titlebarSecondaryTabs ??= new(this, TitlebarChild(2));
    public Button TitlebarLeading
    {
        get
        {
            Guard();
            if (titlebarLeading is null) { titlebarLeading = new(this, TitlebarChild(1)); titlebarLeading.Visible(true); }
            return titlebarLeading;
        }
    }
    private ulong TitlebarChild(uint index) { Guard(); ulong handle; Check(Native.FeatureChild(Handle, index, &handle)); return handle; }
    public Window SetTitle(string title)
    {
        Guard(); using var pins = new Pins(); Check(Native.WindowTitle(Handle, pins.Text(title))); return this;
    }
    public Func<UiKeyEvent, bool>? KeyHandler
    {
        get { Guard(); return keyHandler; }
        set
        {
            Guard();
            if (!keyRoot.IsAllocated) keyRoot = GCHandle.Alloc(this, GCHandleType.Weak);
            Check(Native.WindowKeyHandler(Handle, value is null ? null : &KeyTrampoline, GCHandle.ToIntPtr(keyRoot)));
            keyHandler = value;
        }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int KeyTrampoline(nint context, Native.KeyEvent* e, uint* handled)
    {
        Window? window = null;
        try
        {
            window = GCHandle.FromIntPtr(context).Target as Window;
            if (window is null) return 8;
            ++window.callbacks;
            try { *handled = window.keyHandler?.Invoke(new(e->VirtualKey, (KeyModifiers)e->Modifiers, e->Target)) == true ? 1u : 0u; }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error) { if (window is not null) window.callbackError = error; return 8; }
    }
    /// <summary>Handles mouse Back/Forward and browser application commands. Return true to consume the event.</summary>
    public Func<UiNavigationEvent, bool>? NavigationHandler
    {
        get { Guard(); return navigationHandler; }
        set
        {
            Guard();
            if (!navigationRoot.IsAllocated) navigationRoot = GCHandle.Alloc(this, GCHandleType.Weak);
            Check(Native.WindowNavigationHandler(Handle, value is null ? null : &NavigationTrampoline, GCHandle.ToIntPtr(navigationRoot)));
            navigationHandler = value;
        }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int NavigationTrampoline(nint context, Native.NavigationEvent* e, uint* handled)
    {
        Window? window = null;
        try
        {
            window = GCHandle.FromIntPtr(context).Target as Window;
            if (window is null) return 8;
            ++window.callbacks;
            try
            {
                *handled = window.navigationHandler?.Invoke(new((NavigationDirection)e->Direction, e->Target,
                    e->HasPosition != 0 ? new NavigationPoint(e->X, e->Y) : null)) == true ? 1u : 0u;
            }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error) { if (window is not null) window.callbackError = error; return 8; }
    }
    private sealed record PostedAction(Window Window, Action Action);
    /// <summary>Queues an action on the UI thread. Worker threads can call this method.</summary>
    /// <returns>False after close or disposal. Close discards queued actions without running them.</returns>
    /// <remarks>Action exceptions close the window. Run then throws XuiException with the original exception.</remarks>
    public bool Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        var handle = Handle;
        if (handle == 0) return false;
        var root = GCHandle.Alloc(new PostedAction(this, action));
        int status = Native.WindowPost(handle, &PostTrampoline, GCHandle.ToIntPtr(root));
        if (status == 0) return true;
        root.Free();
        if (status is 2 or 11) return false;
        Check(status); return false;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PostTrampoline(nint context, uint execute)
    {
        var root = GCHandle.FromIntPtr(context);
        PostedAction? posted = null;
        try
        {
            posted = root.Target as PostedAction;
            if (posted is null) return 8;
            if (execute != 0)
            {
                ++posted.Window.callbacks;
                try { posted.Action(); }
                finally { --posted.Window.callbacks; }
            }
            return 0;
        }
        catch (Exception error) { if (posted is not null) posted.Window.callbackError = error; return 8; }
        finally { root.Free(); }
    }
}

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

/// <summary>A navigation entry with an optional vector icon and asynchronous Shell image path.</summary>
public readonly record struct NavigationEntry(ulong Id, string Label, ulong Parent = 0,
    string Keywords = "", bool Selectable = true, bool Expanded = true, bool Enabled = true,
    ButtonIcon Icon = ButtonIcon.None, string ImagePath = "");
public readonly record struct UiKeyEvent(uint VirtualKey, KeyModifiers Modifiers, ulong TargetId)
{
    /// <summary>A text-producing key outside a native editor. Focus an editor and return false to route the original key there.</summary>
    public bool IsTextInput { get; init; }
}
public enum NavigationDirection : uint { Back, Forward }
public readonly record struct NavigationPoint(float X, float Y);
/// <summary>Back/Forward input with a window-local pointer position or source control center, when available.</summary>
public readonly record struct UiNavigationEvent(NavigationDirection Direction, ulong TargetId, NavigationPoint? Position);
public enum GridNavigation : uint { Previous, Next, PagePrevious, PageNext, First, Last }
public enum ButtonIcon : uint
{
    None, Back, Forward, Up, Refresh, Split, Theme, Add, Minimize, Maximize, Restore, Close, More,
    Navigation, Home, Folder, Settings, Search, Library, History, Bookmark, Drive, Open,
    Save = 23, SaveAs = 24, Undo = 25, Redo = 26, ChevronUp = 27, ChevronDown = 28,
    FoldersFirst = 29, FilesFirst = 30, Mixed = 31
}

public sealed unsafe partial class NavigationView
{
    private TextInput? search;
    private Button? toggleButton;
    private RetainedElement? items, headerItems, footerItems;
    private Label? title, emptyMessage;
    public TextInput Search => search ??= new(Window, Features.Child(this, 0));
    public Button ToggleButton => toggleButton ??= new(Window, Features.Child(this, 1));
    public RetainedElement Items => items ??= new(Window, Features.Child(this, 2));
    public RetainedElement HeaderItems => headerItems ??= new(Window, Features.Child(this, 3));
    public RetainedElement FooterItems => footerItems ??= new(Window, Features.Child(this, 4));
    public Label Title => title ??= new(Window, Features.Child(this, 5));
    public Label EmptyMessage => emptyMessage ??= new(Window, Features.Child(this, 6));
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
    private RetainedElement? titlebar;
    private Label? titlebarTitle;
    private Button? titlebarMinimize, titlebarMaximize, titlebarClose;
    public RetainedElement Titlebar => titlebar ??= new(this, TitlebarChild(3));
    public Label TitlebarTitle => titlebarTitle ??= new(this, TitlebarChild(4));
    public Button TitlebarMinimize => titlebarMinimize ??= new(this, TitlebarChild(5));
    public Button TitlebarMaximize => titlebarMaximize ??= new(this, TitlebarChild(6));
    public Button TitlebarClose => titlebarClose ??= new(this, TitlebarChild(7));
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
            GuardWindowCallback();
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
            try
            {
                *handled = window.keyHandler?.Invoke(new(e->VirtualKey, (KeyModifiers)e->Modifiers, e->Target)
                    { IsTextInput = (e->Reserved & 1) != 0 }) == true ? 1u : 0u;
            }
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
            GuardWindowCallback();
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
    internal sealed class PostedAction(Window window, Action action, ContentUpdate? scope)
    {
        internal readonly Window Window = window;
        internal Action? Action = action;
        internal readonly ContentUpdate? Scope = scope;
    }
    /// <summary>Queues an action on the UI thread. Worker threads can call this method.</summary>
    /// <returns>False after close or disposal. Close discards queued actions without running them.</returns>
    /// <remarks>Action exceptions close the window. Run then throws XuiException with the original exception.</remarks>
    public bool Post(Action action) => PostCore(action, contentContext.Value);
    internal bool PostUnscoped(Action action) => PostCore(action, null);
    private bool PostCore(Action action, ContentUpdate? scope)
    {
        ArgumentNullException.ThrowIfNull(action);
        var handle = Handle;
        if (handle == 0) return false;
        var posted = new PostedAction(this, action, scope);
        if (scope is not null && !scope.Track(posted)) return false;
        var root = GCHandle.Alloc(posted);
        int status = Native.WindowPost(handle, &PostTrampoline, GCHandle.ToIntPtr(root));
        if (status == 0) return true;
        scope?.Untrack(posted);
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
            if (execute != 0 && posted.Scope is not { AcceptCallbacks: false } && posted.Action is { } action)
            {
                using var content = posted.Window.EnterContent(posted.Scope);
                ++posted.Window.callbacks;
                try { action(); }
                finally { --posted.Window.callbacks; }
            }
            return 0;
        }
        catch (Exception error) { return posted is null ? 8 : posted.Window.ContentError(posted.Scope, error); }
        finally { posted?.Scope?.Untrack(posted); root.Free(); }
    }
}

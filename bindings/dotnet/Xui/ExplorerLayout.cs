using System.Runtime.InteropServices;

namespace Xui;

public readonly record struct ElementBounds(float X, float Y, float Width, float Height);
public enum PopupPlacement : uint { Below, Above, Right, Left, Center }

public sealed partial class Popup
{
    public Popup SetWindowBackground(bool enabled)
    {
        Window.Guard();
        Window.Check(Native.PopupWindowBackground(Handle, enabled ? 1u : 0u));
        return this;
    }

    public Popup SetPlacement(PopupPlacement placement)
    {
        Window.Guard();
        Window.Check(Native.PopupPlacement(Handle, (uint)placement));
        return this;
    }
}

public sealed partial class ItemsView
{
    /// <summary>Show secondary text as trailing shortcut keycaps. Separate keys with '+'.</summary>
    public ItemsView SetTrailingShortcutBadges(bool enabled)
    {
        Window.Guard();
        Window.Check(Native.ItemsTrailingShortcutBadges(Handle, enabled ? 1u : 0u));
        return this;
    }
}

public sealed unsafe partial class TextInput
{
    public TextInput SetCaptionVisible(bool visible)
    {
        Window.Guard();
        Window.Check(Native.TextInputCaption(Handle, visible ? 1u : 0u));
        return this;
    }

    public TextInput SetPlaceholder(string text)
    {
        Window.Guard();
        using var pins = new Window.Pins();
        Window.Check(Native.TextInputPlaceholder(Handle, pins.Text(text)));
        return this;
    }
}

public static class ElementLayout
{
    public static ElementBounds GetBounds(this Element element)
    {
        element.Window.Guard();
        element.Window.Check(Native.ElementBounds(element.Handle, out float x, out float y, out float width, out float height));
        return new(x, y, width, height);
    }
}

public sealed partial class Window
{
    public VisualStyle Style
    {
        get
        {
            Guard();
            Check(Native.WindowVisualStyleGet(Handle, out uint style));
            return (VisualStyle)style;
        }
    }

    public Window SetVisualStyle(VisualStyle style)
    {
        Guard();
        Check(Native.WindowVisualStyleSet(Handle, (uint)style));
        return this;
    }

    public Window SetTitlebarLayout(Element firstPane, Element? secondPane = null, bool showTitle = false)
    {
        Guard();
        firstPane.BelongsTo(this);
        secondPane?.BelongsTo(this);
        Check(Native.TitlebarLayout(Handle, firstPane.Handle, secondPane?.Handle ?? 0, showTitle ? 1u : 0u));
        return this;
    }
}

public sealed partial class NavigationView
{
    public NavigationView SetHeaderVisible(bool visible)
    {
        Window.Guard();
        Window.Check(Native.NavigationHeader(Handle, visible ? 1u : 0u));
        return this;
    }
}

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_window_visual_style_set")]
    internal static partial int WindowVisualStyleSet(ulong window, uint style);
    [LibraryImport("xui", EntryPoint = "xui_window_visual_style_get")]
    internal static partial int WindowVisualStyleGet(ulong window, out uint style);
    [LibraryImport("xui", EntryPoint = "xui_popup_window_background")]
    internal static partial int PopupWindowBackground(ulong target, uint enabled);
    [LibraryImport("xui", EntryPoint = "xui_items_trailing_shortcut_badges")]
    internal static partial int ItemsTrailingShortcutBadges(ulong target, uint enabled);
    [LibraryImport("xui", EntryPoint = "xui_popup_placement")]
    internal static partial int PopupPlacement(ulong target, uint placement);
    [LibraryImport("xui", EntryPoint = "xui_text_input_caption")]
    internal static partial int TextInputCaption(ulong target, uint visible);
    [LibraryImport("xui", EntryPoint = "xui_text_input_placeholder")]
    internal static partial int TextInputPlaceholder(ulong target, Text text);
    [LibraryImport("xui", EntryPoint = "xui_window_titlebar_layout")]
    internal static partial int TitlebarLayout(ulong window, ulong first, ulong second, uint showTitle);
    [LibraryImport("xui", EntryPoint = "xui_navigation_header")]
    internal static partial int NavigationHeader(ulong target, uint visible);
    [LibraryImport("xui", EntryPoint = "xui_element_bounds")]
    internal static partial int ElementBounds(ulong target, out float x, out float y, out float width, out float height);
}

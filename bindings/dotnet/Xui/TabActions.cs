using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class TabStrip
{
    private Button? newTabButton;
    public Button NewTabButton => newTabButton ??= new(Window, Features.Child(this, 0));

    /// <summary>Shows a native New tab button. Activation raises EventKind.Action.</summary>
    public bool NewTabButtonVisible
    {
        get
        {
            Window.Guard();
            Window.Check(Native.TabGetNewButton(Handle, out var visible));
            return visible != 0;
        }
        set => SetNewTabButtonVisible(value);
    }

    public TabStrip SetNewTabButtonVisible(bool visible)
    {
        Window.Guard();
        Window.Check(Native.TabSetNewButton(Handle, visible ? 1u : 0u));
        return this;
    }
}

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_tab_set_new_button")]
    internal static partial int TabSetNewButton(ulong target, uint visible);
    [LibraryImport("xui", EntryPoint = "xui_tab_get_new_button")]
    internal static partial int TabGetNewButton(ulong target, out uint visible);
}

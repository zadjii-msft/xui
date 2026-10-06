using System.Runtime.InteropServices;
using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static async Task Panes(Window window, DesignerLayout layout, DesignerHierarchyLayout hierarchy,
        DesignerInspectorLayout inspector, MultilineText editor, MultilineText value, TreeView tree, Control preview, Popup popup, TextInput popupField,
        Func<Action, Task> ui, Action<bool, string> require)
    {
        nint sourceHandle = 0, valueHandle = 0, treeHandle = 0;
        ElementBounds docked = default, collapsed = default;
        foreach (var style in new[] { VisualStyle.WinUI, VisualStyle.Classic })
        foreach (var theme in new[] { Theme.Light, Theme.Dark, Theme.HighContrast })
        {
            await ui(() =>
            {
                window.SetVisualStyle(style);
                window.SetTheme(theme);
                require(layout.HierarchyPinned && layout.InspectorPinned, "Both side panes start pinned.");
                require(layout.HierarchyPin.Icon == ButtonIcon.PinFilled && layout.InspectorPin.Icon == ButtonIcon.PinFilled,
                    "Pinned pane headers use filled pushpins.");
                value.Focus(); valueHandle = GetFocus();
                tree.Focus(); treeHandle = GetFocus();
                editor.Focus(); sourceHandle = GetFocus();
                editor.Selection = new(2, 6);
                editor.ReplaceRange(new(0, 0), editor.Text, "Pane ");
                editor.Selection = new(2, 6);
                value.Text = "Unapplied property draft";
                value.Selection = new(1, 7);
                hierarchy.Query.Text = "retained hierarchy query";
                inspector.ArgumentFilter.Text = "retained property query";
                docked = layout.SourcePreview.GetBounds();
                layout.HierarchyPin.Invoke();
                layout.InspectorPin.Invoke();
            });
            await Settle();
            await ui(() =>
            {
                collapsed = layout.SourcePreview.GetBounds();
                require(!layout.HierarchyPinned && !layout.InspectorPinned &&
                    !layout.HierarchyDock.NavigationOpen && !layout.InspectorDock.NavigationOpen,
                    "Unpinning collapses both panes without leaving either overlay open.");
                require(collapsed.Width >= docked.Width + 400 && collapsed.X < docked.X,
                    "Auto-hidden panes release their docked workspace width.");
                require(!IsWindowVisible(treeHandle) && !IsWindowVisible(valueHandle) &&
                    layout.HierarchyTab.GetBounds().Width == 32 && layout.InspectorTab.GetBounds().Width == 32,
                    "Collapsed native children are hidden and accessible edge tabs remain.");
                require(layout.HierarchyTab.Text == "Hierarchy" && layout.InspectorTab.Text == "Properties" &&
                    layout.HierarchyTab.VerticalText && layout.InspectorTab.VerticalText &&
                    layout.HierarchyTab.Icon == ButtonIcon.None && layout.InspectorTab.Icon == ButtonIcon.None &&
                    layout.HierarchyTab.GetBounds().Height == 112 && layout.InspectorTab.GetBounds().Height == 120,
                    "Collapsed panes have full accessible names and tall sideways text tabs, not icon-only buttons.");
                require(layout.HierarchyPin.Icon == ButtonIcon.Pin && layout.InspectorPin.Icon == ButtonIcon.Pin,
                    "Auto-hidden pane headers use outlined pushpins.");
                layout.HierarchyTab.Invoke();
            });
            await Settle();
            await ui(() =>
            {
                require(layout.HierarchyDock.NavigationOpen && IsWindowVisible(treeHandle),
                    "The hierarchy edge tab manually opens its retained subtree.");
                require(layout.SourcePreview.GetBounds() == collapsed &&
                    layout.HierarchyPanel.GetBounds().X == collapsed.X &&
                    layout.HierarchyPanel.GetBounds().Width == 220,
                    "The left overlay covers content without resizing the workspace.");
                var region = CreateRectRgn(0, 0, 0, 0);
                if (region == 0) throw new System.ComponentModel.Win32Exception();
                try
                {
                    require(GetWindowRgn(sourceHandle, region) != 0 && !PtInRegion(region, 1, 20),
                        "The native source HWND excludes the pixels beneath the hierarchy overlay.");
                }
                finally { DeleteObject(region); }
                hierarchy.Query.Focus();
                require(layout.HierarchyDock.NavigationOpen, "Focus on a native descendant keeps the overlay open.");
                tree.Focus();
                require(GetFocus() == treeHandle && layout.HierarchyDock.NavigationOpen,
                    "Moving focus within the hierarchy retains its native tree and overlay.");
                layout.InspectorTab.Invoke();
            });
            await Settle();
            await ui(() =>
            {
                require(!layout.HierarchyDock.NavigationOpen && layout.InspectorDock.NavigationOpen,
                    "Focusing the other pane closes only the previous unpinned overlay.");
                var panel = layout.InspectorPanel.GetBounds();
                require(panel.X + panel.Width == collapsed.X + collapsed.Width && panel.Width == 284 &&
                    layout.SourcePreview.GetBounds() == collapsed,
                    "Properties overlays the right edge without changing source or preview geometry.");
                value.Focus();
                require(GetFocus() == valueHandle && value.Text == "Unapplied property draft" &&
                    value.Selection == new TextSelection(1, 7), "Opening retains native property text, selection and identity.");
                inspector.ArgumentFilter.Focus();
                popup.Show(inspector.ArgumentFilter);
                popupField.Focus();
                require(layout.InspectorDock.NavigationOpen, "A popup anchored inside the pane belongs to its focus subtree.");
                popup.Dismiss();
                require(layout.InspectorDock.NavigationOpen,
                    "Dismissing a child popup retains its pane.");
                value.Focus();
                require(layout.InspectorDock.NavigationOpen, "Returning from a child popup keeps the pane open.");
                editor.Focus();
            });
            await Settle();
            await ui(() =>
            {
                require(!layout.InspectorDock.NavigationOpen && !IsWindowVisible(valueHandle) &&
                    GetFocus() == sourceHandle, "Focusing outside auto-hides properties without stealing destination focus.");
                require(editor.Selection == new TextSelection(2, 6) &&
                    editor.Text == "Pane Native editor layout fixture", "Overlay transitions retain source selection and text.");
                layout.RevealInspector();
                value.Focus();
                if (!PostMessage(GetFocus(), 0x100, 0x1B, 0)) throw new System.ComponentModel.Win32Exception();
            });
            await Settle();
            await ui(() =>
            {
                require(!layout.InspectorDock.NavigationOpen && GetFocus() != valueHandle &&
                    IsWindowVisible(GetFocus()), "Escape from a native property editor closes the overlay and leaves usable focus.");
                layout.RevealInspector();
                value.Focus();
                preview.Focus();
            });
            await Settle();
            await ui(() =>
            {
                require(!layout.InspectorDock.NavigationOpen && preview.Focused,
                    "Focusing a custom preview control closes the overlay without stealing focus.");
                layout.RevealInspector();
                value.Focus();
                require(layout.InspectorDock.NavigationOpen && GetFocus() == valueHandle,
                    "Explicit focus navigation can reveal a hidden property pane.");
                layout.InspectorPin.Invoke();
                layout.RevealHierarchy();
                tree.Focus();
                layout.HierarchyPin.Invoke();
            });
            await Settle();
            await ui(() =>
            {
                editor.Focus();
                require(layout.HierarchyPinned && layout.InspectorPinned &&
                    IsWindowVisible(treeHandle) && IsWindowVisible(valueHandle),
                    "Pinning either overlay docks it and outside focus no longer hides it.");
                require(layout.HierarchyPin.Icon == ButtonIcon.PinFilled && layout.InspectorPin.Icon == ButtonIcon.PinFilled,
                    "Repinning restores filled pushpins on the retained buttons.");
                require(layout.SourcePreview.GetBounds() == docked, "Repinning restores the original workspace geometry.");
                require(hierarchy.Query.Text == "retained hierarchy query" &&
                    inspector.ArgumentFilter.Text == "retained property query" && value.Text == "Unapplied property draft",
                    "Pin transitions preserve search queries and unapplied property drafts.");
                editor.Command(TextCommand.Undo);
                require(editor.Text == "Native editor layout fixture",
                    "Pane transitions preserve native source undo.");
            });
        }
        async Task Settle() { await ui(() => { }); await Task.Delay(100); }
    }

    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(nint hwnd);
    [DllImport("user32.dll", EntryPoint = "PostMessageW", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(nint hwnd, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll")] private static extern int GetWindowRgn(nint hwnd, nint region);
    [DllImport("gdi32.dll")] private static extern nint CreateRectRgn(int left, int top, int right, int bottom);
    [DllImport("gdi32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PtInRegion(nint region, int x, int y);
    [DllImport("gdi32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteObject(nint value);
}

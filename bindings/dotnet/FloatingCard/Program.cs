using System.ComponentModel;
using System.Runtime.InteropServices;
using Xui;

internal static partial class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args is not ([] or ["--smoke"]))
                throw new ArgumentException("Usage: FloatingCard [--smoke]");

            using var app = new Xui.Application();
            using var window = app.CreateWindow("Floating XUI card", 548, 260, Theme.Dark,
                visualStyle: VisualStyle.WinUI, transparent: true);
            uint dpi = GetDpiForSystem();
            int width = (int)(548 * dpi / 96), height = (int)(260 * dpi / 96);
            window.Placement = new((GetSystemMetrics(0) - width) / 2, (GetSystemMetrics(1) - height) / 2,
                width, height, false);
            window.SetDragRegion(26, 26, 490, 19);

            nint configuredHwnd = 0;
            window.NativeHwndCreated = hwnd =>
            {
                configuredHwnd = hwnd;
                if (!SetWindowPos(hwnd, new nint(-1), 0, 0, 0, 0,
                    0x0001 | 0x0002 | 0x0010))
                    throw new Win32Exception(Marshal.GetLastPInvokeError());
            };

            var root = window.Stack().Padding(22);
            var outline = window.Stack().Padding(5).FixedSize(504, 214);
            outline.SetControlStyleValues(StylePart.Root, new PartStyleValues
            {
                Background = new ThemeColor(0x111827),
                CornerRadius = 22
            });
            var card = window.Stack().Padding(18).Spacing(10).FixedSize(494, 204);
            card.SetControlStyleValues(StylePart.Root, new PartStyleValues
            {
                Background = new ThemeColor(0x26364c),
                BorderBrush = new ThemeColor(0x8193a9),
                BorderThickness = new Insets(1),
                CornerRadius = 17
            });
            var heading = window.Label("Floating XUI card").SetAutomationId("card-heading");
            var detail = window.Label("Desktop visible beyond the rounded card. Drag its top edge.");
            var input = window.TextInput("Try native input").SetAutomationId("card-input");
            var close = window.Button("Close").SetAutomationId("card-close");
            close.Click += window.Close;
            card.Add(heading).Add(detail).Add(input).Add(close);
            outline.Add(card);
            root.Add(outline);
            window.SetContent(root);

            app.Show(window);
            if (window.NativeHwnd != configuredHwnd || configuredHwnd == 0)
                throw new InvalidOperationException("The borrowed HWND changed during creation.");
            if (args is ["--smoke"])
            {
                app.Post(() =>
                {
                    try
                    {
                        if (!GetWindowRect(window.NativeHwnd, out var bounds))
                            throw new Win32Exception(Marshal.GetLastPInvokeError());
                        var transparent = WindowFromPoint(new(bounds.Left + 2, bounds.Top + 2));
                        var contrast = new HighContrast { Size = (uint)Marshal.SizeOf<HighContrast>() };
                        if (!SystemParametersInfo(0x0042, contrast.Size, ref contrast, 0))
                            throw new Win32Exception(Marshal.GetLastPInvokeError());
                        bool solidFallback = (contrast.Flags & 1) != 0;
                        if (!solidFallback && (transparent == window.NativeHwnd || IsChild(window.NativeHwnd, transparent)))
                            throw new InvalidOperationException("The empty corner did not pass pointer input through.");
                        var visible = WindowFromPoint(new(bounds.Left + 120, bounds.Top + 85));
                        if (visible != window.NativeHwnd && !IsChild(window.NativeHwnd, visible))
                            throw new InvalidOperationException("The painted card is not hit-testable.");
                        int dragX = bounds.Left + 100, dragY = bounds.Top + (int)(35 * dpi / 96);
                        var dragTarget = WindowFromPoint(new(dragX, dragY));
                        if (SendMessage(dragTarget, 0x0084, 0, (dragY << 16) | (dragX & 0xffff)) != 2)
                            throw new InvalidOperationException("The card header is not draggable.");
                        input.Focus();
                        var editor = GetFocus();
                        if (editor == 0 || !IsChild(window.NativeHwnd, editor))
                            throw new InvalidOperationException("The native editor did not receive focus.");
                        SendMessageText(editor, 0x000C, 0, "Native text input works");
                        if (input.Text != "Native text input works")
                            throw new InvalidOperationException("The native editor did not deliver its text.");
                        Console.WriteLine(solidFallback
                            ? "Floating card: high-contrast solid fallback, drag, native input, and borrowed HWND passed"
                            : "Floating card: alpha, drag, native input, and borrowed HWND passed");
                    }
                    finally { app.Shutdown(); }
                });
            }
            app.Run();
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct Point(int X, int Y);
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    private struct HighContrast { public uint Size, Flags; public nint DefaultScheme; }

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetWindowPos(nint hwnd, nint insertAfter, int x, int y, int width, int height, uint flags);
    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetWindowRect(nint hwnd, out Rect rect);
    [LibraryImport("user32.dll")]
    private static partial nint WindowFromPoint(Point point);
    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool IsChild(nint parent, nint child);
    [LibraryImport("user32.dll")]
    private static partial int GetSystemMetrics(int index);
    [LibraryImport("user32.dll")]
    private static partial uint GetDpiForSystem();
    [LibraryImport("user32.dll")]
    private static partial nint GetFocus();
    [LibraryImport("user32.dll", EntryPoint = "SendMessageW")]
    private static partial nint SendMessage(nint hwnd, uint message, nint wparam, nint lparam);
    [LibraryImport("user32.dll", EntryPoint = "SendMessageW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint SendMessageText(nint hwnd, uint message, nint wparam, string lparam);
    [LibraryImport("user32.dll", EntryPoint = "SystemParametersInfoW", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SystemParametersInfo(uint action, uint value, ref HighContrast result, uint update);
}

using System.Runtime.InteropServices;
using Xui;

internal static class WindowIconTests
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint FindWindowW(string? className, string title);
    [DllImport("user32.dll")]
    private static extern nint SendMessageW(nint hwnd, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll")]
    private static extern bool GetIconInfo(nint icon, out IconInfo info);
    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(nint hwnd);
    [DllImport("user32.dll")]
    private static extern int GetSystemMetricsForDpi(int index, uint dpi);
    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(nint hwnd, out Rect rect);
    [DllImport("gdi32.dll")]
    private static extern int GetObjectW(nint value, int size, out Bitmap bitmap);
    [DllImport("gdi32.dll")]
    private static extern bool DeleteObject(nint value);
    [StructLayout(LayoutKind.Sequential)]
    private struct IconInfo { public int IsIcon; public uint X, Y; public nint Mask, Color; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Bitmap { public int Type, Width, Height, Stride; public ushort Planes, BitsPerPixel; public nint Bits; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }

    private static void Require(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
    private static bool Inspect(nint icon, int? expected = null)
    {
        if (!GetIconInfo(icon, out var info)) return false;
        try
        {
            Require(info.IsIcon != 0 && info.Color != 0, "HWND icon must contain a color bitmap");
            Require(GetObjectW(info.Color, Marshal.SizeOf<Bitmap>(), out var bitmap) != 0, "Read icon bitmap");
            if (expected is { } size)
                Require(bitmap.Width == size && bitmap.Height == size, "Icon dimensions must follow window DPI");
            return true;
        }
        finally
        {
            if (info.Mask != 0) DeleteObject(info.Mask);
            if (info.Color != 0) DeleteObject(info.Color);
        }
    }
    internal static void Run()
    {
        using var application = new Application();
        using var window = application.CreateWindow("HWND file-type icon tests", 500, 300, customTitlebar: true);
        window.SetContent(window.Stack().Add(window.Label("Native document icon")));
        window.SetFileTypeIcon(".txt");
        application.Show(window);
        try
        {
            var hwnd = FindWindowW("Xui.Window.1", "HWND file-type icon tests");
            Require(hwnd != 0, "Icon test window must exist");
            var dpi = GetDpiForWindow(hwnd);
            nint small = SendMessageW(hwnd, 0x7f, 0, 0), large = SendMessageW(hwnd, 0x7f, 1, 0);
            Require(Inspect(small, GetSystemMetricsForDpi(49, dpi))
                && Inspect(large, GetSystemMetricsForDpi(11, dpi)), "Both HWND icons must be populated");
            foreach (var invalid in new[] { "txt", @"C:\private\file.txt", ".a/b", ".a\0b", new string('.', 256) })
            {
                try { window.SetFileTypeIcon(invalid); throw new InvalidOperationException("Invalid icon extension was accepted"); }
                catch (XuiException error) { Require(error.Status == 1, "Invalid extension has an argument error"); }
                catch (ArgumentException) when (invalid.Contains('\0')) { }
                Require(SendMessageW(hwnd, 0x7f, 0, 0) == small, "Invalid input must preserve the current icon");
            }
            for (int i = 0; i < 16; ++i)
            {
                window.SetFileTypeIcon(directory: i % 2 == 0);
                Require(!Inspect(small) && !Inspect(large), "Replacement must release both prior HICONs");
                small = SendMessageW(hwnd, 0x7f, 0, 0); large = SendMessageW(hwnd, 0x7f, 1, 0);
                Require(Inspect(small) && Inspect(large), "Stock file/folder replacement must supply real icons");
            }
            Require(GetWindowRect(hwnd, out var bounds), "Read icon window bounds");
            nint suggested = Marshal.AllocHGlobal(Marshal.SizeOf<Rect>());
            try
            {
                Marshal.StructureToPtr(bounds, suggested, false);
                SendMessageW(hwnd, 0x02e0, (144u << 16) | 144u, suggested);
            }
            finally { Marshal.FreeHGlobal(suggested); }
            Require(!Inspect(small) && !Inspect(large), "DPI changes retire old icon sizes");
            small = SendMessageW(hwnd, 0x7f, 0, 0); large = SendMessageW(hwnd, 0x7f, 1, 0);
            Require(Inspect(small, GetSystemMetricsForDpi(49, 144))
                && Inspect(large, GetSystemMetricsForDpi(11, 144)), "DPI changes update both HWND icons");
            window.Close();
            application.Run();
            Require(!Inspect(small) && !Inspect(large), "Window closure must release icons before managed disposal");
            Console.WriteLine("HWND icons: type/stock, native bitmap sizes, invalid input, replacement, DPI and closure passed");
        }
        finally
        {
            if (window.State is WindowState.Open or WindowState.Closing)
            {
                application.Shutdown();
                application.Run();
            }
        }
    }
}

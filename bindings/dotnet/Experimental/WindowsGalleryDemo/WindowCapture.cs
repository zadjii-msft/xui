using System.Buffers.Binary;
using System.ComponentModel;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text;

internal static class WindowCapture
{
    internal readonly record struct Snapshot(int Width, int Height, byte[] Pixels);

    internal static void Save(nint window, string path)
    {
        var capture = Read(window);
        WritePng(path, capture.Width, capture.Height, capture.Pixels);
    }

    internal static Snapshot Read(nint window)
    {
        SendMessage(window, 0x001f, 0, 0); // WM_CANCELMODE dismisses transient tooltips without moving the user's cursor.
        UpdateWindow(window);
        if (!GetClientRect(window, out var rect)) throw new Win32Exception(Marshal.GetLastWin32Error());
        int width = rect.Right, height = rect.Bottom;
        if (width <= 0 || height <= 0 || (long)width * height > 16_777_216)
            throw new InvalidOperationException("The owned window has invalid capture dimensions.");
        nint dc = GetDC(window);
        if (dc == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
        nint memory = 0, bitmap = 0, previous = 0;
        try
        {
            memory = CreateCompatibleDC(dc);
            bitmap = CreateCompatibleBitmap(dc, width, height);
            if (memory == 0 || bitmap == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
            previous = SelectObject(memory, bitmap);
            if (previous == 0 || previous == -1) throw new Win32Exception(Marshal.GetLastWin32Error());
            // Print only the owned client surface, never the desktop or an overlapping window.
            if (!PrintWindow(window, memory, 3)) throw new Win32Exception(Marshal.GetLastWin32Error());
            SelectObject(memory, previous);
            previous = 0;
            var info = new BitmapInfo
            {
                Size = (uint)Marshal.SizeOf<BitmapInfo>(), Width = width, Height = -height, Planes = 1, BitCount = 32
            };
            var pixels = new byte[checked(width * height * 4)];
            if (GetDIBits(dc, bitmap, 0, (uint)height, pixels, ref info, 0) != height)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return new(width, height, pixels);
        }
        finally
        {
            if (previous != 0) SelectObject(memory, previous);
            if (bitmap != 0) DeleteObject(bitmap);
            if (memory != 0) DeleteDC(memory);
            ReleaseDC(window, dc);
        }
    }

    private static void WritePng(string path, int width, int height, byte[] pixels)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var output = File.Create(path);
        output.Write([137, 80, 78, 71, 13, 10, 26, 10]);
        Span<byte> header = stackalloc byte[13];
        header.Clear();
        BinaryPrimitives.WriteInt32BigEndian(header, width);
        BinaryPrimitives.WriteInt32BigEndian(header[4..], height);
        header[8] = 8;
        header[9] = 2;
        Chunk(output, "IHDR", header);
        using var compressed = new MemoryStream();
        using (var zlib = new ZLibStream(compressed, CompressionLevel.Optimal, leaveOpen: true))
        {
            var row = new byte[checked(width * 3 + 1)];
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    int source = (y * width + x) * 4, target = x * 3 + 1;
                    row[target] = pixels[source + 2];
                    row[target + 1] = pixels[source + 1];
                    row[target + 2] = pixels[source];
                }
                zlib.Write(row);
            }
        }
        Chunk(output, "IDAT", compressed.ToArray());
        Chunk(output, "IEND", []);
    }

    private static void Chunk(Stream output, string name, ReadOnlySpan<byte> data)
    {
        Span<byte> size = stackalloc byte[4];
        BinaryPrimitives.WriteInt32BigEndian(size, data.Length);
        output.Write(size);
        byte[] type = Encoding.ASCII.GetBytes(name);
        output.Write(type);
        output.Write(data);
        uint crc = uint.MaxValue;
        foreach (byte value in type) Update(value);
        foreach (byte value in data) Update(value);
        BinaryPrimitives.WriteUInt32BigEndian(size, ~crc);
        output.Write(size);
        void Update(byte value)
        {
            crc ^= value;
            for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ ((crc & 1) == 0 ? 0u : 0xedb88320u);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    private struct BitmapInfo
    {
        public uint Size;
        public int Width, Height;
        public ushort Planes, BitCount;
        public uint Compression, SizeImage;
        public int XPelsPerMeter, YPelsPerMeter;
        public uint ClrUsed, ClrImportant;
    }
    [DllImport("user32.dll", SetLastError = true)] private static extern bool GetClientRect(nint window, out Rect rect);
    [DllImport("user32.dll", SetLastError = true)] private static extern nint GetDC(nint window);
    [DllImport("user32.dll")] private static extern int ReleaseDC(nint window, nint dc);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool PrintWindow(nint window, nint dc, uint flags);
    [DllImport("user32.dll", EntryPoint = "SendMessageW")] private static extern nint SendMessage(nint window, uint message, nuint first, nint second);
    [DllImport("user32.dll")] private static extern bool UpdateWindow(nint window);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern nint CreateCompatibleDC(nint dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern nint CreateCompatibleBitmap(nint dc, int width, int height);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern nint SelectObject(nint dc, nint value);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(nint value);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(nint dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern int GetDIBits(nint dc, nint bitmap, uint start, uint lines,
        [Out] byte[] pixels, ref BitmapInfo info, uint usage);
}

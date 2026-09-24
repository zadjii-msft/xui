using System.Buffers.Binary;
using System.IO.Compression;

internal static class Fixtures
{
    internal static byte[] ValidPng()
    {
        using var output = new MemoryStream();
        output.Write([137, 80, 78, 71, 13, 10, 26, 10]);
        byte[] header = [0, 0, 0, 3, 0, 0, 0, 2, 8, 6, 0, 0, 0];
        Chunk(output, "IHDR", header);
        using var data = new MemoryStream();
        using (var zlib = new ZLibStream(data, CompressionLevel.SmallestSize, leaveOpen: true))
            zlib.Write([0, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
                0, 255, 255, 255, 255, 0, 0, 0, 255, 128, 64, 32, 255]);
        Chunk(output, "IDAT", data.ToArray());
        Chunk(output, "IEND", []);
        return output.ToArray();
    }

    internal static byte[] Png(uint width = 3, uint height = 2, string? ancillary = null, byte[]? ancillaryData = null)
    {
        using var stream = new MemoryStream();
        stream.Write([137, 80, 78, 71, 13, 10, 26, 10]);
        var header = new byte[13];
        BinaryPrimitives.WriteUInt32BigEndian(header, width);
        BinaryPrimitives.WriteUInt32BigEndian(header.AsSpan(4), height);
        header[8] = 8; header[9] = 6;
        Chunk(stream, "IHDR", header);
        if (ancillary is not null) Chunk(stream, ancillary, ancillaryData ?? []);
        // Header fixtures deliberately do not claim to be decoder-valid images.
        Chunk(stream, "IDAT", [0]);
        Chunk(stream, "IEND", []);
        return stream.ToArray();
    }

    internal static void Chunk(Stream stream, string type, byte[] payload)
    {
        byte[] length = new byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(length, (uint)payload.Length);
        stream.Write(length);
        byte[] bytes = [.. System.Text.Encoding.ASCII.GetBytes(type), .. payload];
        stream.Write(bytes);
        uint crc = uint.MaxValue;
        foreach (byte value in bytes)
        {
            crc ^= value;
            for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320u : 0);
        }
        BinaryPrimitives.WriteUInt32BigEndian(length, ~crc);
        stream.Write(length);
    }

    internal static byte[] Jpeg(ushort width = 3, ushort height = 2, byte frame = 0xc0, byte[]? exif = null)
    {
        using var stream = new MemoryStream();
        stream.Write([0xff, 0xd8]);
        if (exif is not null) Segment(stream, 0xe1, [.. "Exif\0\0"u8, .. exif]);
        Segment(stream, frame, [8, (byte)(height >> 8), (byte)height, (byte)(width >> 8), (byte)width, 3,
            1, 0x11, 0, 2, 0x11, 0, 3, 0x11, 0]);
        Segment(stream, 0xda, [3, 1, 0, 2, 0, 3, 0, 0, 63, 0]);
        stream.Write([0x11, 0xff, 0, 0x22, 0xff, 0xd0, 0x33, 0xff, 0xd9]);
        return stream.ToArray();
    }

    internal static void Segment(Stream stream, byte marker, byte[] payload)
    {
        stream.Write([0xff, marker, (byte)((payload.Length + 2) >> 8), (byte)(payload.Length + 2)]);
        stream.Write(payload);
    }

    internal static byte[] Exif(ushort orientation, bool little = true)
    {
        var bytes = new byte[26];
        bytes[0] = bytes[1] = (byte)(little ? 'I' : 'M');
        void U16(int offset, ushort value)
        {
            if (little) BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(offset), value);
            else BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(offset), value);
        }
        void U32(int offset, uint value)
        {
            if (little) BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset), value);
            else BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(offset), value);
        }
        U16(2, 42); U32(4, 8); U16(8, 1); U16(10, 0x112); U16(12, 3); U32(14, 1); U16(18, orientation);
        return bytes;
    }
}

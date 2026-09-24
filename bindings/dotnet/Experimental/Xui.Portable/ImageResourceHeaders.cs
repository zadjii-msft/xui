using System.Buffers.Binary;

namespace Xui.Experimental.Portable;

internal static class ImageResourceHeaders
{
    private const int MaximumSegments = 65536;
    private static readonly uint[] CrcTable = CreateCrcTable();

    internal static ImageDecodePlan Inspect(ReadOnlySpan<byte> bytes, ImageDecodeOptions options, CancellationToken token)
    {
        if (bytes.StartsWith(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 })) return Png(bytes, options, token);
        if (bytes.Length >= 2 && bytes[0] == 0xff && bytes[1] == 0xd8) return Jpeg(bytes, options, token);
        throw new NotSupportedException("Only static PNG and baseline or progressive JPEG images are supported.");
    }

    private static ImageDecodePlan Png(ReadOnlySpan<byte> bytes, ImageDecodeOptions options, CancellationToken token)
    {
        int offset = 8, chunks = 0;
        bool data = false, endedData = false, palette = false;
        int colorType = -1;
        ImageDecodePlan? plan = null;
        while (offset < bytes.Length)
        {
            token.ThrowIfCancellationRequested();
            if (++chunks > MaximumSegments) throw new InvalidDataException("PNG contains too many chunks.");
            if (bytes.Length - offset < 12) throw new InvalidDataException("Truncated PNG chunk.");
            uint length = BinaryPrimitives.ReadUInt32BigEndian(bytes[offset..]);
            if (length > (uint)(bytes.Length - offset - 12)) throw new InvalidDataException("Invalid PNG chunk length.");
            var type = bytes.Slice(offset + 4, 4);
            if (!AsciiLetters(type) || (type[2] & 32) != 0) throw new InvalidDataException("Invalid PNG chunk type.");
            var payload = bytes.Slice(offset + 8, (int)length);
            uint crc = BinaryPrimitives.ReadUInt32BigEndian(bytes.Slice(offset + 8 + (int)length, 4));
            if (Crc32(bytes.Slice(offset + 4, (int)length + 4), token) != crc)
                throw new InvalidDataException("PNG chunk checksum mismatch.");
            if (plan is null && !type.SequenceEqual("IHDR"u8)) throw new InvalidDataException("PNG must begin with IHDR.");
            if (type.SequenceEqual("IHDR"u8))
            {
                if (plan is not null || length != 13) throw new InvalidDataException("Invalid or duplicate PNG IHDR.");
                uint width = BinaryPrimitives.ReadUInt32BigEndian(payload);
                uint height = BinaryPrimitives.ReadUInt32BigEndian(payload[4..]);
                if (width > int.MaxValue || height > int.MaxValue) throw new InvalidDataException("PNG dimensions overflow.");
                colorType = payload[9];
                bool depth = colorType switch
                {
                    0 => payload[8] is 1 or 2 or 4 or 8 or 16,
                    2 or 4 or 6 => payload[8] is 8 or 16,
                    3 => payload[8] is 1 or 2 or 4 or 8,
                    _ => false
                };
                if (!depth || payload[10] != 0 || payload[11] != 0 || payload[12] > 1)
                    throw new InvalidDataException("Invalid PNG image header.");
                plan = new(PackagedImageFormat.Png, (int)width, (int)height, options);
            }
            else if (type.SequenceEqual("acTL"u8) || type.SequenceEqual("fcTL"u8) || type.SequenceEqual("fdAT"u8))
                throw new NotSupportedException("Animated PNG is not supported.");
            else if (type.SequenceEqual("eXIf"u8)) Exif(payload);
            else if (type.SequenceEqual("PLTE"u8))
            {
                if (palette || data || length is 0 or > 768 || length % 3 != 0 || colorType is 0 or 4)
                    throw new InvalidDataException("Invalid PNG palette.");
                palette = true;
            }
            else if (type.SequenceEqual("IDAT"u8))
            {
                if (endedData || (colorType == 3 && !palette)) throw new InvalidDataException("Invalid PNG image data order.");
                data = true;
            }
            else if (type.SequenceEqual("IEND"u8))
            {
                if (length != 0 || !data || offset + 12 != bytes.Length) throw new InvalidDataException("Invalid PNG end marker.");
                return plan!;
            }
            else if ((type[0] & 32) == 0)
                throw new NotSupportedException("Unknown critical PNG chunk.");
            if (data && !type.SequenceEqual("IDAT"u8)) endedData = true;
            offset += (int)length + 12;
        }
        throw new InvalidDataException("PNG has no complete image terminator.");
    }

    private static ImageDecodePlan Jpeg(ReadOnlySpan<byte> bytes, ImageDecodeOptions options, CancellationToken token)
    {
        int offset = 2, segments = 0;
        bool entropy = false, sawScan = false;
        ImageDecodePlan? plan = null;
        while (offset < bytes.Length)
        {
            token.ThrowIfCancellationRequested();
            if (entropy)
            {
                while (offset < bytes.Length && bytes[offset] != 0xff)
                {
                    if ((offset & 4095) == 0) token.ThrowIfCancellationRequested();
                    offset++;
                }
            }
            if (offset >= bytes.Length || bytes[offset++] != 0xff) throw new InvalidDataException("Invalid JPEG marker.");
            while (offset < bytes.Length && bytes[offset] == 0xff) offset++;
            if (offset == bytes.Length) throw new InvalidDataException("Truncated JPEG marker.");
            byte marker = bytes[offset++];
            if (entropy && (marker == 0 || marker is >= 0xd0 and <= 0xd7)) continue;
            entropy = false;
            if (++segments > MaximumSegments) throw new InvalidDataException("JPEG contains too many segments.");
            if (marker == 0xd9)
            {
                if (plan is null || !sawScan || offset != bytes.Length) throw new InvalidDataException("Invalid JPEG end marker.");
                return plan;
            }
            if (marker is 0 or 0xd8 or 0x01 || marker is >= 0xd0 and <= 0xd7)
                throw new InvalidDataException("Unexpected JPEG marker.");
            if (bytes.Length - offset < 2) throw new InvalidDataException("Truncated JPEG segment length.");
            int length = BinaryPrimitives.ReadUInt16BigEndian(bytes[offset..]);
            if (length < 2 || length > bytes.Length - offset) throw new InvalidDataException("Invalid JPEG segment length.");
            var payload = bytes.Slice(offset + 2, length - 2);
            if (marker is >= 0xc0 and <= 0xcf && marker is not (0xc4 or 0xc8 or 0xcc))
            {
                if (marker is not (0xc0 or 0xc2)) throw new NotSupportedException("Only baseline and progressive JPEG frames are supported.");
                if (plan is not null || payload.Length < 6) throw new InvalidDataException("Invalid or multiple JPEG frames.");
                int components = payload[5];
                if (payload[0] != 8 || components is not (1 or 3) || payload.Length != 6 + 3 * components)
                    throw new NotSupportedException("Only 8-bit grayscale and three-component JPEG frames are supported.");
                plan = new(PackagedImageFormat.Jpeg,
                    BinaryPrimitives.ReadUInt16BigEndian(payload[3..]), BinaryPrimitives.ReadUInt16BigEndian(payload[1..]), options);
            }
            else if (marker == 0xe1 && payload.StartsWith("Exif\0\0"u8)) Exif(payload[6..]);
            else if (marker == 0xda)
            {
                if (plan is null || payload.Length < 6 || payload.Length != 4 + 2 * payload[0])
                    throw new InvalidDataException("Invalid JPEG scan header.");
                sawScan = true;
                entropy = true;
            }
            else if (marker == 0xdc)
                throw new NotSupportedException("JPEG deferred dimensions are not supported.");
            offset += length;
        }
        throw new InvalidDataException("JPEG has no complete image terminator.");
    }

    private static void Exif(ReadOnlySpan<byte> tiff)
    {
        if (tiff.Length < 8) throw new InvalidDataException("Truncated image EXIF header.");
        bool little = tiff[..2].SequenceEqual("II"u8);
        if (!little && !tiff[..2].SequenceEqual("MM"u8)) throw new InvalidDataException("Invalid EXIF byte order.");
        if (U16(tiff[2..], little) != 42) throw new InvalidDataException("Invalid EXIF TIFF header.");
        uint next = U32(tiff[4..], little);
        if (next == 0) return;
        if (next < 8 || next > (uint)(tiff.Length - 2)) throw new InvalidDataException("Invalid EXIF directory offset.");
        int offset = (int)next;
        int entries = U16(tiff[offset..], little);
        if ((long)offset + 2 + 12L * entries + 4 > tiff.Length) throw new InvalidDataException("Truncated EXIF directory.");
        bool orientation = false;
        for (int i = 0; i < entries; i++)
        {
            var entry = tiff.Slice(offset + 2 + 12 * i, 12);
            if (U16(entry, little) != 0x112) continue;
            if (orientation || U16(entry[2..], little) != 3 || U32(entry[4..], little) != 1)
                throw new InvalidDataException("Invalid EXIF orientation entry.");
            orientation = true;
            if (U16(entry[8..], little) != 1)
                throw new NotSupportedException("Images with EXIF rotation or reflection are not supported.");
        }
    }

    private static ushort U16(ReadOnlySpan<byte> value, bool little) =>
        little ? BinaryPrimitives.ReadUInt16LittleEndian(value) : BinaryPrimitives.ReadUInt16BigEndian(value);
    private static uint U32(ReadOnlySpan<byte> value, bool little) =>
        little ? BinaryPrimitives.ReadUInt32LittleEndian(value) : BinaryPrimitives.ReadUInt32BigEndian(value);
    private static bool AsciiLetters(ReadOnlySpan<byte> type)
    {
        foreach (byte value in type) if (value is not (>= 65 and <= 90 or >= 97 and <= 122)) return false;
        return true;
    }

    private static uint Crc32(ReadOnlySpan<byte> bytes, CancellationToken token)
    {
        uint crc = uint.MaxValue;
        for (int index = 0; index < bytes.Length; index++)
        {
            if ((index & 4095) == 0) token.ThrowIfCancellationRequested();
            crc = (crc >> 8) ^ CrcTable[(crc ^ bytes[index]) & 255];
        }
        return ~crc;
    }

    private static uint[] CreateCrcTable()
    {
        var result = new uint[256];
        for (uint index = 0; index < result.Length; index++)
        {
            uint crc = index;
            for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320u : 0);
            result[index] = crc;
        }
        return result;
    }
}

using System.Buffers.Binary;
using System.Collections.ObjectModel;
using System.Security.Cryptography;
using System.Text;

namespace Xui.Experimental.Portable;

/// <summary>Bounded static TrueType metadata, not native registration or a legal license determination.</summary>
public sealed class FontResourceMetadata
{
    public const int MaximumEncodedBytes = 4 * 1024 * 1024;
    public const int MaximumTables = 64;
    public const int MaximumGlyphs = 8192;
    public string Sha256 { get; }
    public string FamilyName { get; }
    public string PostScriptName { get; }
    public int GlyphCount { get; }
    public int UnitsPerEm { get; }
    public int Weight => 400;
    public int FaceIndex => 0;
    public bool Italic => false;
    public ReadOnlyDictionary<string, string> TableSha256 { get; }

    private FontResourceMetadata(string hash, string family, string postScript, int glyphs, int units,
        Dictionary<string, string> tableHashes)
    {
        Sha256 = hash;
        FamilyName = family;
        PostScriptName = postScript;
        GlyphCount = glyphs;
        UnitsPerEm = units;
        TableSha256 = new(tableHashes);
    }

    public void ValidateFace(int faceIndex, uint weight, bool italic)
    {
        if (faceIndex != 0 || weight != 400 || italic)
            throw new NotSupportedException("Packaged fonts currently support only face zero, regular weight 400, and normal style.");
    }

    public static FontResourceMetadata Inspect(ReadOnlySpan<byte> bytes, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (bytes.Length is < 12 or > MaximumEncodedBytes) throw new InvalidDataException("Font bytes exceed the bounded SFNT input range.");
        if (U32(bytes) != 0x00010000)
            throw new NotSupportedException("Only a single static TrueType-outline SFNT face is supported; collections, CFF, WOFF and variable fonts are not supported.");
        int count = U16(bytes[4..]);
        if (count is < 1 or > MaximumTables || bytes.Length < 12 + 16 * count) throw new InvalidDataException("Invalid or oversized SFNT table directory.");
        int power = 1, selector = 0;
        while (power * 2 <= count) { power *= 2; selector++; }
        if (U16(bytes[6..]) != power * 16 || U16(bytes[8..]) != selector || U16(bytes[10..]) != count * 16 - power * 16)
            throw new InvalidDataException("Invalid SFNT search parameters.");
        var tables = new Dictionary<string, Table>(StringComparer.Ordinal);
        var hashes = new Dictionary<string, string>(StringComparer.Ordinal);
        var ranges = new List<(int Start, int End)>();
        uint previousTag = 0;
        for (int index = 0; index < count; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var record = bytes.Slice(12 + 16 * index, 16);
            uint tag = U32(record), offset = U32(record[8..]), length = U32(record[12..]);
            if ((index != 0 && tag <= previousTag) || offset % 4 != 0 || offset < 12 + 16 * count ||
                offset > bytes.Length || length > bytes.Length - offset)
                throw new InvalidDataException("Invalid, duplicate, unsorted, or out-of-bounds SFNT table.");
            previousTag = tag;
            foreach (byte value in record[..4])
                if (value is < 32 or > 126) throw new InvalidDataException("Invalid SFNT table tag.");
            int start = (int)offset, end = checked(start + (int)length);
            foreach (var range in ranges)
                if (start < range.End && end > range.Start) throw new InvalidDataException("Overlapping SFNT table ranges.");
            ranges.Add((start, end));
            string name = Encoding.ASCII.GetString(record[..4]);
            var data = bytes.Slice(start, (int)length);
            if (Checksum(data, name == "head" ? 8 : -1, cancellationToken) != U32(record[4..]))
                throw new InvalidDataException($"SFNT checksum mismatch in '{name}'.");
            tables.Add(name, new(start, (int)length));
            hashes.Add(name, Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant());
        }
        if (Checksum(bytes, -1, cancellationToken) != 0xb1b0afba)
            throw new InvalidDataException("SFNT whole-file checksum mismatch.");
        foreach (string unsupported in new[] { "fvar", "gvar", "CFF ", "CFF2", "COLR", "CPAL", "CBDT", "CBLC", "EBDT", "EBLC", "sbix", "SVG " })
            if (tables.ContainsKey(unsupported)) throw new NotSupportedException("Variable, color, bitmap, SVG and CFF font data are outside this prototype.");
        var head = Required(bytes, tables, "head", 54);
        if (U32(head[12..]) != 0x5f0f3cf5) throw new InvalidDataException("Invalid TrueType head magic.");
        int units = U16(head[18..]), style = U16(head[44..]), locationFormat = I16(head[50..]);
        if (units is < 16 or > 16384 || locationFormat is not (0 or 1) || I16(head[52..]) != 0)
            throw new InvalidDataException("Invalid TrueType header metrics.");
        if (style != 0) throw new NotSupportedException("Only a regular, upright TrueType face is supported.");
        var maxp = Required(bytes, tables, "maxp", 32);
        int glyphs = U16(maxp[4..]);
        if (U32(maxp) != 0x00010000 || glyphs is < 1 or > MaximumGlyphs) throw new InvalidDataException("Invalid or oversized TrueType glyph count.");
        var os2 = Required(bytes, tables, "OS/2", 78);
        int fsType = U16(os2[8..]), selection = U16(os2[62..]);
        if (fsType != 0) throw new NotSupportedException("This controlled-font prototype requires OS/2 fsType zero plus independently declared distribution rights.");
        if (U16(os2[4..]) != 400 || U16(os2[6..]) != 5 || (selection & (1 | 32 | 512)) != 0 || (selection & 64) == 0)
            throw new NotSupportedException("Only regular weight 400, normal-width, non-italic faces are supported.");
        var hhea = Required(bytes, tables, "hhea", 36);
        int metrics = U16(hhea[34..]);
        if (metrics < 1 || metrics > glyphs) throw new InvalidDataException("Invalid horizontal metrics count.");
        Required(bytes, tables, "hmtx", checked(4 * metrics + 2 * (glyphs - metrics)));
        var loca = Required(bytes, tables, "loca", checked((glyphs + 1) * (locationFormat == 0 ? 2 : 4)));
        var glyf = Required(bytes, tables, "glyf", 1);
        uint last = 0;
        for (int glyph = 0; glyph <= glyphs; glyph++)
        {
            uint position = locationFormat == 0 ? (uint)U16(loca[(glyph * 2)..]) * 2 : U32(loca[(glyph * 4)..]);
            if (position < last || position > glyf.Length) throw new InvalidDataException("Invalid glyph location offsets.");
            last = position;
        }
        ValidateCmap(Required(bytes, tables, "cmap", 4));
        var (family, postScript) = Names(Required(bytes, tables, "name", 6));
        cancellationToken.ThrowIfCancellationRequested();
        return new(Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(), family, postScript, glyphs, units, hashes);
    }

    private static (string Family, string PostScript) Names(ReadOnlySpan<byte> table)
    {
        int version = U16(table), count = U16(table[2..]), storage = U16(table[4..]);
        if (version != 0) throw new NotSupportedException("This initial font prototype supports naming table version zero.");
        if (count is < 1 or > 256 || 6 + 12 * count > table.Length || storage < 6 + 12 * count || storage > table.Length)
            throw new InvalidDataException("Invalid font naming directory.");
        var names = new Dictionary<int, HashSet<string>>();
        var decoder = new UnicodeEncoding(true, false, true);
        for (int index = 0; index < count; index++)
        {
            var record = table.Slice(6 + 12 * index, 12);
            int platform = U16(record), encoding = U16(record[2..]), id = U16(record[6..]), length = U16(record[8..]), offset = U16(record[10..]);
            if (length > 2048 || storage + offset > table.Length || length > table.Length - storage - offset)
                throw new InvalidDataException("Font name points outside its bounded string storage.");
            if (id is not (1 or 2 or 6 or 16 or 17) || (platform != 0 && !(platform == 3 && encoding is 1 or 10))) continue;
            if (length == 0 || length % 2 != 0) throw new InvalidDataException("Invalid Unicode font name.");
            string value;
            try { value = decoder.GetString(table.Slice(storage + offset, length)); }
            catch (DecoderFallbackException error) { throw new InvalidDataException("Malformed Unicode font name.", error); }
            if (value.Any(char.IsControl)) throw new InvalidDataException("Control characters in font names are not supported.");
            if (!names.TryGetValue(id, out var values)) names.Add(id, values = new(StringComparer.Ordinal));
            values.Add(value);
        }
        string One(int id)
        {
            if (!names.TryGetValue(id, out var values) || values.Count != 1)
                throw new NotSupportedException("A controlled font needs unambiguous family, style and PostScript names.");
            return values.Single();
        }
        string family = One(1), style = One(2), postScript = One(6);
        if (family.Length is < 1 or > 31 || family.Trim() != family ||
            family.Any(character => character is not (>= 'A' and <= 'Z' or >= 'a' and <= 'z' or >= '0' and <= '9' or ' ' or '-')) ||
            style != "Regular" || postScript.Length is < 1 or > 63 ||
            postScript.Any(character => character is not (>= 'A' and <= 'Z' or >= 'a' and <= 'z' or >= '0' and <= '9' or '-')))
            throw new NotSupportedException("This initial font prototype requires regular style and short, unambiguous ASCII native names.");
        if (names.ContainsKey(16) && One(16) != family || names.ContainsKey(17) && One(17) != style)
            throw new NotSupportedException("Conflicting legacy and typographic font names are not supported.");
        return (family, postScript);
    }

    private static void ValidateCmap(ReadOnlySpan<byte> table)
    {
        int count = U16(table[2..]);
        if (U16(table) != 0 || count is < 1 or > 32 || table.Length < 4 + 8 * count) throw new InvalidDataException("Invalid cmap directory.");
        bool unicode = false;
        for (int index = 0; index < count; index++)
        {
            var record = table.Slice(4 + 8 * index, 8);
            uint offset = U32(record[4..]);
            if (offset > table.Length - 2) throw new InvalidDataException("Invalid cmap subtable offset.");
            var data = table[(int)offset..];
            int format = U16(data);
            if (format is 4 or 12 && (U16(record) == 0 || U16(record) == 3))
            {
                uint length = format == 4 && data.Length >= 4 ? U16(data[2..]) :
                    format == 12 && data.Length >= 8 ? U32(data[4..]) : 0;
                if (length < 16 || length > data.Length) throw new InvalidDataException("Invalid Unicode cmap length.");
                if (format == 4)
                {
                    int segmentBytes = U16(data[6..]);
                    if (segmentBytes < 2 || segmentBytes % 2 != 0 || 16L + 4L * segmentBytes > length)
                        throw new InvalidDataException("Invalid format-four cmap segments.");
                    int segments = segmentBytes / 2;
                    ushort previousEnd = 0;
                    for (int segment = 0; segment < segments; segment++)
                    {
                        ushort end = U16(data[(14 + 2 * segment)..]);
                        ushort start = U16(data[(16 + segmentBytes + 2 * segment)..]);
                        if (start > end || (segment != 0 && end <= previousEnd))
                            throw new InvalidDataException("Invalid cmap character ranges.");
                        previousEnd = end;
                        int rangePosition = 16 + 3 * segmentBytes + 2 * segment;
                        int range = U16(data[rangePosition..]);
                        if (range != 0 && ((range & 1) != 0 || (long)rangePosition + range + 2L * (end - start) + 2 > length))
                            throw new InvalidDataException("A cmap glyph offset exceeds its subtable.");
                    }
                    if (previousEnd != ushort.MaxValue) throw new InvalidDataException("Format-four cmap lacks a sentinel segment.");
                }
                else
                {
                    uint groups = U32(data[12..]);
                    if (groups > 65536 || 16L + 12L * groups != length) throw new InvalidDataException("Invalid format-twelve cmap groups.");
                    uint previousEnd = 0;
                    for (int group = 0; group < groups; group++)
                    {
                        var range = data.Slice(16 + 12 * group, 12);
                        uint start = U32(range), end = U32(range[4..]);
                        if (start > end || end > 0x10ffff || (group != 0 && start <= previousEnd))
                            throw new InvalidDataException("Invalid format-twelve Unicode range.");
                        previousEnd = end;
                    }
                }
                unicode = true;
            }
        }
        if (!unicode) throw new NotSupportedException("A Unicode cmap is required.");
    }

    private readonly record struct Table(int Offset, int Length);
    private static ReadOnlySpan<byte> Required(ReadOnlySpan<byte> bytes, Dictionary<string, Table> tables, string name, int minimum)
    {
        if (!tables.TryGetValue(name, out var table) || table.Length < minimum)
            throw new InvalidDataException($"Required font table '{name}' is absent or truncated.");
        return bytes.Slice(table.Offset, table.Length);
    }
    private static ushort U16(ReadOnlySpan<byte> bytes) => BinaryPrimitives.ReadUInt16BigEndian(bytes);
    private static short I16(ReadOnlySpan<byte> bytes) => BinaryPrimitives.ReadInt16BigEndian(bytes);
    private static uint U32(ReadOnlySpan<byte> bytes) => BinaryPrimitives.ReadUInt32BigEndian(bytes);
    private static uint Checksum(ReadOnlySpan<byte> bytes, int zeroOffset, CancellationToken token)
    {
        uint sum = 0;
        for (int offset = 0; offset < bytes.Length; offset += 4)
        {
            if ((offset & 4095) == 0) token.ThrowIfCancellationRequested();
            uint word = 0;
            for (int index = 0; index < 4; index++)
                word = (word << 8) | (offset + index < bytes.Length && !(offset + index >= zeroOffset && offset + index < zeroOffset + 4 && zeroOffset >= 0)
                    ? bytes[offset + index] : 0u);
            sum = unchecked(sum + word);
        }
        return sum;
    }
}

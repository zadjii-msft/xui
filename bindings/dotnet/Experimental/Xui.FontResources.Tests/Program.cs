using System.Buffers.Binary;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using Xui.Experimental.Portable;

internal static class Program
{
    private const string FontHash = "8809dcad25318225052f88333e208c5aad4adcb7b2c934c135735ec19aa410b4";
    private const string LicenseHash = "4f4bc3806a1e55789c6ef75ca5fc628297b05292f74966474dc0d40324abc609";
    private static readonly Uri Provenance = new("https://github.com/google/fonts/tree/9437b806936896fa1a8c812e561067a5f30f5933/ofl/abel");
    private static int assertions;
    private static byte[] font = [], license = [];

    private static async Task Main()
    {
        var manifest = new PackagedAssetManifest(typeof(Program).Assembly);
        await using (var owned = await manifest.OpenReadAsync("fonts/abel-regular.ttf", FontResourceMetadata.MaximumEncodedBytes))
        {
            font = new byte[checked((int)owned.Length!.Value)];
            await owned.Content.ReadExactlyAsync(font);
        }
        await using (var owned = await manifest.OpenReadAsync("licenses/abel-ofl.txt", FontResourceCache.MaximumLicenseBytes))
        {
            license = new byte[checked((int)owned.Length!.Value)];
            await owned.Content.ReadExactlyAsync(license);
        }
        Metadata();
        await Ownership(manifest);
        await Cancellation();
        await ByteBudget();
        Console.WriteLine($"Controlled font resources: {assertions} assertions passed. No font was registered, installed, rendered, renamed or subsetted.");
    }

    private static void Check(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
        assertions++;
    }
    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
    private static async Task<T> Fails<T>(Func<Task> action) where T : Exception
    {
        try { await action().WaitAsync(TimeSpan.FromSeconds(5)); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    private static FontLicenseDeclaration License(string hash = LicenseHash) => new("licenses/abel-ofl.txt", hash, "OFL-1.1", Provenance);
    private static PackagedFontSource Source(PackagedAssetManifest manifest, string hash = FontHash, string licenseHash = LicenseHash) =>
        new(manifest, "fonts/abel-regular.ttf", hash, License(licenseHash));

    private static void Metadata()
    {
        Check(font.Length == 35220 && Hash(font) == FontHash, "Unmodified pinned font bytes.");
        Check(license.Length == 4418 && Hash(license) == LicenseHash, "Complete unchanged OFL license bytes.");
        string text = Encoding.UTF8.GetString(license);
        Check(text.Contains("Reserved Font Name Abel") && text.Contains("SIL OPEN FONT LICENSE Version 1.1"), "Actual license and attribution are preserved.");
        var metadata = FontResourceMetadata.Inspect(font);
        Check(metadata.FamilyName == "Abel" && metadata.PostScriptName == "Abel-Regular" && metadata.Weight == 400 &&
            metadata.GlyphCount == 259 && metadata.UnitsPerEm == 2048 && metadata.FaceIndex == 0 && !metadata.Italic,
            "Native identity and face metrics come from the actual SFNT.");
        Check(metadata.TableSha256.Count == 14 && metadata.Sha256 == FontHash, "Immutable whole-font and table fingerprints.");
        Throws<NotSupportedException>(() => ((IDictionary<string, string>)metadata.TableSha256).Clear());
        metadata.ValidateFace(0, 400, false);
        Throws<NotSupportedException>(() => metadata.ValidateFace(0, 700, false));
        Throws<NotSupportedException>(() => metadata.ValidateFace(1, 400, false));
        Throws<NotSupportedException>(() => metadata.ValidateFace(0, 400, true));
        Throws<OperationCanceledException>(() => FontResourceMetadata.Inspect(font, new(true)));
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect([]));
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(new byte[FontResourceMetadata.MaximumEncodedBytes + 1]));
        foreach (string signature in new[] { "OTTO", "ttcf", "wOFF", "wOF2" })
        {
            var bytes = font.ToArray(); Encoding.ASCII.GetBytes(signature).CopyTo(bytes, 0);
            Throws<NotSupportedException>(() => FontResourceMetadata.Inspect(bytes));
        }
        var tableCount = font.ToArray(); Put16(tableCount, 4, 65);
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(tableCount));
        var overlap = font.ToArray(); Put32(overlap, 12 + 16 + 8, Get32(overlap, 12 + 8));
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(overlap));
        var overflow = font.ToArray(); Put32(overflow, 12 + 8, uint.MaxValue - 3);
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(overflow));
        var duplicate = font.ToArray(); font.AsSpan(12, 4).CopyTo(duplicate.AsSpan(28));
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(duplicate));
        var badChecksum = font.ToArray(); badChecksum[TableOffset(font, "glyf") + 10] ^= 1;
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(badChecksum));
        foreach (ushort fsType in new ushort[] { 1, 2, 4, 8, 0x100, 0x200 })
        {
            var bytes = font.ToArray(); Put16(bytes, TableOffset(bytes, "OS/2") + 8, fsType); Repair(bytes);
            Throws<NotSupportedException>(() => FontResourceMetadata.Inspect(bytes));
        }
        var bold = font.ToArray(); Put16(bold, TableOffset(bold, "OS/2") + 4, 700); Repair(bold);
        Throws<NotSupportedException>(() => FontResourceMetadata.Inspect(bold));
        var glyphs = font.ToArray(); Put16(glyphs, TableOffset(glyphs, "maxp") + 4, 8193); Repair(glyphs);
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(glyphs));
        var badLoca = font.ToArray(); Put16(badLoca, TableOffset(badLoca, "loca") + 2, ushort.MaxValue); Repair(badLoca);
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(badLoca));
        var names = font.ToArray(); Put16(names, TableOffset(names, "name") + 4, ushort.MaxValue); Repair(names);
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(names));
        var malformedName = font.ToArray();
        int name = TableOffset(malformedName, "name"), records = Get16(malformedName, name + 2);
        for (int i = 0; i < records; i++)
        {
            int record = name + 6 + 12 * i;
            if (Get16(malformedName, record) != 3 || Get16(malformedName, record + 6) != 1) continue;
            int data = name + Get16(malformedName, name + 4) + Get16(malformedName, record + 10);
            Put16(malformedName, data, 0);
        }
        Repair(malformedName);
        Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(malformedName));
        for (int length = 12; length < font.Length; length += 257)
            Throws<InvalidDataException>(() => FontResourceMetadata.Inspect(font.AsSpan(0, length)));
        var random = new Random(1231);
        for (int i = 0; i < 128; i++)
        {
            byte[] bytes = font.ToArray(); bytes[random.Next(12, bytes.Length)] ^= (byte)random.Next(1, 256);
            try { FontResourceMetadata.Inspect(bytes); Check(false, "A mutated pinned font passed checksums."); }
            catch (Exception error) when (error is InvalidDataException or NotSupportedException) { assertions++; }
        }
    }

    private static async Task Ownership(PackagedAssetManifest manifest)
    {
        Throws<ArgumentException>(() => new FontLicenseDeclaration("licenses/a", "BAD", "OFL-1.1", Provenance));
        Throws<ArgumentException>(() => new FontLicenseDeclaration("licenses/a", LicenseHash, "OFL-1.1", new("http://example.com/license")));
        Throws<ArgumentException>(() => new PackagedFontSource(manifest, "fonts/abel-regular.ttf", FontHash.ToUpperInvariant(), License()));
        Throws<FileNotFoundException>(() => new PackagedFontSource(manifest, "fonts/missing.ttf", FontHash, License()));
        var source = Source(manifest);
        Check(source.AndroidAssetName == $"xui-fonts/{FontHash}.ttf", "Android projected asset uses the exact lowercase content digest.");
        using var cache = new FontResourceCache();
        var first = await cache.AcquireAsync(source);
        var second = await cache.AcquireAsync(source);
        Check(first.FontBytes.Span.SequenceEqual(font) && second.LicenseBytes.Span.SequenceEqual(license), "Owned font and legal bytes are exact.");
        Check(cache.Statistics.ResidentBytes == font.Length + license.Length && cache.Statistics.Faces == 1, "Shared pins do not double count resident bytes.");
        first.Dispose(); first.Dispose();
        Throws<ObjectDisposedException>(() => _ = first.FontBytes);
        cache.ClearUnused();
        Check(cache.Statistics.PinnedFaces == 1, "Pinned resources cannot be evicted.");
        cache.Dispose();
        Check(second.FontBytes.Length == font.Length, "Cache disposal does not revoke a caller-owned lease.");
        second.Dispose();
        Check(cache.Statistics == default, "Final disposal releases every owned byte and slot.");
        using var pinned = new FontResourceCache();
        var pins = new List<FontResourceLease>();
        for (int i = 0; i < FontResourceCache.MaximumFaces; i++)
            pins.Add(await pinned.AcquireAsync(Source(new(new FixtureAssembly(font, license)))));
        await Fails<FontResourceLimitException>(() => pinned.AcquireAsync(Source(manifest)));
        foreach (var pin in pins) pin.Dispose();
        pinned.ClearUnused();
        Check(pinned.Statistics == default, "Four-face capacity is bounded and reusable after lease release.");
        for (int cycle = 0; cycle < 110; cycle++)
        {
            (await pinned.AcquireAsync(source)).Dispose();
            pinned.ClearUnused();
            Check(pinned.Statistics == default, "Repeated application resource lifetime returns owned buffers to zero.");
        }
        using var failures = new FontResourceCache();
        await Fails<InvalidDataException>(() => failures.AcquireAsync(Source(manifest, new string('0', 64))));
        await Fails<InvalidDataException>(() => failures.AcquireAsync(Source(manifest, FontHash, new string('0', 64))));
        Check(failures.Statistics == default, "Digest failures publish no resource and release reservations.");
        byte[] changed = font.ToArray(); changed[TableOffset(changed, "FFTM") + 7] ^= 1; Repair(changed);
        using var keep = await failures.AcquireAsync(source);
        await Fails<FontResourceLimitException>(() => failures.AcquireAsync(Source(new(new FixtureAssembly(changed, license)), Hash(changed))));
        Check(failures.Statistics.Faces == 1, "Same family/PostScript name cannot silently select different owned bytes.");
        byte[] badLicense = [0xff, 0xfe];
        using var invalid = new FontResourceCache();
        await Fails<InvalidDataException>(() => invalid.AcquireAsync(Source(new(new FixtureAssembly(font, badLicense)), FontHash, Hash(badLicense))));
        Check(invalid.Statistics == default, "Invalid UTF-8 license bytes cannot produce a valid resource.");
        byte[] emptyLicense = [];
        await Fails<InvalidDataException>(() => invalid.AcquireAsync(Source(new(new FixtureAssembly(font, emptyLicense)), FontHash, Hash(emptyLicense))));
        Check(invalid.Statistics == default, "Missing legal text releases the font allocation and request slot.");
    }

    private static async Task ByteBudget()
    {
        byte[] maximum = new byte[FontResourceMetadata.MaximumEncodedBytes];
        font.CopyTo(maximum, 0);
        // Zero padding leaves SFNT checksums unchanged and exercises the exact encoded ceiling.
        Check(FontResourceMetadata.Inspect(maximum).GlyphCount == 259, "Exact four-MiB encoded ceiling.");
        using var cache = new FontResourceCache();
        var leases = new List<FontResourceLease>();
        try
        {
            for (int i = 0; i < 3; i++)
                leases.Add(await cache.AcquireAsync(Source(new(new FixtureAssembly(maximum, license)), Hash(maximum))));
            await Fails<FontResourceLimitException>(() => cache.AcquireAsync(Source(new(new FixtureAssembly(maximum, license)), Hash(maximum))));
            Check(cache.Statistics.ResidentBytes == 3L * (maximum.Length + license.Length) && cache.Statistics.ReservedBytes == 0,
                "Pinned byte budget includes licenses and rejects allocation before exceeding sixteen MiB.");
        }
        finally { foreach (var lease in leases) lease.Dispose(); }
        cache.ClearUnused();
        Check(cache.Statistics == default, "Large bounded buffer leases release all residency.");
    }

    private static async Task Cancellation()
    {
        var provider = new FixtureAssembly(font, license);
        var source = Source(new(provider));
        using var cache = new FontResourceCache();
        await Fails<OperationCanceledException>(() => cache.AcquireAsync(source, new(true)));
        Check(provider.Opens == 0, "Pre-cancel never opens the packaged font.");
        var pause = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        provider.Pause = pause;
        using var cancellation = new CancellationTokenSource();
        var pending = cache.AcquireAsync(source, cancellation.Token);
        Check(cache.Statistics.InFlight == 1 && cache.Statistics.ReservedBytes == font.Length, "In-flight font bytes are reserved before reads.");
        await Fails<FontResourceLimitException>(() => cache.AcquireAsync(source));
        cancellation.Cancel();
        pause.SetResult();
        await Fails<OperationCanceledException>(() => pending);
        Check(cache.Statistics == default && provider.Disposals == 1, "Late canceled reads release the stream and reservation.");
        pause = new(TaskCreationOptions.RunContinuationsAsynchronously);
        provider.Pause = pause;
        pending = cache.AcquireAsync(source);
        cache.Dispose();
        pause.SetResult();
        await Fails<OperationCanceledException>(() => pending);
        Check(cache.Statistics == default && provider.Disposals == 2, "Owner disposal rejects late results without retaining bytes.");
    }

    private static int TableRecord(byte[] bytes, string tag)
    {
        for (int i = 0; i < Get16(bytes, 4); i++)
            if (Encoding.ASCII.GetString(bytes, 12 + 16 * i, 4) == tag) return 12 + 16 * i;
        throw new InvalidDataException(tag);
    }
    private static int TableOffset(byte[] bytes, string tag) => checked((int)Get32(bytes, TableRecord(bytes, tag) + 8));
    private static ushort Get16(byte[] bytes, int offset) => BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(offset));
    private static uint Get32(byte[] bytes, int offset) => BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset));
    private static void Put16(byte[] bytes, int offset, ushort value) => BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(offset), value);
    private static void Put32(byte[] bytes, int offset, uint value) => BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(offset), value);
    private static uint Sum(ReadOnlySpan<byte> bytes)
    {
        uint value = 0;
        for (int offset = 0; offset < bytes.Length; offset += 4)
        {
            uint word = 0;
            for (int i = 0; i < 4; i++) word = (word << 8) | (offset + i < bytes.Length ? bytes[offset + i] : 0u);
            value = unchecked(value + word);
        }
        return value;
    }
    private static void Repair(byte[] bytes)
    {
        int head = TableOffset(bytes, "head");
        Put32(bytes, head + 8, 0);
        for (int i = 0; i < Get16(bytes, 4); i++)
        {
            int record = 12 + 16 * i;
            Put32(bytes, record + 4, Sum(bytes.AsSpan((int)Get32(bytes, record + 8), (int)Get32(bytes, record + 12))));
        }
        Put32(bytes, head + 8, unchecked(0xb1b0afba - Sum(bytes)));
    }

    private sealed class FixtureAssembly(byte[] fontBytes, byte[] licenseBytes) : Assembly
    {
        public int Opens, Disposals;
        public TaskCompletionSource? Pause;
        public override string[] GetManifestResourceNames() => ["Xui.Asset.fonts/abel-regular.ttf", "Xui.Asset.licenses/abel-ofl.txt"];
        public override Stream? GetManifestResourceStream(string name)
        {
            Opens++;
            return new ObservedStream(name.Contains("licenses", StringComparison.Ordinal) ? licenseBytes : fontBytes, this);
        }
        private sealed class ObservedStream(byte[] bytes, FixtureAssembly owner) : MemoryStream(bytes)
        {
            public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
            {
                if (owner.Pause is not null) await owner.Pause.Task;
                byte[] copy = new byte[buffer.Length];
                int count = base.Read(copy, 0, copy.Length);
                copy.AsSpan(0, count).CopyTo(buffer.Span);
                return count;
            }
            protected override void Dispose(bool disposing) { if (disposing) owner.Disposals++; base.Dispose(disposing); }
        }
    }
}

using System.Reflection;
using System.Text;
using Xui.Experimental.Portable;

internal static class Program
{
    private static int assertions;
    private static readonly byte[] Bytes = [3, 1, 4];

    private static async Task Main()
    {
        IdsAndManifest();
        await EmbeddedBytes();
        await OwnershipAndFailures();
        Console.WriteLine($"Packaged assets: {assertions} assertions passed; byte metadata only, no image decoding or font registration.");
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

    private static void IdsAndManifest()
    {
        foreach (string id in new[] { "a", "a/b", "images/logo.png", "fonts/body-regular.ttf", "a_b/c.d", new string('a', 160) })
        {
            PackagedAssetIds.Validate(id);
            Check(true, "Canonical id accepted.");
        }
        foreach (string id in new[]
        {
            "", ".", "..", "./a", "../a", "a/../b", "a/./b", "/a", "a/", "a//b", @"a\b",
            "A", "Images/logo.png", "a%2fb", "https://a", "file:a", "a?b", "a#b", "a b", "a\nb",
            "\ta", "a\0b", ".a", "a.", "a/-b", "_a", new string('a', 161), "caf\u00e9"
        }) Throws<ArgumentException>(() => PackagedAssetIds.Validate(id));
        Throws<ArgumentNullException>(() => PackagedAssetIds.Validate(null!));
        Throws<ArgumentNullException>(() => new PackagedAssetManifest(null!));
        Throws<ArgumentException>(() => new PackagedAssetManifest(new FixtureAssembly(["Xui.Asset.../a"])));
        Throws<InvalidDataException>(() => new PackagedAssetManifest(new FixtureAssembly(["Xui.Asset.a", "Xui.Asset.a"])));
        Throws<InvalidDataException>(() => new PackagedAssetManifest(new FixtureAssembly(
            Enumerable.Range(0, 4097).Select(i => $"Xui.Asset.assets/{i}").ToArray())));
        var largest = new PackagedAssetManifest(new FixtureAssembly(
            Enumerable.Range(0, 4096).Select(i => $"Xui.Asset.assets/{i}").ToArray()));
        Check(largest.Assets.Count == 4096, "Manifest entry budget has an exact boundary.");
        Check(new PackagedAssetManifest(new FixtureAssembly(["Unrelated.Resource"])).Assets.Count == 0,
            "Unrelated managed resources are ignored, not exposed as assets.");
    }

    private static async Task EmbeddedBytes()
    {
        var manifest = new PackagedAssetManifest(typeof(Program).Assembly);
        Check(manifest.Assets.Count == 5, "All XuiAsset items, and only those items, are cataloged.");
        Check(manifest.Assets.Select(a => a.Id).SequenceEqual(new[]
        {
            "data/message.txt", "fonts/body.ttf", "fonts/title.otf", "images/logo.png", "images/photo.jpg"
        }), "Catalog has deterministic ordinal ordering.");
        Check(manifest.Get("fonts/body.ttf").Kind == PackagedAssetKind.Font &&
            manifest.Get("fonts/body.ttf").ContentType == "font/ttf", "TTF metadata.");
        Check(manifest.Get("fonts/title.otf").ContentType == "font/otf", "OTF metadata.");
        Check(manifest.Get("images/logo.png").Kind == PackagedAssetKind.Image &&
            manifest.Get("images/logo.png").ContentType == "image/png", "PNG metadata.");
        Check(manifest.Get("images/photo.jpg").ContentType == "image/jpeg", "JPEG metadata.");
        Check(manifest.Get("data/message.txt").Kind == PackagedAssetKind.Data &&
            manifest.Get("data/message.txt").ContentType == "application/octet-stream", "Unknown extension has honest data metadata.");
        Throws<NotSupportedException>(() => ((IList<PackagedAssetDescriptor>)manifest.Assets).Clear());
        byte[]? first = null;
        foreach (var asset in manifest.Assets)
        {
            await using var owner = await manifest.OpenReadAsync(asset.Id, 1024);
            Check(ReferenceEquals(asset, owner.Asset) && owner.MaximumBytes == 1024 && owner.Length > 0, "Open preserves catalog identity and known byte length.");
            Check(!owner.Content.CanSeek && !owner.Content.CanWrite, "Owned resource is read-only and nonseekable.");
            using var copy = new MemoryStream();
            await owner.Content.CopyToAsync(copy);
            var bytes = copy.ToArray();
            Check(Encoding.UTF8.GetString(bytes).Contains("not an encoded image or font"), "Labels do not pretend bytes were decoded.");
            if (first is not null) Check(bytes.SequenceEqual(first), "One authored payload keeps exact bytes under image/font IDs.");
            first = bytes;
        }
        await using var exact = await manifest.OpenReadAsync("images/logo.png", first!.Length);
        Check(await exact.Content.ReadAsync(new byte[first.Length]) == first.Length, "Exact limit permits exact resource.");
        await Fails<IOException>(() => manifest.OpenReadAsync("images/logo.png", first.Length - 1));
        await Fails<FileNotFoundException>(() => manifest.OpenReadAsync("missing.png", 1024));
        Throws<ArgumentException>(() => manifest.OpenReadAsync("../missing", 1024));
        Throws<ArgumentOutOfRangeException>(() => manifest.OpenReadAsync("images/logo.png", 0));
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        var canceled = manifest.OpenReadAsync("images/logo.png", 1024, cancellation.Token);
        Check((await Fails<OperationCanceledException>(() => canceled)).CancellationToken == cancellation.Token && canceled.IsCanceled,
            "External cancellation has a canceled task and original token.");
        await using var second = await manifest.OpenReadAsync("images/logo.png", 1024);
        Check(second.Content.ReadByte() == first[0], "Each open has an independent position and lifetime.");
    }

    private static async Task OwnershipAndFailures()
    {
        var assembly = new FixtureAssembly(["Xui.Asset.data/item"]);
        var manifest = new PackagedAssetManifest(assembly);
        using var preCanceled = new CancellationTokenSource();
        preCanceled.Cancel();
        await Fails<OperationCanceledException>(() => manifest.OpenReadAsync("data/item", 3, preCanceled.Token));
        Check(assembly.Opens == 0, "Canceled open never touches the provider.");
        Throws<ArgumentOutOfRangeException>(() => manifest.OpenReadAsync("data/item", -1));
        Check(assembly.Opens == 0, "Invalid limits fail before provider access.");
        await Fails<FileNotFoundException>(() => manifest.OpenReadAsync("data/missing", 3));
        Check(assembly.Opens == 0, "Missing ids cannot open arbitrary resource names.");
        assembly.Factory = () => null;
        await Fails<InvalidDataException>(() => manifest.OpenReadAsync("data/item", 3));
        var nonreadable = new ObservedStream(Bytes) { Readable = false };
        assembly.Factory = () => nonreadable;
        await Fails<InvalidDataException>(() => manifest.OpenReadAsync("data/item", 3));
        Check(nonreadable.Disposes == 1, "An unreadable provider handle is rejected and disposed.");
        var tooLarge = new ObservedStream(Bytes) { AdvertisedLength = 4 };
        assembly.Factory = () => tooLarge;
        await Fails<IOException>(() => manifest.OpenReadAsync("data/item", 3));
        Check(tooLarge.Disposes == 1 && tooLarge.Reads == 0, "Known oversize is rejected and disposed without reading.");
        var unread = new ObservedStream(Bytes);
        assembly.Factory = () => unread;
        var owner = await manifest.OpenReadAsync("data/item", 3);
        Check(unread.Reads == 0, "Opening never eagerly reads bytes.");
        owner.Dispose();
        await owner.DisposeAsync();
        Check(unread.Disposes == 1, "Content/file owner disposal releases exactly once.");
        Throws<ObjectDisposedException>(() => owner.Content.ReadByte());
        var unknown = new ObservedStream([1, 2, 3, 4]) { Seekable = false };
        assembly.Factory = () => unknown;
        await using (var selected = await manifest.OpenReadAsync("data/item", 3))
        {
            Check(selected.Length is null, "Unknown provider length stays unknown.");
            await Fails<IOException>(async () => { _ = await selected.Content.ReadAsync(new byte[3]); });
            Throws<IOException>(() => selected.Content.ReadByte());
            Check(unknown.Reads == 2 && unknown.Position == 4, "Unknown oversize consumes only budget plus one probe, then sticks.");
        }
        using var canceled = new CancellationTokenSource();
        var late = new ObservedStream(Bytes);
        assembly.Factory = () => { canceled.Cancel(); return late; };
        await Fails<OperationCanceledException>(() => manifest.OpenReadAsync("data/item", 3, canceled.Token));
        Check(late.Disposes == 1, "Cancellation during provider open releases the late handle.");
        var failure = new ObservedStream(Bytes) { LengthError = new IOException("metadata"), DisposeError = new IOException("cleanup") };
        assembly.Factory = () => failure;
        var aggregate = await Fails<AggregateException>(() => manifest.OpenReadAsync("data/item", 3));
        Check(aggregate.InnerExceptions.Select(e => e.Message).SequenceEqual(new[] { "metadata", "cleanup" }),
            "Provider and cleanup errors both survive failed ownership transfer.");
    }

    private sealed class FixtureAssembly(string[] names) : Assembly
    {
        public Func<Stream?> Factory { get; set; } = () => new MemoryStream(Bytes);
        public int Opens { get; private set; }
        public override string[] GetManifestResourceNames() => names;
        public override Stream? GetManifestResourceStream(string name) { Opens++; return Factory(); }
    }

    private sealed class ObservedStream(byte[] bytes) : MemoryStream(bytes)
    {
        public bool Readable { get; init; } = true;
        public bool Seekable { get; init; } = true;
        public long? AdvertisedLength { get; init; }
        public Exception? LengthError { get; init; }
        public Exception? DisposeError { get; init; }
        public int Reads { get; private set; }
        public int Disposes { get; private set; }
        public override bool CanSeek => Seekable;
        public override bool CanRead => Readable && base.CanRead;
        public override long Length => LengthError is not null ? throw LengthError : AdvertisedLength ?? base.Length;
        public override int Read(byte[] buffer, int offset, int count) { Reads++; return base.Read(buffer, offset, count); }
        public override int Read(Span<byte> buffer)
        {
            var bytes = new byte[buffer.Length];
            int count = Read(bytes, 0, bytes.Length);
            bytes.AsSpan(0, count).CopyTo(buffer);
            return count;
        }
        public override ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(Read(buffer.Span));
        }
        protected override void Dispose(bool disposing)
        {
            if (disposing) Disposes++;
            base.Dispose(disposing);
            if (DisposeError is not null) throw DisposeError;
        }
    }
}

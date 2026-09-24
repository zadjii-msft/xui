using Xui.Experimental.Portable;

namespace AssetConsumer;

public static class AssetProof
{
    public static async Task VerifyAsync(CancellationToken cancellationToken = default)
    {
        byte[] expected = Convert.FromBase64String("__ASSET_BYTES_BASE64__");
        var manifest = new PackagedAssetManifest(typeof(AssetProof).Assembly);
        if (manifest.Assets.Count != 4) throw new InvalidDataException("Expected exactly four packaged assets.");
        foreach (var descriptor in manifest.Assets)
        {
            await using var file = await manifest.OpenReadAsync(descriptor.Id, expected.Length, cancellationToken);
            if (file.Length != expected.Length || file.Content.CanSeek || file.Content.CanWrite)
                throw new InvalidDataException("Wrong packaged asset length or ownership contract.");
            var chunk = new byte[127];
            int offset = 0;
            while (true)
            {
                int count = await file.Content.ReadAsync(chunk, cancellationToken);
                if (count == 0) break;
                if (!chunk.AsSpan(0, count).SequenceEqual(expected.AsSpan(offset, count)))
                    throw new InvalidDataException("Embedded asset bytes differ across the package boundary.");
                offset += count;
            }
            if (offset != expected.Length) throw new InvalidDataException("An embedded asset was silently truncated.");
        }
    }
}

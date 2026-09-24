namespace Xui.Experimental.Portable;

public enum PackagedImageFormat { Png, Jpeg }

/// <summary>An explicit assembly asset, never a disk path or network URL.</summary>
public sealed class PackagedImageSource
{
    public PackagedAssetManifest Manifest { get; }
    public string AssetId { get; }

    public PackagedImageSource(PackagedAssetManifest manifest, string assetId)
    {
        Manifest = manifest ?? throw new ArgumentNullException(nameof(manifest));
        manifest.Get(assetId);
        AssetId = assetId;
    }
}

/// <summary>Decode limits in physical pixels. These do not set an element's layout size.</summary>
public sealed class ImageDecodeOptions
{
    public const int EncodedByteLimit = 32 * 1024 * 1024;
    public const int SourceDimensionLimit = 16384;
    public const long SourcePixelLimit = 16 * 1024 * 1024;
    public const int OutputDimensionLimit = 1024;
    public int OutputWidth { get; }
    public int OutputHeight { get; }
    public int MaximumEncodedBytes { get; }

    public ImageDecodeOptions(int outputWidth = 192, int outputHeight = 144,
        int maximumEncodedBytes = EncodedByteLimit)
    {
        if (outputWidth is < 1 or > OutputDimensionLimit) throw new ArgumentOutOfRangeException(nameof(outputWidth));
        if (outputHeight is < 1 or > OutputDimensionLimit) throw new ArgumentOutOfRangeException(nameof(outputHeight));
        if (maximumEncodedBytes is < 1 or > EncodedByteLimit) throw new ArgumentOutOfRangeException(nameof(maximumEncodedBytes));
        OutputWidth = outputWidth;
        OutputHeight = outputHeight;
        MaximumEncodedBytes = maximumEncodedBytes;
    }
}

/// <summary>Header preflight, not a decoded image or proof that a codec accepts the payload.</summary>
public sealed class ImageDecodePlan
{
    public PackagedImageFormat Format { get; }
    public string ContentType => Format == PackagedImageFormat.Png ? "image/png" : "image/jpeg";
    public int SourceWidth { get; }
    public int SourceHeight { get; }
    public int OutputWidth { get; }
    public int OutputHeight { get; }
    public long SourcePixels => (long)SourceWidth * SourceHeight;
    public long OutputPixelBytes => (long)OutputWidth * OutputHeight * 4;

    internal ImageDecodePlan(PackagedImageFormat format, int sourceWidth, int sourceHeight, ImageDecodeOptions options)
    {
        if (sourceWidth is < 1 or > ImageDecodeOptions.SourceDimensionLimit ||
            sourceHeight is < 1 or > ImageDecodeOptions.SourceDimensionLimit ||
            (long)sourceWidth * sourceHeight > ImageDecodeOptions.SourcePixelLimit)
            throw new InvalidDataException("Image source dimensions exceed the supported pixel limits.");
        Format = format;
        SourceWidth = sourceWidth;
        SourceHeight = sourceHeight;
        if (sourceWidth <= options.OutputWidth && sourceHeight <= options.OutputHeight)
        {
            OutputWidth = sourceWidth;
            OutputHeight = sourceHeight;
        }
        else if ((long)sourceWidth * options.OutputHeight >= (long)sourceHeight * options.OutputWidth)
        {
            OutputWidth = options.OutputWidth;
            OutputHeight = Math.Max(1, (int)((long)sourceHeight * OutputWidth / sourceWidth));
        }
        else
        {
            OutputHeight = options.OutputHeight;
            OutputWidth = Math.Max(1, (int)((long)sourceWidth * OutputHeight / sourceHeight));
        }
    }

    /// <summary>Call after codec metadata inspection and before codec pixel allocation.</summary>
    public void ValidateCodecSource(int width, int height)
    {
        if (width != SourceWidth || height != SourceHeight)
            throw new InvalidDataException("Codec dimensions disagree with the bounded image preflight.");
    }

    /// <summary>Reject unexpected native output instead of publishing success with different dimensions.</summary>
    public void ValidateDecodedOutput(int width, int height)
    {
        if (width != OutputWidth || height != OutputHeight)
            throw new InvalidDataException("The decoded image does not match the reserved output dimensions.");
    }

    /// <summary>Reject a plan from a different decode request, including a smaller but otherwise valid output.</summary>
    /// <remarks>Encoded-byte limits and source identity must still be checked by the encoded lease and request generation.</remarks>
    public void ValidateOptions(ImageDecodeOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        var expected = new ImageDecodePlan(Format, SourceWidth, SourceHeight, options);
        if (OutputWidth != expected.OutputWidth || OutputHeight != expected.OutputHeight)
            throw new InvalidDataException("The image plan does not match the current decode options.");
    }

    public static ImageDecodePlan Inspect(ReadOnlySpan<byte> encoded, ImageDecodeOptions options,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        cancellationToken.ThrowIfCancellationRequested();
        if (encoded.IsEmpty || encoded.Length > options.MaximumEncodedBytes)
            throw new InvalidDataException("An encoded image must be nonempty and within its configured byte limit.");
        return ImageResourceHeaders.Inspect(encoded, options, cancellationToken);
    }
}

/// <summary>Explicit rejection when resident/pinned resources exhaust an owned image budget.</summary>
public sealed class ImageResourceLimitException(string message) : IOException(message);

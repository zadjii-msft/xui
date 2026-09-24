namespace Xui.Experimental.Portable;

public enum ImageLoadState { Empty, Loading, Ready, Error }

public interface IImageElementPeer : IElementPeer
{
    void ValidateImage(PackagedImageSource? source, ImageDecodeOptions options);
    void CancelImage();
}

public interface IImageControlEvents : IControlEvents
{
    bool ImageCompleted(long generation, ImageDecodePlan plan, int pixelWidth, int pixelHeight);
    bool ImageFailed(long generation, string message);
}

/// <summary>Optional terminal reporting for unrecoverable native image presentation failures.</summary>
public interface IImagePresentationEvents : IImageControlEvents
{
    /// <summary>Detaches a current Loading/Ready request's attachment and throws the failure after cleanup.</summary>
    /// <remarks>
    /// Deliver on the owning UI thread after native operations, construction, reconciliation and
    /// viewport staging unwind. Stale attachments, peers and requests return false.
    /// A current failure never returns true: the backend must catch and report the exception
    /// at its managed async boundary, including aggregated cleanup errors, without crossing a native ABI.
    /// Ordinary asset/codec failures still use ImageFailed while Loading.
    /// </remarks>
    bool ImagePresentationFailed(long generation, Exception error);
}

public sealed class Image : Control
{
    private PackagedImageSource? source;
    private ImageDecodeOptions options = new();
    private long generation;
    private ImageLoadState state;
    private string? error;
    private int pixelWidth, pixelHeight;
    private Action<ImageLoadState>? stateChanged;

    public PackagedImageSource? Source { get => Read(source); set => SetImage(value, options); }
    public ImageDecodeOptions DecodeOptions { get => Read(options); set => SetImage(source, value); }
    public long SourceGeneration => Read(generation);
    public ImageLoadState State => Read(state);
    public string? ErrorMessage => Read(error);
    public int PixelWidth => Read(pixelWidth);
    public int PixelHeight => Read(pixelHeight);
    public event Action<ImageLoadState> StateChanged
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); stateChanged += value; }
        remove { Owner.VerifyEventRemoval(this); stateChanged -= value; }
    }

    internal Image(Host host, string name) : base(host, ElementKind.Image, name) { }

    public Image SetImage(PackagedImageSource? value, ImageDecodeOptions decodeOptions)
        => Owner.ImageOperation(() => SetImageCore(value, decodeOptions));

    private Image SetImageCore(PackagedImageSource? value, ImageDecodeOptions decodeOptions)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        ArgumentNullException.ThrowIfNull(decodeOptions);
        if (SameSource(source, value) && SameOptions(options, decodeOptions)) return this;
        long nextGeneration = checked(generation + 1);
        Owner.ValidateImageSupport(this, value, decodeOptions);
        source = value;
        options = decodeOptions;
        generation = nextGeneration;
        ResetState();
        Owner.Update(this, ElementProperty.ImageRequest);
        Owner.QueueImageState(this);
        return this;
    }

    internal static bool SameSource(PackagedImageSource? left, PackagedImageSource? right) =>
        ReferenceEquals(left, right) || left is not null && right is not null &&
        ReferenceEquals(left.Manifest, right.Manifest) && string.Equals(left.AssetId, right.AssetId, StringComparison.Ordinal);
    internal static bool SameOptions(ImageDecodeOptions left, ImageDecodeOptions right) =>
        left.OutputWidth == right.OutputWidth && left.OutputHeight == right.OutputHeight &&
        left.MaximumEncodedBytes == right.MaximumEncodedBytes;

    internal void BeginAttachment()
    {
        generation = checked(generation + 1);
        ResetState();
    }
    internal void EndAttachment()
    {
        // Attachment identity also rejects stale callbacks; terminal cleanup cannot overflow.
        if (generation != long.MaxValue) generation++;
        ResetState();
    }
    private void ResetState()
    {
        state = source is null ? ImageLoadState.Empty : ImageLoadState.Loading;
        error = null;
        pixelWidth = pixelHeight = 0;
    }
    internal bool Awaiting(long value) => source is not null && generation == value && state == ImageLoadState.Loading;
    internal void Complete(ImageDecodePlan plan, int width, int height)
    {
        ArgumentNullException.ThrowIfNull(plan);
        plan.ValidateOptions(options);
        plan.ValidateDecodedOutput(width, height);
        pixelWidth = width;
        pixelHeight = height;
        error = null;
        state = ImageLoadState.Ready;
    }
    internal void Fail(string message)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(message);
        Values.Text(message);
        if (message.Length > 4096) throw new ArgumentOutOfRangeException(nameof(message), "Image errors support at most 4096 UTF-16 code units.");
        state = ImageLoadState.Error;
        error = message;
        pixelWidth = pixelHeight = 0;
    }
    internal void NotifyState() => stateChanged?.Invoke(state);
    internal override void Release()
    {
        stateChanged = null;
        source = null;
        ResetState();
        base.Release();
    }
}

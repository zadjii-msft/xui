using Android.Views;
using Android.Views.Accessibility;
using Android.Widget;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal sealed partial class EnabledScrollView
{
    private NativeVirtualViewport? virtualLease;
    private bool virtualRetired;
    private readonly NativeMeasureContext scrollMeasureContext = new();
    private bool previousSmoothScrolling;
    internal bool HasVirtualViewport => virtualLease is not null || virtualRetired;
    internal NativeVirtualViewport? VirtualLease => virtualLease;
    public void MeasureWith(MeasureConstraint width, MeasureConstraint height) => scrollMeasureContext.Measure(this, width, height);

    internal IVirtualViewportLease BeginVirtualViewport(AndroidBackend backend, int itemCount, float rowHeight,
        long sourceVersion, Action<VirtualViewportRequest> requested)
    {
        if (virtualLease is not null) throw new InvalidOperationException("The Android scroll already owns a virtual viewport.");
        var lease = new NativeVirtualViewport(this, backend, itemCount, rowHeight, sourceVersion, requested);
        virtualLease = lease;
        virtualRetired = false;
        previousSmoothScrolling = SmoothScrollingEnabled;
        SmoothScrollingEnabled = false;
        PublishViewport(0, 0, 0);
        RequestLayout();
        return lease;
    }

    internal void ReleaseVirtualViewport(NativeVirtualViewport lease)
    {
        if (virtualLease != lease) return;
        virtualLease = null;
        virtualRetired = true;
        SmoothScrollingEnabled = previousSmoothScrolling;
        SetMeasuredDimension(0, 0);
        Layout(Left, Top, Left, Top);
        RequestLayout();
    }

    protected override void OnMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        var (width, height) = scrollMeasureContext.Read(widthMeasureSpec, heightMeasureSpec);
        if (virtualRetired) { SetMeasuredDimension(0, 0); return; }
        if (virtualLease is not { } lease) { base.OnMeasure(widthMeasureSpec, heightMeasureSpec); return; }
        if (width.Mode == MeasureMode.Unspecified || height.Mode == MeasureMode.Unspecified)
            throw new InvalidOperationException("An Android virtual viewport requires bounded native width and height.");
        lease.MeasureCommittedContent();
        SetMeasuredDimension(Math.Min(lease.CommittedWidth, NativeMeasure.Allocation(width.Size)),
            Math.Min(lease.CommittedHeight, NativeMeasure.Allocation(height.Size)));
    }

    internal void ArrangeVirtualViewport(int width, int height)
    {
        if (virtualRetired) { SetMeasuredDimension(0, 0); return; }
        var lease = virtualLease ?? throw new InvalidOperationException("The native viewport lease has been retired.");
        // Intrinsic measurement probes are not available viewport space; only the final parent allocation is.
        lease.MeasureAvailable(width, height);
        lease.MeasureCommittedContent();
        SetMeasuredDimension(lease.CommittedWidth, lease.CommittedHeight);
    }

    protected override void OnLayout(bool changed, int left, int top, int right, int bottom)
    {
        if (virtualRetired) return;
        if (virtualLease is not { } lease) { base.OnLayout(changed, left, top, right, bottom); return; }
        lease.LayoutCommittedContent();
    }

    public override void ScrollTo(int x, int y)
    {
        if (virtualRetired) return;
        if (virtualLease is not { } lease) { base.ScrollTo(x, y); return; }
        lease.RequestNativeOffset(y, "ScrollTo");
    }

    protected override void OnOverScrolled(int scrollX, int scrollY, bool clampedX, bool clampedY)
    {
        if (virtualRetired) return;
        if (virtualLease is not { } lease) { base.OnOverScrolled(scrollX, scrollY, clampedX, clampedY); return; }
        lease.RequestNativeOffset(scrollY, "OverScrolled");
    }

    public override void Fling(int velocityY)
    {
        if (virtualRetired) return;
        if (virtualLease is not { } lease) { base.Fling(velocityY); return; }
        lease.Fling(velocityY);
    }

    public override void ComputeScroll()
    {
        if (virtualRetired) return;
        if (virtualLease is not { } lease) { base.ComputeScroll(); return; }
        lease.ComputeAnimation();
    }

    protected override void OnSizeChanged(int width, int height, int oldWidth, int oldHeight)
    {
        if (virtualLease is not { } lease) { base.OnSizeChanged(width, height, oldWidth, oldHeight); return; }
        lease.SuppressNativeAdjustment(() => base.OnSizeChanged(width, height, oldWidth, oldHeight));
    }

    protected override int ComputeVerticalScrollRange() => virtualLease?.CommittedExtent ?? base.ComputeVerticalScrollRange();
    protected override int ComputeVerticalScrollExtent() => virtualLease?.CommittedHeight ?? base.ComputeVerticalScrollExtent();
    protected override int ComputeVerticalScrollOffset() => virtualLease?.CommittedOffset ?? base.ComputeVerticalScrollOffset();

    internal void PublishViewport(int width, int height, int offset)
    {
        SetMeasuredDimension(width, height);
        Layout(Left, Top, Left + width, Top + height);
        base.ScrollTo(0, offset);
        Invalidate();
    }

    public override void OnInitializeAccessibilityNodeInfo(AccessibilityNodeInfo? info)
    {
        base.OnInitializeAccessibilityNodeInfo(info);
        if (info is null || virtualLease is not { } lease) return;
        using var collection = OperatingSystem.IsAndroidVersionAtLeast(33)
            ? new AccessibilityNodeInfo.CollectionInfo(lease.CommittedCount, 1, false, (int)SelectionMode.None)
            : AccessibilityNodeInfo.CollectionInfo.Obtain(lease.CommittedCount, 1, false, SelectionMode.None);
        info.SetCollectionInfo(collection);
        info.Extras?.PutLong("Xui.Virtual.SourceVersion", lease.CommittedSourceVersion);
    }
}

internal sealed class NativeVirtualViewport : ISettledVirtualViewportLease
{
    private readonly EnabledScrollView view;
    private readonly AndroidBackend backend;
    private readonly float density;
    private readonly float rowHeight;
    private readonly int pitch;
    private readonly OverScroller animation;
    private Action<VirtualViewportRequest>? callback;
    private int requestedCount;
    private long requestedSourceVersion;
    private int availableWidth;
    private int availableHeight;
    private bool measured;
    private bool posted;
    private bool dirty;
    private bool disposed;
    private bool suppressNativeScroll;
    private long epoch;
    private VirtualViewportRequest? latest;
    private VirtualViewportRequest? reserved;
    private int reservedCount;
    private int reservedOffset;
    private int reservedWidth;
    private int reservedHeight;
    private int reservedExtent;
    private VirtualViewportRect committedRect;
    private View? lastFocusedEditor;
    private bool lastComposing;
    internal int IntentOffset { get; private set; }
    internal int CommittedOffset { get; private set; }
    internal int CommittedWidth { get; private set; }
    internal int CommittedHeight { get; private set; }
    internal int CommittedExtent { get; private set; }
    internal int CommittedCount { get; private set; }
    internal long CommittedSourceVersion { get; private set; }
    internal long CommittedEpoch { get; private set; }
    internal long LastFlushedEpoch { get; private set; }
    internal int DeliveredRequestCount { get; private set; }
    internal bool IsActive => !disposed;
    internal bool ResourcesReleased => disposed && animation.Handle == IntPtr.Zero;
    internal Action<long, long, long, long>? TraceCommit { get; set; }
    internal Action<string>? TraceRequests { get; set; }

    internal NativeVirtualViewport(EnabledScrollView view, AndroidBackend backend, int itemCount, float rowHeight,
        long sourceVersion, Action<VirtualViewportRequest> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        this.view = view;
        this.backend = backend;
        this.callback = callback;
        density = view.Resources!.DisplayMetrics!.Density;
        this.rowHeight = rowHeight;
        if (!float.IsFinite(rowHeight) || rowHeight <= 0) throw new ArgumentOutOfRangeException(nameof(rowHeight));
        pitch = LayoutMath.Pixels(rowHeight, density);
        if (pitch <= 0 || Math.Abs((double)pitch / density - rowHeight) > 0.0001)
            throw new ArgumentOutOfRangeException(nameof(rowHeight), "Choose a row pitch that is exactly representable in native pixels.");
        Extent(itemCount);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(sourceVersion);
        requestedCount = itemCount;
        requestedSourceVersion = sourceVersion;
        animation = new OverScroller(view.Context!);
        lastFocusedEditor = backend.FocusedEditor;
        lastComposing = backend.HasComposition;
        backend.InteractionChanged += InteractionChanged;
    }

    private void Verify()
    {
        backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
    }

    private int Extent(int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        long extent = (long)count * pitch;
        if (extent > LayoutMath.MaxDimension)
            throw new ArgumentOutOfRangeException(nameof(count), "The virtual extent exceeds Android's native measured-dimension limit.");
        return (int)extent;
    }

    private ElementFrame Content => view.GetChildAt(0) as ElementFrame
        ?? throw new InvalidOperationException("The leased Android scroll requires one retained element root.");

    internal void MeasureAvailable(int width, int height)
    {
        Verify();
        if (!measured || availableWidth != width || availableHeight != height)
        {
            measured = true;
            availableWidth = width;
            availableHeight = height;
            // Shrink publishes only the already-realized intersection; growth waits for commit.
            CommittedWidth = Math.Min(CommittedWidth, width);
            CommittedHeight = Math.Min(CommittedHeight, height);
            IntentOffset = Math.Clamp(IntentOffset, 0, Math.Max(0, Extent(requestedCount) - height));
            Request();
        }
    }

    internal void MeasureCommittedContent()
    {
        bool previous = suppressNativeScroll;
        suppressNativeScroll = true;
        try { Content.MeasureWith(MeasureConstraint.Exactly(CommittedWidth), MeasureConstraint.Exactly(CommittedExtent, true)); }
        finally { suppressNativeScroll = previous; }
    }

    internal void LayoutCommittedContent()
    {
        bool previous = suppressNativeScroll;
        suppressNativeScroll = true;
        try { Content.Layout(0, 0, Content.MeasuredWidth, Content.MeasuredHeight); }
        finally { suppressNativeScroll = previous; }
    }

    internal void RequestNativeOffset(float pixels, string reason = "Native")
    {
        Verify();
        if (!float.IsFinite(pixels)) throw new ArgumentOutOfRangeException(nameof(pixels));
        if (suppressNativeScroll) return;
        animation.AbortAnimation();
        SetOffset(pixels, reason);
    }

    internal void Fling(int velocity)
    {
        Verify();
        animation.Fling(0, CommittedOffset, 0, velocity, 0, 0, 0, Math.Max(0, Extent(requestedCount) - availableHeight));
        view.PostInvalidateOnAnimation();
    }

    internal void StopAnimation()
    {
        Verify();
        animation.AbortAnimation();
    }

    internal void ComputeAnimation()
    {
        Verify();
        if (!animation.ComputeScrollOffset()) return;
        SetOffset(animation.CurrY, "Fling");
        if (!animation.IsFinished) view.PostInvalidateOnAnimation();
    }

    internal void SuppressNativeAdjustment(System.Action adjustment)
    {
        bool previous = suppressNativeScroll;
        suppressNativeScroll = true;
        try { adjustment(); }
        finally { suppressNativeScroll = previous; }
    }

    private void SetOffset(double pixels, string reason)
    {
        int next = (int)Math.Clamp(Math.Round((double)pixels, MidpointRounding.AwayFromZero),
            0, Math.Max(0, Extent(requestedCount) - availableHeight));
        if (next == IntentOffset) return;
        TraceRequests?.Invoke($"Virtual intent {reason}: {IntentOffset} -> {next}px, committed={CommittedOffset}px.");
        IntentOffset = next;
        Request();
    }

    public void RequestOffset(float offset)
    {
        Verify();
        if (!float.IsFinite(offset) || offset < 0) throw new ArgumentOutOfRangeException(nameof(offset));
        animation.AbortAnimation();
        SetOffset(Math.Min((double)offset * density, Extent(requestedCount)), "Explicit");
        Request();
    }

    public void SetExtent(int itemCount, long sourceVersion)
    {
        Verify();
        Extent(itemCount);
        if (sourceVersion <= requestedSourceVersion) throw new ArgumentOutOfRangeException(nameof(sourceVersion));
        animation.AbortAnimation();
        requestedCount = itemCount;
        requestedSourceVersion = sourceVersion;
        IntentOffset = Math.Min(IntentOffset, Math.Max(0, Extent(itemCount) - availableHeight));
        Request();
    }

    private void InteractionChanged()
    {
        if (disposed) return;
        var focused = backend.FocusedEditor;
        bool composing = backend.HasComposition;
        if (ReferenceEquals(focused, lastFocusedEditor) && composing == lastComposing) return;
        lastFocusedEditor = focused;
        lastComposing = composing;
        if (measured) Request();
    }

    private void Request()
    {
        dirty = true;
        if (disposed || !measured || posted || reserved is not null) return;
        posted = true;
        try { backend.Dispatcher.Post(DeliverRequest); }
        catch { posted = false; throw; }
    }

    private void DeliverRequest()
    {
        posted = false;
        if (disposed || !dirty || reserved is not null) return;
        dirty = false;
        var committed = committedRect with { Width = CommittedWidth / density, Height = CommittedHeight / density };
        float requestedHeight = availableHeight / density;
        float requestedExtent = requestedCount * rowHeight;
        float offset = Math.Min(IntentOffset / density, Math.Max(0, requestedExtent - requestedHeight));
        var requested = new VirtualViewportRect(offset, availableWidth / density, requestedHeight, requestedExtent);
        latest = new VirtualViewportRequest(checked(++epoch), CommittedSourceVersion, requestedSourceVersion,
            committed, requested, backend.HasComposition);
        DeliveredRequestCount++;
        try { callback!(latest.Value); }
        catch (Exception error)
        {
            global::Android.Util.Log.Error("Xui.Android", error.ToString());
            throw;
        }
    }

    public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch)
    {
        Verify();
        if (reserved is not null) throw new InvalidOperationException("Complete the reserved native viewport update first.");
        if (latest is not { } request || request.Epoch != expectedEpoch || dirty)
            return VirtualViewportUpdateResult.Superseded;
        if (request.IsBlocked || backend.HasComposition) return VirtualViewportUpdateResult.Blocked;
        reserved = request;
        reservedCount = requestedCount;
        reservedWidth = availableWidth;
        reservedHeight = availableHeight;
        reservedOffset = IntentOffset;
        reservedExtent = Extent(requestedCount);
        return VirtualViewportUpdateResult.Ready;
    }

    public VirtualViewportCommitResult TryCommit(long expectedEpoch)
    {
        Verify();
        suppressNativeScroll = true;
        try { return CommitCore(expectedEpoch); }
        finally { suppressNativeScroll = false; }
    }

    private VirtualViewportCommitResult CommitCore(long expectedEpoch)
    {
        if (reserved is not { } request || request.Epoch != expectedEpoch)
            throw new InvalidOperationException("Native commit requires the reserved viewport epoch.");
        if (backend.HasComposition)
            throw new InvalidOperationException("Composition started inside synchronous native viewport staging.");
        long started = TraceCommit is null ? 0 : System.Diagnostics.Stopwatch.GetTimestamp();
        var content = Content;
        content.MeasureWith(MeasureConstraint.Exactly(reservedWidth), MeasureConstraint.Exactly(reservedExtent, true));
        content.Layout(0, 0, content.MeasuredWidth, content.MeasuredHeight);
        if (content.MeasuredWidth != reservedWidth || content.MeasuredHeight != reservedExtent)
            throw new InvalidOperationException("The staged native content does not match its reserved width and declared extent.");
        long measuredAt = started == 0 ? 0 : System.Diagnostics.Stopwatch.GetTimestamp();
        ValidateCoverage(content, request.RequestedSourceVersion, reservedCount, reservedOffset, reservedWidth, reservedHeight);
        long validatedAt = started == 0 ? 0 : System.Diagnostics.Stopwatch.GetTimestamp();
        CommittedCount = reservedCount;
        CommittedSourceVersion = request.RequestedSourceVersion;
        CommittedEpoch = expectedEpoch;
        CommittedOffset = reservedOffset;
        CommittedWidth = reservedWidth;
        CommittedHeight = reservedHeight;
        CommittedExtent = reservedExtent;
        committedRect = request.Requested;
        reserved = null;
        view.PublishViewport(CommittedWidth, CommittedHeight, CommittedOffset);
        if (view.ScrollY != CommittedOffset) throw new InvalidOperationException("Android did not publish the exact reserved viewport offset.");
        if (started != 0) TraceCommit?.Invoke(started, measuredAt, validatedAt, System.Diagnostics.Stopwatch.GetTimestamp());
        if (dirty) Request();
        return VirtualViewportCommitResult.Committed;
    }

    public void FlushCommitted(long expectedEpoch)
    {
        Verify();
        suppressNativeScroll = true;
        try { FlushCore(expectedEpoch); }
        finally { suppressNativeScroll = false; }
    }

    private void FlushCore(long expectedEpoch)
    {
        if (reserved is not null || expectedEpoch != CommittedEpoch || expectedEpoch <= 0)
            throw new InvalidOperationException("Native flush requires the current committed epoch without an active preparation.");
        backend.FlushNativeLayout();
        var content = Content;
        if (content.IsLayoutRequested)
        {
            MeasureCommittedContent();
            LayoutCommittedContent();
        }
        if (content.MeasuredWidth != CommittedWidth || content.MeasuredHeight != CommittedExtent)
            throw new InvalidOperationException("Post-prune native content differs from the committed geometry.");
        ValidateCoverage(content, CommittedSourceVersion, CommittedCount, CommittedOffset, CommittedWidth, CommittedHeight);
        LastFlushedEpoch = expectedEpoch;
    }

    private void ValidateCoverage(ElementFrame content, long sourceVersion, int count, int offset, int width, int height)
    {
        var rows = backend.VirtualRows(view);
        var indices = new HashSet<int>();
        var keys = new HashSet<string>(StringComparer.Ordinal);
        foreach (var row in rows)
        {
            var info = row.VirtualItem!.Value;
            if (info.SourceVersion != sourceVersion || info.Count != count ||
                !indices.Add(info.Index) || !keys.Add(info.Key))
                throw new InvalidOperationException("Native virtual row identity does not match the reserved source.");
            int top = 0;
            View? current = row;
            while (current is not null && current != content)
            {
                top = checked(top + current.Top);
                current = current.Parent as View;
            }
            if (current != content || Math.Abs((long)top - (long)info.Index * pitch) > 1 ||
                Math.Abs(row.Height - pitch) > 1)
                throw new InvalidOperationException("A native virtual row does not occupy its declared logical position and pitch.");
        }
        if (width == 0 || height == 0) return;
        int first = Math.Min(count, offset / pitch);
        int end = Math.Min(count, (int)(((long)offset + height + pitch - 1) / pitch));
        for (int index = first; index < end; index++)
            if (!indices.Contains(index)) throw new InvalidOperationException($"Visible native row {index} has not been realized for the reserved source.");
    }

    internal void ValidateItem(VirtualItemInfo info)
    {
        Verify();
        info.Validate();
        if (reserved is not { } request || info.SourceVersion != request.RequestedSourceVersion || info.Count != reservedCount)
            throw new InvalidOperationException("Native row metadata requires the reserved source and count.");
    }

    public void Cancel(long expectedEpoch)
    {
        Verify();
        if (reserved is { } request && request.Epoch != expectedEpoch)
            throw new InvalidOperationException("Cancel must identify the reserved native epoch.");
        reserved = null;
        // The canceled intent itself does not spin; genuinely newer input still gets a fresh epoch.
        if (dirty) Request();
    }

    public void Dispose()
    {
        backend.VerifyAccess();
        if (disposed) return;
        disposed = true;
        callback = null;
        lastFocusedEditor = null;
        TraceCommit = null;
        TraceRequests = null;
        reserved = null;
        backend.InteractionChanged -= InteractionChanged;
        animation.AbortAnimation();
        animation.Dispose();
        view.ReleaseVirtualViewport(this);
    }
}

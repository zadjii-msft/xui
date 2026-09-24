namespace Xui.Experimental.Portable;

/// <summary>The actual resolved native style, including inherited typography; never infer it from null.</summary>
public readonly record struct FontResourceStyle
{
    public uint Weight { get; }
    public bool Italic { get; }
    public FontResourceStyle(uint weight, bool italic = false)
    {
        if (weight is < 1 or > 1000) throw new ArgumentOutOfRangeException(nameof(weight));
        Weight = weight;
        Italic = italic;
    }
}

/// <summary>A backend-owned target for one control and every measurement/cache use of its custom font.</summary>
public interface IFontResourceBindingTarget
{
    /// <summary>Read-only preflight, including composition and clear/null policy. Must not take ownership.</summary>
    void ValidateFontResource(FontResource? resource, FontResourceStyle effectiveStyle);
    /// <summary>Apply on the native UI thread. The borrowed pin is retained by the binding, not the target.</summary>
    /// <remarks>On success, all uses of the previous registration must have retired.</remarks>
    void ApplyFontResource(FontResourcePin? pin, FontResourceStyle effectiveStyle);
    /// <summary>Terminal detach: restore all native/measurement references without a composition veto before pins may be disposed.</summary>
    /// <remarks>
    /// Be idempotent after success and retryable after partial failure. Throwing preserves pins
    /// for a cleanup retry; do not double-release already-retired references or hide a failure.
    /// </remarks>
    void ReleaseFontReferences();
}

/// <summary>Attachment-only binding with independent pins and fail-closed retention after native apply failure.</summary>
/// <remarks>
/// Backend Host hooks still own model commit, interaction deferral, and detachment on native failure.
/// Set on both resource changes and effective typography changes. A failure after Apply starts
/// retains every possibly referenced pin until ReleaseFontReferences succeeds during disposal.
/// Caller closure of the resource does not invalidate this binding's existing pin.
/// </remarks>
public sealed class FontResourceBinding : IDisposable
{
    private readonly FontResourceScope scope;
    private readonly IFontResourceBindingTarget target;
    private FontResourcePin? current;
    private FontResourcePin? pendingCleanup;
    private bool busy, faulted, disposed;
    public FontResource? Resource
    {
        get
        {
            scope.VerifyAccess();
            ObjectDisposedException.ThrowIf(disposed, this);
            return current?.Resource;
        }
    }

    public FontResourceBinding(FontResourceScope scope, IFontResourceBindingTarget target)
    {
        this.scope = scope ?? throw new ArgumentNullException(nameof(scope));
        this.target = target ?? throw new ArgumentNullException(nameof(target));
        scope.VerifyOpen();
    }

    public void Set(FontResource? resource, FontResourceStyle effectiveStyle)
    {
        scope.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (faulted) throw new InvalidOperationException("A failed native font binding must be detached before reuse.");
        if (busy) throw new InvalidOperationException("Font binding updates cannot reenter.");
        if (effectiveStyle.Weight is < 1 or > 1000) throw new ArgumentException("An explicit resolved font weight is required.", nameof(effectiveStyle));
        bool same = ReferenceEquals(current?.Resource, resource);
        if (resource is not null)
        {
            if (!ReferenceEquals(resource.Scope, scope)) throw new ArgumentException("The font belongs to another attachment.", nameof(resource));
            resource.Metadata.ValidateFace(0, effectiveStyle.Weight, effectiveStyle.Italic);
            if (!same && !resource.AcceptsNewUses) throw new ObjectDisposedException(nameof(FontResource));
        }
        if (!same && resource is not null) scope.VerifyOpen();
        busy = true;
        FontResourcePin? candidate = null;
        try
        {
            target.ValidateFontResource(resource, effectiveStyle);
            if (!same && resource is not null) candidate = resource.Acquire(scope);
            try { target.ApplyFontResource(same ? current : candidate, effectiveStyle); }
            catch
            {
                faulted = true;
                pendingCleanup = candidate;
                candidate = null;
                throw;
            }
            if (!same)
            {
                var previous = current;
                current = candidate;
                candidate = null;
                try { previous?.Dispose(); }
                catch { faulted = true; throw; }
            }
        }
        finally
        {
            try { candidate?.Dispose(); }
            finally { busy = false; }
        }
    }

    public void Dispose()
    {
        scope.VerifyAccess();
        if (disposed) return;
        if (busy) throw new InvalidOperationException("Font binding disposal cannot reenter an update.");
        busy = true;
        try
        {
            // If this fails, retain all pins: native code may still reference either registration.
            try { target.ReleaseFontReferences(); }
            catch { faulted = true; throw; }
            disposed = true;
            List<Exception>? failures = null;
            foreach (var pin in new[] { current, pendingCleanup })
            {
                try { pin?.Dispose(); }
                catch (Exception error) { (failures ??= []).Add(error); }
            }
            current = pendingCleanup = null;
            if (failures is not null) throw new AggregateException("Font binding pin cleanup failed.", failures);
        }
        finally { busy = false; }
    }
}

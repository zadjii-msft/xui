namespace Xui.Experimental.Portable;

/// <summary>A backend's verified native registration with explicit retryable retirement semantics.</summary>
/// <remarks>
/// Dispose must be idempotent after success. If retirement fails, it must retain ownership of
/// remaining native references and permit a later Dispose retry without double-releasing
/// already-retired handles. It must not borrow encoded bytes after successful disposal.
/// Unlike arbitrary IDisposable values, backend implementations must honor this stronger contract.
/// </remarks>
public interface IFontNativeRegistration : IDisposable { }

/// <summary>One UI attachment's identity and ownership boundary for registered native font resources.</summary>
/// <remarks>
/// A backend creates a new scope for each attachment. Adopt only after native face identity and
/// collision checks succeed; this helper does not register fonts or verify native rendering.
/// Closing a scope rejects new uses and cancels loading, but bound-use pins retain registrations.
/// Native peers and measurement caches must release their references before disposing their pins.
/// </remarks>
public sealed class FontResourceScope : IDisposable
{
    private readonly IUiDispatcher dispatcher;
    private readonly CancellationTokenSource lifetime = new();
    private readonly HashSet<FontResource> resources = [];
    private bool closed;
    private bool disposing;
    public CancellationToken Token { get; }
    public bool IsClosed { get { VerifyAccess(); return closed; } }

    public FontResourceScope(IUiDispatcher dispatcher)
    {
        this.dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
        VerifyAccess();
        Token = lifetime.Token;
    }

    internal void VerifyAccess()
    {
        if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Registered font resources require their attachment UI thread.");
    }

    internal void VerifyOpen()
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(closed, this);
    }

    /// <summary>Transfers both owners only on success. The caller must clean up a rejected/late registration.</summary>
    public FontResource Adopt(FontResourceLease verifiedBytes, IFontNativeRegistration nativeRegistration)
    {
        ArgumentNullException.ThrowIfNull(verifiedBytes);
        ArgumentNullException.ThrowIfNull(nativeRegistration);
        VerifyOpen();
        _ = verifiedBytes.FontBytes;
        verifiedBytes.Metadata.ValidateFace(0, 400, false);
        if (resources.Count >= FontResourceCache.MaximumFaces)
            throw new FontResourceLimitException("The attachment's registered font resource limit is full.");
        if (resources.Any(resource => resource.Owns(verifiedBytes, nativeRegistration)))
            throw new ArgumentException("Font bytes and native registration owners cannot be transferred twice.");
        var resource = new FontResource(this, verifiedBytes, nativeRegistration);
        resources.Add(resource);
        return resource;
    }

    internal void Forget(FontResource resource) => resources.Remove(resource);

    public void Dispose()
    {
        VerifyAccess();
        if (disposing) throw new InvalidOperationException("Font scope retirement cannot reenter.");
        disposing = true;
        List<Exception>? failures = null;
        try
        {
            if (!closed)
            {
                closed = true;
                try { lifetime.Cancel(); }
                catch (Exception error) { (failures ??= []).Add(error); }
                finally { lifetime.Dispose(); }
            }
            foreach (var resource in resources.ToArray())
            {
                try { resource.Dispose(); }
                catch (Exception error) { (failures ??= []).Add(new FontResourceRetirementException(resource, error)); }
            }
            if (failures is not null) throw new AggregateException("Registered font scope cleanup failed.", failures);
        }
        finally { disposing = false; }
    }
}

/// <summary>A ready native font identity owned by one attachment, not a retained model/font-family string.</summary>
/// <remarks>
/// Dispose closes new pin acquisition. Existing pins remain valid until native consumers retire.
/// The native registration is released before the verified encoded font and license lease.
/// </remarks>
public sealed class FontResource : IDisposable
{
    internal FontResourceScope Scope { get; }
    private FontResourceLease? bytes;
    private IFontNativeRegistration? registration;
    private int pins;
    private bool closed;
    private bool retiring;
    public PackagedFontSource Source { get; }
    public FontResourceMetadata Metadata { get; }
    public bool AcceptsNewUses { get { Scope.VerifyAccess(); return !closed && !Scope.IsClosed; } }

    internal FontResource(FontResourceScope scope, FontResourceLease bytes, IFontNativeRegistration registration)
    {
        Scope = scope;
        this.bytes = bytes;
        this.registration = registration;
        Source = bytes.Source;
        Metadata = bytes.Metadata;
    }

    public FontResourcePin Acquire(FontResourceScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        Scope.VerifyAccess();
        scope.VerifyOpen();
        if (!ReferenceEquals(Scope, scope)) throw new ArgumentException("Font resources cannot cross backend or attachment scopes.", nameof(scope));
        ObjectDisposedException.ThrowIf(closed, this);
        pins = checked(pins + 1);
        return new FontResourcePin(this);
    }

    internal IFontNativeRegistration Registration
    {
        get
        {
            Scope.VerifyAccess();
            return registration ?? throw new ObjectDisposedException(nameof(FontResource));
        }
    }

    internal bool Owns(FontResourceLease encoded, IFontNativeRegistration native) =>
        ReferenceEquals(bytes, encoded) || ReferenceEquals(registration, native);

    internal void ReleasePin()
    {
        Scope.VerifyAccess();
        pins--;
        if (pins == 0 && closed) Retire();
    }

    public void Dispose()
    {
        Scope.VerifyAccess();
        closed = true;
        if (pins == 0 && registration is not null) Retire();
    }

    private void Retire()
    {
        if (retiring) throw new InvalidOperationException("Native font registration retirement cannot reenter.");
        retiring = true;
        try
        {
            var native = registration;
            var encoded = bytes;
            // Native cleanup failure retains the encoded owner for a retry: a loader may still borrow it.
            native?.Dispose();
            registration = null;
            bytes = null;
            Scope.Forget(this);
            encoded?.Dispose();
        }
        finally { retiring = false; }
    }
}

/// <summary>A single native-use pin. Registration is borrowed and must not be disposed directly.</summary>
public sealed class FontResourcePin : IDisposable
{
    private FontResource? resource;
    public FontResource Resource
    {
        get
        {
            var current = resource ?? throw new ObjectDisposedException(nameof(FontResourcePin));
            current.Scope.VerifyAccess();
            return current;
        }
    }
    public IFontNativeRegistration Registration => Resource.Registration;
    internal FontResourcePin(FontResource resource) => this.resource = resource;

    public void Dispose()
    {
        var current = resource;
        if (current is null) return;
        current.Scope.VerifyAccess();
        resource = null;
        current.ReleasePin();
    }
}

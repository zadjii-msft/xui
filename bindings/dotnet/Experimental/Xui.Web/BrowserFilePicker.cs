using System.Globalization;
using Microsoft.JSInterop;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Web;

/// <summary>A native browser picker backed by a preloaded xui-filepicker.js module.</summary>
/// <remarks>
/// The caller owns the module and must retain it until all selected files are disposed.
/// Invoke OpenAsync directly in a user-gesture handler; do not import the module in that handler.
/// Selected streams support asynchronous reads only. Cancellation releases listeners and file
/// references, but cannot promise to dismiss the operating-system dialog.
/// </remarks>
public sealed class BrowserFilePicker : IFilePicker, IDisposable
{
    private readonly IJSInProcessObjectReference module;
    private readonly IUiDispatcher dispatcher;
    private readonly Action<Exception> reportError;
    private Selection? pending;
    private bool disposed;

    public BrowserFilePicker(IJSInProcessObjectReference module, IUiDispatcher dispatcher, Action<Exception> reportError)
    {
        this.module = module ?? throw new ArgumentNullException(nameof(module));
        this.dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
        this.reportError = reportError ?? throw new ArgumentNullException(nameof(reportError));
        VerifyAccess();
    }

    private void VerifyAccess()
    {
        if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Browser file pickers require the UI thread.");
    }

    public CapabilityAvailability GetAvailability(ServiceCapability capability)
    {
        PlatformServicePolicy.ValidateCapability(capability);
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        return capability == ServiceCapability.OpenFile ? CapabilityAvailability.RequiresUserGesture : CapabilityAvailability.Unsupported;
    }

    public Task<OperationResult<PickedFile>> OpenAsync(FileSelectionOptions options, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<OperationResult<PickedFile>>(cancellationToken);
        if (pending is not null)
            return Task.FromResult(OperationResult<PickedFile>.Failed(new InvalidOperationException("A browser file picker is already pending.")));
        var selection = new Selection(this, options, cancellationToken);
        pending = selection;
        selection.Start();
        return selection.Request.Task;
    }

    public void Dispose()
    {
        VerifyAccess();
        if (disposed) return;
        disposed = true;
        pending?.Cancel();
    }

    public sealed class Selection
    {
        private readonly BrowserFilePicker owner;
        private readonly FileSelectionOptions options;
        private readonly CancellationToken token;
        private DotNetObjectReference<Selection>? reference;
        private IJSInProcessObjectReference? handle;
        private CancellationTokenRegistration registration;
        private bool finished;
        internal FileSelectionRequest Request { get; }

        internal Selection(BrowserFilePicker owner, FileSelectionOptions options, CancellationToken token)
        {
            this.owner = owner;
            this.options = options;
            this.token = token;
            Request = new(token, owner.reportError);
        }

        internal void Start()
        {
            try
            {
                reference = DotNetObjectReference.Create(this);
                handle = owner.module.Invoke<IJSInProcessObjectReference>("createFilePicker", reference,
                    options.MaximumBytes.ToString(CultureInfo.InvariantCulture));
                registration = token.UnsafeRegister(static state => ((Selection)state!).CancellationRequested(), this);
                if (Request.Task.IsCompleted) { Cancel(); return; }
                handle.InvokeVoid("show");
            }
            catch (Exception error) { Complete(OperationResult<PickedFile>.Failed(error)); }
        }

        private void CancellationRequested()
        {
            if (owner.dispatcher.CheckAccess()) Cancel();
            else
            {
                try
                {
                    if (owner.dispatcher is ICancellableUiDispatcher cancellable)
                        cancellable.Post(Cancel, owner.reportError);
                    else owner.dispatcher.Post(Cancel);
                }
                catch (Exception error) { owner.reportError(error); }
            }
        }

        internal void Cancel()
        {
            Request.Dispose();
            Complete(OperationResult<PickedFile>.Cancelled());
        }

        [JSInvokable]
        public void Receive(string status, string displayName, long length, string error)
        {
            owner.VerifyAccess();
            if (finished) return;
            if (Request.Task.IsCompleted) { Cancel(); return; }
            OperationResult<PickedFile> result;
            try
            {
                if (status == "completed")
                {
                    var stream = new BrowserSelectedFileStream(handle!, owner.dispatcher);
                    var file = new PickedFile(displayName, stream, options, length);
                    handle = null;
                    result = OperationResult<PickedFile>.Completed(file);
                }
                else result = status switch
                {
                    "cancelled" => OperationResult<PickedFile>.Cancelled(),
                    "denied" => OperationResult<PickedFile>.Denied(),
                    "unsupported" => OperationResult<PickedFile>.Unsupported(),
                    "oversized" => OperationResult<PickedFile>.Failed(new FileSelectionTooLargeException(options.MaximumBytes)),
                    "failed" => OperationResult<PickedFile>.Failed(new IOException(error)),
                    _ => OperationResult<PickedFile>.Failed(new IOException($"Unknown browser picker result: {status}."))
                };
            }
            catch (Exception failure) { result = OperationResult<PickedFile>.Failed(failure); }
            Complete(result);
        }

        private void Complete(OperationResult<PickedFile> result)
        {
            if (finished)
            {
                Request.TryComplete(result);
                return;
            }
            finished = true;
            if (ReferenceEquals(owner.pending, this)) owner.pending = null;
            registration.Unregister();
            try
            {
                if (handle is not null)
                {
                    try { BrowserSelectedFileStream.ReleaseHandle(handle); }
                    finally { handle = null; }
                }
            }
            catch (Exception cleanup)
            {
                if (result.Status == OperationStatus.Completed)
                {
                    try { result.Value.Dispose(); }
                    catch (Exception fileCleanup) { cleanup = new AggregateException(cleanup, fileCleanup); }
                }
                result = OperationResult<PickedFile>.Failed(result.Error is null ? cleanup : new AggregateException(result.Error, cleanup));
            }
            finally { reference?.Dispose(); reference = null; }
            try { Request.TryComplete(result); }
            finally { Request.Dispose(); }
        }
    }
}

internal sealed class BrowserSelectedFileStream(IJSInProcessObjectReference handle, IUiDispatcher dispatcher) : Stream
{
    private bool disposed;
    public override bool CanRead => !disposed;
    public override bool CanSeek => false;
    public override bool CanWrite => false;
    public override long Length => throw new NotSupportedException();
    public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
    public override void Flush() => ObjectDisposedException.ThrowIf(disposed, this);
    public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException("Browser selected files require asynchronous reads.");
    public override int Read(Span<byte> buffer) => throw new NotSupportedException("Browser selected files require asynchronous reads.");
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();

    public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
    {
        if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Browser file reads require the UI thread.");
        ObjectDisposedException.ThrowIf(disposed, this);
        cancellationToken.ThrowIfCancellationRequested();
        if (buffer.IsEmpty) return 0;
        int count = Math.Min(buffer.Length, 65536);
        byte[] bytes = await handle.InvokeAsync<byte[]>("read", cancellationToken, count);
        cancellationToken.ThrowIfCancellationRequested();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (bytes.Length > count) throw new IOException("The browser file provider exceeded the requested chunk size.");
        bytes.CopyTo(buffer);
        return bytes.Length;
    }

    internal static void ReleaseHandle(IJSInProcessObjectReference handle)
    {
        Exception? failure = null;
        try { handle.InvokeVoid("release"); }
        catch (Exception error) { failure = error; }
        try { handle.Dispose(); }
        catch (Exception cleanup)
        {
            if (failure is not null) throw new AggregateException(failure, cleanup);
            throw;
        }
        if (failure is not null) System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing && !disposed)
        {
            if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Dispose browser selected files on the UI thread.");
            disposed = true;
            ReleaseHandle(handle);
        }
        base.Dispose(disposing);
    }
}

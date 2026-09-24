using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed class PlatformServicesWorkbenchController : IDisposable
{
    private sealed record Outcome(OperationStatus Status, int? ClipboardLength = null,
        string FileName = "", int? FileBytes = null, bool TooLarge = false);

    private readonly Host host;
    private readonly IPlatformServices services;
    private readonly IFilePicker picker;
    private readonly Action<Exception> reportUnhandled;
    private readonly UiWorkScope work;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private PlatformServicesWorkbenchState state = new();
    private Action<PlatformServicesWorkbenchState>? render;
    private CancellationToken lifetime;
    private CancellationTokenSource? active;
    private volatile bool disposed;
    public Task LastOperation { get; private set; } = Task.CompletedTask;
    public PlatformServicesWorkbenchState State { get { Verify(); return state; } }

    // Keep caller-owned providers alive until LastOperation settles, including after root retirement.
    // The reporter must be thread-safe, nonthrowing, and is given sanitized errors only.
    public PlatformServicesWorkbenchController(Host host, IPlatformServices services, IFilePicker picker,
        Action<Exception> reportUnhandled)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(picker);
        ArgumentNullException.ThrowIfNull(reportUnhandled);
        host.VerifyMutation();
        this.host = host;
        this.services = services;
        this.picker = picker;
        this.reportUnhandled = reportUnhandled;
        work = new(host);
        state = Availability(state);
    }
    public void Attach(ComponentLifetime owner, Action<PlatformServicesWorkbenchState> refresh)
    {
        Verify();
        ArgumentNullException.ThrowIfNull(owner);
        ArgumentNullException.ThrowIfNull(refresh);
        if (render is not null) throw new InvalidOperationException("Services controller is already attached.");
        owner.Own(this);
        lifetime = owner.Token;
        render = refresh;
        refresh(state);
    }
    public void SetClipboardDraft(string value)
    {
        VerifyIdle();
        PlatformServicePolicy.ValidateClipboardText(value);
        Publish(state with { ClipboardDraft = value, Error = "" });
    }
    public void SetUriDraft(string value)
    {
        VerifyIdle();
        Publish(state with { UriDraft = GalleryValues.Text(value), Error = "" });
    }
    public void RefreshAvailability()
    {
        VerifyIdle();
        Publish(Availability(state) with { Status = "Capabilities refreshed. No service action has run.", Error = "" });
    }
    private PlatformServicesWorkbenchState Availability(PlatformServicesWorkbenchState current)
    {
        var clipboard = services.GetAvailability(ServiceCapability.Clipboard);
        var uri = services.GetAvailability(ServiceCapability.OpenUri);
        var file = picker.GetAvailability(ServiceCapability.OpenFile);
        _ = PlatformServicesWorkbenchState.AvailabilityText(clipboard);
        _ = PlatformServicesWorkbenchState.AvailabilityText(uri);
        _ = PlatformServicesWorkbenchState.AvailabilityText(file);
        return current with { ClipboardAvailability = clipboard, UriAvailability = uri, FileAvailability = file };
    }
    public void Reset()
    {
        VerifyIdle();
        Publish(Availability(new()) with { Status = "Workbench cleared. No service action has run." });
    }
    public void ReadClipboard()
    {
        VerifyIdle();
        if (!state.CanReadClipboard) throw new InvalidOperationException("Clipboard reading is unsupported.");
        Start("Clipboard read", async token =>
        {
            var result = await services.ReadClipboardAsync(token).ConfigureAwait(false);
            if (result.Status != OperationStatus.Completed) return new(result.Status);
            token.ThrowIfCancellationRequested();
            PlatformServicePolicy.ValidateClipboardText(result.Value);
            if (result.Value.Length > PlatformServicesWorkbenchState.MaximumClipboardCharacters)
                throw new InvalidDataException("Clipboard exceeds the workbench limit.");
            return new(OperationStatus.Completed, ClipboardLength: result.Value.Length);
        });
    }
    public void WriteClipboard()
    {
        VerifyIdle();
        if (!state.CanWriteClipboard) throw new InvalidOperationException("Clipboard write is unavailable or the draft exceeds its limit.");
        string text = state.ClipboardDraft;
        Start("Clipboard write", token => Confirm(services.WriteClipboardAsync(text, token)));
    }
    public void Launch()
    {
        VerifyIdle();
        if (!state.CanOpenUri || !PlatformServicesWorkbenchState.TryUri(state.UriDraft, out var uri))
            throw new InvalidOperationException("HTTPS launch is unavailable or the URL is invalid.");
        PlatformServicePolicy.ValidateLaunchUri(uri!);
        Start("HTTPS launch", token => Confirm(services.OpenUriAsync(uri!, token)));
    }
    private static async Task<Outcome> Confirm(Task<OperationResult<bool>> operation)
    {
        var result = await operation.ConfigureAwait(false);
        if (result.Status == OperationStatus.Completed && !result.Value)
            throw new InvalidOperationException("Provider did not confirm the requested action.");
        return new(result.Status);
    }
    public void PickFile()
    {
        VerifyIdle();
        if (!state.CanPickFile) throw new InvalidOperationException("Single-file selection is unsupported.");
        Start("File inspection", InspectFile);
    }
    private async Task<Outcome> InspectFile(CancellationToken token)
    {
        var options = new FileSelectionOptions(PlatformServicesWorkbenchState.MaximumInspectionBytes);
        // Start inside the gesture and retain the UI context through disposal: browser file streams are UI-bound.
        var result = await picker.OpenAsync(options, token);
        if (result.Status != OperationStatus.Completed) return new(result.Status);
        await using var file = result.Value;
        token.ThrowIfCancellationRequested();
        string label = PlatformServicesWorkbenchState.SafeMetadata(file.DisplayName, 120);
        byte[] buffer = new byte[4096];
        int total = 0;
        try
        {
            while (true)
            {
                token.ThrowIfCancellationRequested();
                int request = Math.Min(buffer.Length, PlatformServicesWorkbenchState.MaximumInspectionBytes + 1 - total);
                int count = await file.Content.ReadAsync(buffer.AsMemory(0, request), token);
                if (count < 0 || count > request) throw new IOException("Invalid stream read count.");
                total += count;
                if (total > PlatformServicesWorkbenchState.MaximumInspectionBytes)
                    throw new FileSelectionTooLargeException(PlatformServicesWorkbenchState.MaximumInspectionBytes);
                if (count == 0) break;
            }
            return new(OperationStatus.Completed, FileName: label, FileBytes: total);
        }
        catch (FileSelectionTooLargeException error) when (error.MaximumBytes == PlatformServicesWorkbenchState.MaximumInspectionBytes)
        {
            return new(OperationStatus.Failed, FileName: label, TooLarge: true);
        }
        finally { Array.Clear(buffer); }
    }
    public void Cancel()
    {
        Verify();
        if (active is null || !state.CanCancel) throw new InvalidOperationException("No cancellable service operation.");
        Publish(state with { CancelRequested = true, Status = "Cancel requested. Waiting for provider cleanup..." });
        active.Cancel();
    }
    private void Start(string operation, Func<CancellationToken, Task<Outcome>> produce)
    {
        VerifyIdle();
        if (render is null) throw new InvalidOperationException("Services controller is not attached.");
        var request = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        active = request;
        var starting = state with { Busy = true, CancelRequested = false, Status = operation + " in progress...", Error = "" };
        if (operation == "Clipboard read") starting = starting with { ClipboardLength = null };
        if (operation == "File inspection") starting = starting with { FileName = "", FileByteCount = null, FileTooLarge = false };
        try { Publish(starting); }
        catch { active = null; request.Dispose(); throw; }
        Task<Outcome>? production = null;
        var delivery = work.RunAsync(token => production = produce(token), result =>
        {
            active = null;
            var next = state with { Busy = false, CancelRequested = false };
            next = result.Status switch
            {
                OperationStatus.Completed => next with { Status = operation + " completed." },
                OperationStatus.Cancelled => next with { Status = operation + " dismissed by provider." },
                OperationStatus.Denied => next with { Status = operation + " denied.", Error = "Provider denied access." },
                OperationStatus.Unsupported => next with { Status = operation + " unsupported.", Error = "Provider does not support this action." },
                OperationStatus.Failed => next with { Status = operation + " failed.", Error = result.TooLarge
                    ? "File exceeds the 65536-byte inspection limit." : "Provider reported a failure. Contents are not included in diagnostics." },
                _ => throw new InvalidOperationException("Unknown service result.")
            };
            if (operation == "Clipboard read")
                next = next with { ClipboardLength = result.Status == OperationStatus.Completed ? result.ClipboardLength : null };
            if (operation == "File inspection")
                next = next with { FileName = result.FileName, FileByteCount = result.FileBytes, FileTooLarge = result.TooLarge };
            try { Publish(next); }
            catch
            {
                reportUnhandled(new InvalidOperationException("Services workbench could not render the operation result."));
                throw;
            }
        }, request.Token);
        LastOperation = Observe(delivery, () => production, request, operation);
    }
    private async Task Observe(Task delivery, Func<Task<Outcome>?> production, CancellationTokenSource request, string operation)
    {
        bool canceled = false, failed = false;
        try
        {
            try { await delivery.ConfigureAwait(false); }
            catch (OperationCanceledException) when (request.IsCancellationRequested) { canceled = true; }
            catch { failed = true; }
            if (canceled && production() is { } provider)
            {
                try { failed |= (await provider.ConfigureAwait(false)).Status == OperationStatus.Failed; }
                catch (OperationCanceledException) when (request.IsCancellationRequested) { }
                catch { failed = true; }
            }
            if (disposed || lifetime.IsCancellationRequested)
            {
                if (failed) reportUnhandled(new InvalidOperationException("Services workbench provider or resource cleanup failed after retirement."));
                return;
            }
            if (!canceled && !failed) return;
            try
            {
                await host.DispatchAsync(() =>
                {
                    if (disposed || lifetime.IsCancellationRequested) return;
                    active = null;
                    Publish(state with
                    {
                        Busy = false, CancelRequested = false,
                        Status = failed ? operation + " failed." : operation + " canceled. A completed external action cannot be undone.",
                        Error = failed ? "Provider or resource cleanup failed. Contents are not included in diagnostics." : ""
                    });
                }).ConfigureAwait(false);
            }
            catch
            {
                if (!disposed && !lifetime.IsCancellationRequested)
                    reportUnhandled(new InvalidOperationException("Services workbench could not deliver its operation outcome."));
            }
        }
        finally { Interlocked.CompareExchange(ref active, null, request); request.Dispose(); }
    }
    private void Publish(PlatformServicesWorkbenchState next) { state = next; render?.Invoke(next); }
    private void Verify() { host.VerifyMutation(); ObjectDisposedException.ThrowIf(disposed, this); }
    private void VerifyIdle()
    {
        Verify();
        if (state.Busy) throw new InvalidOperationException("Wait for the current service operation to finish.");
    }
    public void Dispose()
    {
        if (Environment.CurrentManagedThreadId != thread) throw new InvalidOperationException("Dispose workbench on its creating UI thread.");
        if (disposed) return;
        disposed = true;
        render = null;
        var failures = new List<Exception>();
        try { active?.Cancel(); } catch (Exception error) { failures.Add(error); }
        try { work.Dispose(); } catch (Exception error) { failures.Add(error); }
        if (failures.Count > 0) throw new InvalidOperationException("Services workbench cancellation or cleanup failed.");
    }
}

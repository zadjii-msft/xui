using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Xui.Experimental.Portable;

namespace PortableDemo;

public enum ProfilePage { Edit, Preview }

public sealed record ProfileWorkspaceState
{
    public ProfileSnapshot Draft { get; init; } = new();
    public ProfileSnapshot? SavedProfile { get; init; }
    public ProfilePage Page { get; init; }
    public string PageKey { get; init; } = "";
    public bool Busy { get; init; }
    public bool CancelRequested { get; init; }
    public bool Interrupted { get; init; }
    public string Status { get; init; } = "No draft loaded. Nothing is saved automatically.";
    public string Error { get; init; } = "";
    public bool CanSave => Draft.CanSave && !Busy;
    public bool CanCancel => Busy && !CancelRequested;
}

public sealed class ProfileWorkspaceController : IDisposable
{
    public const string StorageKey = "profile-draft-v1";
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private readonly Host host;
    private readonly IApplicationStorage storage;
    private readonly Action<Exception> reportUnhandled;
    private readonly NavigationStack<ProfilePage> navigation;
    private readonly UiWorkScope writes;
    private readonly LatestUiWork loads;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private CancellationToken lifetime;
    private CancellationTokenSource? active;
    private Action<ProfileWorkspaceState>? render;
    private ProfileWorkspaceState state = new();
    private volatile bool disposed;

    public Task LastOperation { get; private set; } = Task.CompletedTask;
    public Exception? LastError { get; private set; }
    public ProfileWorkspaceState State { get { VerifyAccess(); return state; } }
    public int NavigationDepth { get { VerifyAccess(); return navigation.Entries.Count; } }
    public CancellationToken CurrentPageLifetime { get { VerifyAccess(); return navigation.Current!.Lifetime; } }

    // A failed dispatcher cannot deliver errors on the UI thread; this reporter must be thread-safe and nonthrowing.
    public ProfileWorkspaceController(Host host, IApplicationStorage storage, Action<Exception> reportUnhandled,
        ProfileWorkspaceSession? session = null)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(storage);
        ArgumentNullException.ThrowIfNull(reportUnhandled);
        host.VerifyMutation();
        this.host = host;
        this.storage = storage;
        this.reportUnhandled = reportUnhandled;
        navigation = new(host);
        writes = new(host);
        loads = new(host);
        var entry = navigation.Push("edit", ProfilePage.Edit);
        state = state with { PageKey = entry.Id.ToString("N") };
        if (session is not null)
        {
            if (session.Page == ProfilePage.Preview) entry = navigation.Push("preview", ProfilePage.Preview);
            state = new()
            {
                Draft = session.Draft,
                SavedProfile = session.SavedProfile,
                Page = session.Page,
                PageKey = entry.Id.ToString("N"),
                Interrupted = session.Interrupted,
                Status = session.Interrupted
                    ? "Storage operation interrupted; Load draft to check stored data. Current edits kept."
                    : "Session restored. Stored draft was not read or changed."
            };
        }
    }

    public ProfileWorkspaceSession CaptureSession()
    {
        VerifyAccess();
        return new(1, state.Draft, state.SavedProfile, state.Page,
            state.Interrupted || state.Busy || state.CancelRequested || !LastOperation.IsCompleted);
    }

    public void Attach(ComponentLifetime owner, Action<ProfileWorkspaceState> render)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(owner);
        ArgumentNullException.ThrowIfNull(render);
        if (this.render is not null) throw new InvalidOperationException("Profile controller is already attached.");
        owner.Own(this);
        lifetime = owner.Token;
        this.render = render;
        render(state);
    }

    public void SetDisplayName(string value) => Edit(state.Draft.WithDisplayName(value));
    public void SetRole(string value) => Edit(state.Draft.WithRole(value));
    public void SetLocation(string value) => Edit(state.Draft.WithLocation(value));
    public void SetFocus(string value) => Edit(state.Draft.WithFocus(value));

    private void Edit(ProfileSnapshot draft)
    {
        VerifyIdle();
        LastError = null;
        Publish(state with { Draft = draft, Status = "Edits are not saved.", Error = "" });
    }

    public void Preview()
    {
        VerifyIdle();
        if (!state.Draft.CanSave) throw new InvalidOperationException(state.Draft.Validation);
        if (state.Page != ProfilePage.Edit) throw new InvalidOperationException("Already previewing this profile.");
        var entry = navigation.Push("preview", ProfilePage.Preview);
        try { Publish(state with { Page = ProfilePage.Preview, PageKey = entry.Id.ToString("N") }); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted)
        {
            navigation.Back();
            throw;
        }
    }

    public void Back()
    {
        VerifyIdle();
        if (!navigation.CanGoBack) throw new InvalidOperationException("There is no previous profile page.");
        var previous = navigation.Entries[^2];
        // Compose first so a rejected native mutation does not retire the current history entry.
        try { Publish(state with { Page = previous.State, PageKey = previous.Id.ToString("N") }); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted) { throw; }
        catch
        {
            navigation.Back();
            throw;
        }
        navigation.Back();
    }

    public void ResetEditor()
    {
        VerifyIdle();
        if (state.Page == ProfilePage.Preview) Back();
        LastError = null;
        Publish(state with { Draft = new(), Status = "Editor cleared. Stored draft was not changed.", Error = "" });
    }

    public void Save()
    {
        VerifyIdle();
        if (!state.Draft.CanSave) throw new InvalidOperationException(state.Draft.Validation);
        var saved = state.Draft;
        byte[] data = Utf8.GetBytes(ProfileWorkspaceCodec.Serialize(saved));
        Start("Save", token => storage.WriteAsync(StorageKey, data, token), confirmed =>
        {
            if (!confirmed) throw new InvalidOperationException("Storage did not confirm the save.");
            return state with { SavedProfile = saved, Status = "Draft saved." };
        });
    }

    public void Load() => Start("Load", token => storage.ReadAsync(StorageKey, token), value =>
    {
        if (!value.Exists)
            return state with { SavedProfile = null, Status = "No saved draft found. Current edits kept." };
        var loaded = ProfileWorkspaceCodec.Restore(Utf8.GetString(value.Data.Span));
        return state with { Draft = loaded, SavedProfile = loaded, Status = "Saved draft loaded." };
    });

    public void Delete() => Start("Delete", token => storage.DeleteAsync(StorageKey, token), confirmed =>
    {
        if (!confirmed) throw new InvalidOperationException("Storage did not confirm the deletion.");
        return state with { SavedProfile = null, Status = "Stored draft deleted. Current edits kept." };
    });

    public void Cancel()
    {
        VerifyAccess();
        if (active is null || !state.CanCancel) throw new InvalidOperationException("There is no cancellable storage operation.");
        Publish(state with { CancelRequested = true, Status = "Cancel requested. Waiting for storage..." });
        active.Cancel();
    }

    private void Start<T>(string operation, Func<CancellationToken, Task<OperationResult<T>>> produce,
        Func<T, ProfileWorkspaceState> completed)
    {
        VerifyIdle();
        if (render is null) throw new InvalidOperationException("Attach the controller before starting storage work.");
        var request = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        active = request;
        LastError = null;
        try { Publish(state with { Busy = true, CancelRequested = false, Status = operation + " in progress...", Error = "" }); }
        catch
        {
            active = null;
            request.Dispose();
            throw;
        }
        Task<OperationResult<T>>? production = null;
        Task<OperationResult<T>> Produce(CancellationToken token) =>
            production = produce(token) ?? throw new InvalidOperationException("Storage returned no task.");
        void Apply(OperationResult<T> result)
        {
            ArgumentNullException.ThrowIfNull(result);
            var next = result.Status switch
            {
                OperationStatus.Completed => completed(result.Value),
                OperationStatus.Cancelled => state with { Status = CancelMessage(operation) },
                OperationStatus.Denied => state with { Status = operation + " denied.", Error = "Storage access was denied." },
                OperationStatus.Unsupported => state with { Status = operation + " unavailable.", Error = "Application storage is not supported by this host." },
                OperationStatus.Failed => throw result.Error ?? new InvalidOperationException("Storage failed without an error."),
                _ => throw new InvalidOperationException("Unknown storage operation result.")
            };
            active = null;
            try { Publish(next with
            {
                Busy = false,
                CancelRequested = false,
                Interrupted = result.Status == OperationStatus.Completed ? false : next.Interrupted
            }); }
            catch (Exception presentationFailure)
            {
                reportUnhandled(presentationFailure);
                throw;
            }
        }
        var delivery = operation == "Load"
            ? loads.RunAsync(Produce, Apply, request.Token)
            : writes.RunAsync(Produce, Apply, request.Token);
        LastOperation = ObserveAsync(delivery, () => production, request, operation);
    }

    private async Task ObserveAsync<T>(Task delivery, Func<Task<OperationResult<T>>?> production,
        CancellationTokenSource request, string operation)
    {
        Exception? error = null;
        bool canceled = false;
        try
        {
            try { await delivery.ConfigureAwait(false); }
            catch (OperationCanceledException) when (request.IsCancellationRequested) { canceled = true; }
            catch (Exception failure) { error = failure; }
            if (canceled && production() is { } providerTask)
            {
                // UI cancellation cannot undo a provider commit or allow a second side effect to race it.
                try
                {
                    var lateResult = await providerTask.ConfigureAwait(false);
                    if (lateResult.Status == OperationStatus.Failed)
                        error = lateResult.Error ?? new InvalidOperationException("Storage failed without an error.");
                }
                catch (OperationCanceledException) when (request.IsCancellationRequested) { }
                catch (Exception failure) { error = failure; }
            }
            if ((!canceled && error is null) || disposed || lifetime.IsCancellationRequested) return;
            try
            {
                await host.DispatchAsync(() =>
                {
                    if (disposed || lifetime.IsCancellationRequested) return;
                    if (!ReferenceEquals(active, request) && active is not null) return;
                    active = null;
                    LastError = error;
                    Publish(state with
                    {
                        Busy = false,
                        CancelRequested = false,
                        Status = error is null ? CancelMessage(operation) : operation + " failed.",
                        Error = error is null ? "" : error.Message.Replace('\0', ' ')
                    });
                }).ConfigureAwait(false);
            }
            catch (Exception reportingFailure)
            {
                if (!disposed && !lifetime.IsCancellationRequested)
                    reportUnhandled(error is null ? reportingFailure : new AggregateException(error, reportingFailure));
            }
        }
        finally
        {
            Interlocked.CompareExchange(ref active, null, request);
            request.Dispose();
        }
    }

    private static string CancelMessage(string operation) => operation == "Load"
        ? "Load canceled. Current edits kept."
        : operation + " canceled. Storage may have changed; Load draft to check.";

    private void Publish(ProfileWorkspaceState next)
    {
        var previous = state;
        state = next;
        try { render?.Invoke(next); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted)
        {
            state = previous;
            throw;
        }
    }

    private void VerifyAccess()
    {
        host.VerifyMutation();
        ObjectDisposedException.ThrowIf(disposed, this);
    }
    private void VerifyIdle()
    {
        VerifyAccess();
        if (state.Busy) throw new InvalidOperationException("Wait for the current storage operation to settle.");
    }

    public void Dispose()
    {
        if (Environment.CurrentManagedThreadId != thread)
            throw new InvalidOperationException("Dispose the profile workspace on its UI thread.");
        if (disposed) return;
        disposed = true;
        render = null;
        var failures = new List<Exception>();
        try { active?.Cancel(); }
        catch (Exception error) { failures.Add(error); }
        foreach (var resource in new IDisposable[] { loads, writes, navigation })
        {
            try { resource.Dispose(); }
            catch (Exception error) { failures.Add(error); }
        }
        if (failures.Count != 0) throw new AggregateException("Profile workspace cleanup failed.", failures);
    }
}

using System.Text;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private sealed class MemoryStorage : IApplicationStorage
    {
        public byte[]? Data { get; set; }
        public int Reads { get; private set; }
        public int Writes { get; private set; }
        public int Deletes { get; private set; }
        public CancellationToken LastToken { get; private set; }
        public Func<CancellationToken, Task<OperationResult<StoredValue>>>? Read { get; set; }
        public Func<ReadOnlyMemory<byte>, CancellationToken, Task<OperationResult<bool>>>? Write { get; set; }
        public Func<CancellationToken, Task<OperationResult<bool>>>? Delete { get; set; }

        public Task<OperationResult<StoredValue>> ReadAsync(string key, CancellationToken cancellationToken = default)
        {
            Assert(key == "profile-draft-v1", "Read uses the one explicit application storage key.");
            Reads++;
            LastToken = cancellationToken;
            return Read is not null ? Read(cancellationToken) :
                Task.FromResult(OperationResult<StoredValue>.Completed(new(Data is not null, Data ?? [])));
        }
        public Task<OperationResult<bool>> WriteAsync(string key, ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
        {
            Assert(key == "profile-draft-v1", "Write uses the one explicit application storage key.");
            Writes++;
            LastToken = cancellationToken;
            if (Write is not null) return Write(data, cancellationToken);
            Data = data.ToArray();
            return Task.FromResult(OperationResult<bool>.Completed(true));
        }
        public Task<OperationResult<bool>> DeleteAsync(string key, CancellationToken cancellationToken = default)
        {
            Assert(key == "profile-draft-v1", "Delete uses the one explicit application storage key.");
            Deletes++;
            LastToken = cancellationToken;
            if (Delete is not null) return Delete(cancellationToken);
            Data = null;
            return Task.FromResult(OperationResult<bool>.Completed(true));
        }
    }

    private sealed class Harness : IDisposable
    {
        public Dispatcher Dispatcher { get; } = new();
        public Host Host { get; }
        public ProfileWorkspace App { get; }
        public ProfileWorkspaceController Controller => App.Controller;
        public Backend Backend { get; } = new();
        public List<Exception> Unhandled { get; } = [];
        public Harness(IApplicationStorage storage, ProfileWorkspaceSession? session = null)
        {
            Host = new(Dispatcher);
            App = ProfileWorkspace.Create(Host, storage, Unhandled.Add, session);
            Host.Attach(Backend);
        }
        public void SetProfile()
        {
            Controller.SetDisplayName("Ada");
            Controller.SetRole("Engineer");
            Controller.SetLocation("London");
            Controller.SetFocus("Native input");
        }
        public void Finish() => Dispatcher.Complete(Controller.LastOperation);
        public void Dispose() => Host.Dispose();
    }

    private static void StorageChecks()
    {
        StorageRoundtripChecks();
        StorageOutcomeChecks();
        CancellationChecks();
        ComponentOwnershipChecks();
        DirectoryChecks();
        NavigationChecks();
    }

    private static void StorageRoundtripChecks()
    {
        var storage = new MemoryStorage();
        using var h = new Harness(storage);
        Assert(storage.Reads == 0 && storage.Writes == 0 && storage.Deletes == 0, "Construction never reads or writes user storage.");
        Assert(h.App.Root.Children.Count == 2 && h.App.Root.Children[1] is ScrollView { Flex: 1 }, "Profile controls scroll in short viewports.");
        Throws<InvalidOperationException>(() => h.Controller.Save());
        h.SetProfile();
        var namePeer = h.Backend.Find("profile-name");
        var editLifetime = h.Host.GetComponentLifetime(h.App.PageView.Children[0]);
        var editNavigationLifetime = h.Controller.CurrentPageLifetime;
        h.Controller.Preview();
        Assert(editLifetime.Token.IsCancellationRequested && namePeer.Disposed && !namePeer.Events.Change("retired"), "Preview removes and retires the editor view.");
        Assert(!editNavigationLifetime.IsCancellationRequested && h.Controller.NavigationDepth == 2, "Back-stack entry survives while editor view is absent.");
        Assert(storage.Writes == 0 && h.Controller.State.Draft.DisplayName == "Ada" &&
            ((Label)h.Backend.Find("profile-preview-name").Element).Text == "Ada", "Preview shows unsaved data without writing it.");
        var previewLifetime = h.Host.GetComponentLifetime(h.App.PageView.Children[0]);
        var previewNavigationLifetime = h.Controller.CurrentPageLifetime;
        h.Controller.Back();
        Assert(previewLifetime.Token.IsCancellationRequested && previewNavigationLifetime.IsCancellationRequested &&
            h.Controller.NavigationDepth == 1, "Back retires preview view and history entry.");
        Assert(!ReferenceEquals(namePeer, h.Backend.Find("profile-name")) &&
            ((TextInput)h.Backend.Find("profile-name").Element).Text == "Ada", "Fresh editor view restores unsaved draft.");
        h.Controller.Save();
        Assert(h.Controller.State.Busy && h.Controller.State.Status == "Save in progress...", "Save remains busy until UI delivery acknowledges provider completion.");
        Assert(!h.Backend.Find("profile-name").Events.Change("blocked") && !h.Backend.Find("profile-preview").Events.Click(), "Busy operation disables editing and navigation.");
        Throws<InvalidOperationException>(() => h.Controller.Delete());
        h.Finish();
        Assert(!h.Controller.State.Busy && h.Controller.State.Status == "Draft saved." &&
            h.Controller.State.SavedProfile == h.Controller.State.Draft && storage.Writes == 1, "Save confirms actual provider completion.");
        const string literal = """{"Version":1,"Profile":{"DisplayName":"Ada","Role":"Engineer","Location":"London","Focus":"Native input"}}""";
        Assert(Encoding.UTF8.GetString(storage.Data!) == literal, "Provider receives literal profile data, not UI objects.");
        h.Controller.SetDisplayName("Grace");
        Assert(Encoding.UTF8.GetString(storage.Data!) == literal, "Typing after save does not persist automatically.");
        h.Controller.Load();
        h.Finish();
        Assert(h.Controller.State.Draft.DisplayName == "Ada" && h.Controller.State.Status == "Saved draft loaded.", "Load explicitly replaces local draft.");
        h.Controller.Delete();
        h.Finish();
        Assert(storage.Data is null && h.Controller.State.Draft.DisplayName == "Ada" &&
            h.Controller.State.Status == "Stored draft deleted. Current edits kept.", "Delete only changes persisted data.");
        h.Controller.Load();
        h.Finish();
        Assert(h.Controller.State.Draft.DisplayName == "Ada" &&
            h.Controller.State.Status == "No saved draft found. Current edits kept.", "Missing draft is explicit and does not erase edits.");
        Assert(h.Unhandled.Count == 0, "Ordinary storage workflow has no unhandled failures.");
    }

    private static void StorageOutcomeChecks()
    {
        var storage = new MemoryStorage();
        using var h = new Harness(storage);
        h.SetProfile();
        foreach (var result in new[] { OperationResult<bool>.Denied(), OperationResult<bool>.Unsupported(), OperationResult<bool>.Cancelled() })
        {
            storage.Write = (_, _) => Task.FromResult(result);
            h.Controller.Save();
            h.Finish();
            Assert(!h.Controller.State.Busy && storage.Data is null, "Rejected storage result is not a successful save.");
            Assert(h.Controller.State.Status == result.Status switch
            {
                OperationStatus.Denied => "Save denied.",
                OperationStatus.Unsupported => "Save unavailable.",
                _ => "Save canceled. Storage may have changed; Load draft to check."
            }, "Service outcome is visible.");
        }
        storage.Write = (_, _) => Task.FromResult(OperationResult<bool>.Completed(false));
        h.Controller.Save();
        h.Finish();
        Assert(h.Controller.State.Status == "Save failed." && h.Controller.State.Error == "Storage did not confirm the save.", "False acknowledgment is never called successful.");
        storage.Write = (_, _) => Task.FromResult(OperationResult<bool>.Failed(new IOException("Disk full.")));
        h.Controller.Save();
        h.Finish();
        Assert(h.Controller.State.Status == "Save failed." && h.Controller.State.Error == "Disk full." &&
            h.Controller.LastError is IOException, "Provider failure is visible and retained for diagnostics.");
        storage.Write = (_, _) => throw new IOException("Provider threw.");
        h.Controller.Save();
        h.Finish();
        Assert(h.Controller.State.Status == "Save failed." && h.Controller.State.Error == "Provider threw.", "Synchronous provider exceptions are observed.");

        var original = h.Controller.State.Draft;
        foreach (byte[] invalid in new[] { Encoding.UTF8.GetBytes("{"), new byte[] { 0xff }, Encoding.UTF8.GetBytes("""{"Version":99,"Profile":null}""") })
        {
            storage.Data = invalid;
            h.Controller.Load();
            h.Finish();
            Assert(h.Controller.State.Status == "Load failed." && h.Controller.State.Error.Length > 0 &&
                h.Controller.State.Draft == original, "Malformed stored data never replaces live edits.");
        }
        storage.Read = _ => Task.FromResult(OperationResult<StoredValue>.Denied());
        h.Controller.Load();
        h.Finish();
        Assert(h.Controller.State.Status == "Load denied." && h.Controller.State.Draft == original, "Denied load preserves draft.");
        storage.Read = _ => Task.FromResult(OperationResult<StoredValue>.Unsupported());
        h.Controller.Load();
        h.Finish();
        Assert(h.Controller.State.Status == "Load unavailable." && h.Controller.State.Error.Length > 0, "Unsupported storage isn't an empty profile.");
        storage.Read = _ => Task.FromResult(OperationResult<StoredValue>.Cancelled());
        h.Controller.Load();
        h.Finish();
        Assert(h.Controller.State.Status == "Load canceled. Current edits kept.", "Native load cancellation is distinct.");
        storage.Delete = _ => Task.FromResult(OperationResult<bool>.Failed(new IOException("Read-only storage.")));
        h.Controller.Delete();
        h.Finish();
        Assert(h.Controller.State.Status == "Delete failed." && h.Controller.State.Error == "Read-only storage.", "Delete failure is surfaced.");
        Assert(h.Unhandled.Count == 0, "Expected storage errors are displayed, not abandoned tasks.");
    }

    private static void CancellationChecks()
    {
        var storage = new MemoryStorage();
        using var h = new Harness(storage);
        h.SetProfile();
        var write = new TaskCompletionSource<OperationResult<bool>>(TaskCreationOptions.RunContinuationsAsynchronously);
        byte[]? pendingBytes = null;
        storage.Write = (data, _) => { pendingBytes = data.ToArray(); return write.Task; };
        h.Controller.Save();
        var save = h.Controller.LastOperation;
        h.Controller.Cancel();
        Assert(storage.LastToken.IsCancellationRequested && h.Controller.State.Busy &&
            h.Controller.State.CancelRequested && !save.IsCompleted, "Cancellation signals the provider but waits for its acknowledgment.");
        Throws<InvalidOperationException>(() => h.Controller.Delete());
        Throws<InvalidOperationException>(() => h.Controller.Load());
        Throws<InvalidOperationException>(() => h.Controller.Save());
        Assert(storage.Deletes == 0 && storage.Reads == 0 && storage.Writes == 1, "Cancel never permits overlapping side effects.");
        storage.Data = pendingBytes;
        write.SetResult(OperationResult<bool>.Completed(true));
        h.Finish();
        Assert(!h.Controller.State.Busy && h.Controller.State.Status == "Save canceled. Storage may have changed; Load draft to check.",
            "Late committed save is not falsely reported as rolled back or confirmed.");
        h.Controller.Load();
        h.Finish();
        Assert(h.Controller.State.Status == "Saved draft loaded.", "Explicit load resolves ambiguous canceled save.");

        var read = new TaskCompletionSource<OperationResult<StoredValue>>(TaskCreationOptions.RunContinuationsAsynchronously);
        storage.Read = _ => read.Task;
        h.Controller.SetDisplayName("Keep this edit");
        h.Controller.Load();
        h.Controller.Cancel();
        read.SetResult(OperationResult<StoredValue>.Completed(new(true, storage.Data!)));
        h.Finish();
        Assert(h.Controller.State.Draft.DisplayName == "Keep this edit" &&
            h.Controller.State.Status == "Load canceled. Current edits kept.", "Late canceled read cannot overwrite current edits.");

        var closeRead = new TaskCompletionSource<OperationResult<StoredValue>>(TaskCreationOptions.RunContinuationsAsynchronously);
        storage.Read = _ => closeRead.Task;
        h.Controller.Load();
        var closing = h.Controller.LastOperation;
        var token = h.App.Lifetime.Token;
        h.Host.Dispose();
        Assert(token.IsCancellationRequested && storage.LastToken.IsCancellationRequested, "Workspace lifetime cancels provider work on terminal removal.");
        closeRead.SetResult(OperationResult<StoredValue>.Completed(new(true, storage.Data!)));
        h.Dispatcher.Complete(closing);
        Assert(h.Unhandled.Count == 0 && h.Backend.Peers.All(p => p.Disposed), "Late closed-workspace result is observed without touching disposed UI.");

        var failingStorage = new MemoryStorage();
        using var broken = new Harness(failingStorage);
        broken.SetProfile();
        broken.Dispatcher.RejectPost = true;
        broken.Controller.Save();
        broken.Finish();
        Assert(broken.Unhandled.Count == 1, "Unrenderable dispatcher failures reach the required host reporter.");

        var renderStorage = new MemoryStorage();
        using var renderFailure = new Harness(renderStorage);
        renderFailure.SetProfile();
        var acknowledged = new TaskCompletionSource<OperationResult<bool>>(TaskCreationOptions.RunContinuationsAsynchronously);
        renderStorage.Write = (_, _) => acknowledged.Task;
        renderFailure.Controller.Save();
        renderFailure.Backend.FailUpdateId = "profile-status";
        acknowledged.SetResult(OperationResult<bool>.Completed(true));
        renderFailure.Finish();
        Assert(!renderFailure.Host.IsAttached && renderFailure.Unhandled.Count == 1 &&
            renderFailure.Controller.LastError?.Message == "Native property update failed.",
            "Async presentation failure reaches the host reporter even when the failed backend cannot display an error.");
    }

    private static void ComponentOwnershipChecks()
    {
        var storage = new MemoryStorage();
        using (var h = new Harness(storage))
        {
            h.SetProfile();
            var write = new TaskCompletionSource<OperationResult<bool>>(TaskCreationOptions.RunContinuationsAsynchronously);
            storage.Write = (_, _) => write.Task;
            h.Controller.Save();
            var owner = h.App.Lifetime;
            h.Host.Detach();
            Assert(!owner.Token.IsCancellationRequested && !storage.LastToken.IsCancellationRequested,
                "Native detach does not cancel workspace-owned storage.");
            write.SetResult(OperationResult<bool>.Completed(true));
            h.Finish();
            Assert(h.Controller.State.Status == "Draft saved." && !h.Controller.State.Busy,
                "Detached workspace still accepts its owned operation result.");
            var replacement = new Backend();
            h.Host.Attach(replacement);
            Assert(((Label)replacement.Find("profile-status").Element).Text == "Draft saved.",
                "Reattach displays the completed retained state.");

            var failedWrite = new TaskCompletionSource<OperationResult<bool>>(TaskCreationOptions.RunContinuationsAsynchronously);
            storage.Write = (_, _) => failedWrite.Task;
            h.Controller.Save();
            h.Controller.Cancel();
            failedWrite.SetResult(OperationResult<bool>.Failed(new IOException("Canceled write cleanup failed.")));
            h.Finish();
            Assert(h.Controller.State.Status == "Save failed." && h.Controller.State.Error == "Canceled write cleanup failed.",
                "Cancellation cannot hide a provider cleanup failure.");
        }

        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var removableStorage = new MemoryStorage();
        var errors = new List<Exception>();
        ProfileWorkspace? workspace = null;
        KeyedStack pages;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            pages = host.KeyedStack(Axis.Vertical);
            root.Add(pages);
            pages.Reconcile([KeyedItem.Create("profile", h => workspace = ProfileWorkspace.Create(h, removableStorage, errors.Add))]);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new Backend();
        host.Attach(backend);
        var app = workspace ?? throw new InvalidOperationException("Nested workspace was not constructed.");
        app.Controller.SetDisplayName("Owned component");
        var read = new TaskCompletionSource<OperationResult<StoredValue>>(TaskCreationOptions.RunContinuationsAsynchronously);
        removableStorage.Read = _ => read.Task;
        app.Controller.Load();
        var pending = app.Controller.LastOperation;
        var lifetime = app.Lifetime;
        var retiredName = backend.Find("profile-name");
        pages.Reconcile([]);
        Assert(host.IsAttached && lifetime.Token.IsCancellationRequested && removableStorage.LastToken.IsCancellationRequested &&
            retiredName.Disposed, "Permanent keyed workspace removal cancels I/O without disposing its host.");
        Assert(!retiredName.Events.Change("late") && pages.Children.Count == 0, "Removed workspace callbacks cannot mutate replacement UI.");
        read.SetResult(OperationResult<StoredValue>.Completed(new(false, ReadOnlyMemory<byte>.Empty)));
        dispatcher.Complete(pending);
        Assert(errors.Count == 0 && backend.Peers.Count(p => !p.Disposed) == 2,
            "Retired workspace observes late provider results and leaves only the owning tree.");
        Throws<ObjectDisposedException>(() => app.Controller.Load());
    }

    private static void DirectoryChecks()
    {
        string directory = Path.Combine(Path.GetTempPath(), "xui-profile-tests-" + Guid.NewGuid().ToString("N"));
        try
        {
            using var h = new Harness(new DirectoryApplicationStorage(directory));
            h.SetProfile();
            h.Controller.Preview();
            Assert(!Directory.Exists(directory), "Neither editor nor preview creates storage on disk.");
            h.Controller.Save();
            h.Finish();
            string path = Path.Combine(directory, "profile-draft-v1.data");
            Assert(File.ReadAllText(path) == """{"Version":1,"Profile":{"DisplayName":"Ada","Role":"Engineer","Location":"London","Focus":"Native input"}}""",
                "Real provider writes the exact application snapshot to isolated test storage.");
            h.Controller.Back();
            h.Controller.SetDisplayName("Unsaved");
            h.Controller.Load();
            h.Finish();
            Assert(h.Controller.State.Draft.DisplayName == "Ada", "Real provider loads prior persisted values.");
            File.WriteAllText(path, "{malformed test draft");
            h.Controller.Load();
            h.Finish();
            Assert(h.Controller.State.Status == "Load failed." && h.Controller.State.Draft.DisplayName == "Ada", "Real malformed file is explicit without edit loss.");
            h.Controller.Delete();
            h.Finish();
            Assert(!File.Exists(path) && Directory.GetFiles(directory).Length == 0, "Real delete removes the saved file and leaves no temporary files.");
            h.Controller.Load();
            h.Finish();
            Assert(h.Controller.State.Status == "No saved draft found. Current edits kept.", "Real missing-file result is explicit.");
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    private static void NavigationChecks()
    {
        using var h = new Harness(new MemoryStorage());
        h.SetProfile();
        var original = h.Controller.State;
        var name = h.Backend.Find("profile-name");
        h.Backend.RejectMutation = true;
        var rejected = Throws<KeyedUpdateException>(() => h.Controller.Preview());
        Assert(!rejected.ModelCommitted && h.Controller.NavigationDepth == 1 && h.Controller.State == original &&
            ReferenceEquals(name, h.Backend.Find("profile-name")), "Rejected preview rolls history back without replacing input.");
        h.Backend.RejectMutation = false;
        h.Controller.Preview();
        var preview = h.Controller.State;
        var pageToken = h.Controller.CurrentPageLifetime;
        h.Backend.RejectMutation = true;
        rejected = Throws<KeyedUpdateException>(() => h.Controller.Back());
        Assert(!rejected.ModelCommitted && h.Controller.NavigationDepth == 2 && h.Controller.State == preview &&
            !pageToken.IsCancellationRequested, "Rejected back preserves route, page identity, and navigation lifetime.");
        h.Backend.RejectMutation = false;
        h.Backend.FailInsert = true;
        var committed = Throws<KeyedUpdateException>(() => h.Controller.Back());
        Assert(committed.ModelCommitted && h.Controller.NavigationDepth == 1 &&
            h.Controller.State.Page == ProfilePage.Edit && pageToken.IsCancellationRequested &&
            !h.Host.IsAttached, "Postcommit back failure retains the new route coherently while detached.");
        var recovered = new Backend();
        h.Host.Attach(recovered);
        Assert(((TextInput)recovered.Find("profile-name").Element).Text == "Ada", "Reattach shows the committed page and unsaved draft.");
        h.Host.Detach();
        recovered = new Backend { FailInsert = true };
        h.Host.Attach(recovered);
        committed = Throws<KeyedUpdateException>(() => h.Controller.Preview());
        Assert(committed.ModelCommitted && h.Controller.NavigationDepth == 2 &&
            h.Controller.State.Page == ProfilePage.Preview && !h.Host.IsAttached, "Postcommit preview failure keeps matching history.");
    }
}

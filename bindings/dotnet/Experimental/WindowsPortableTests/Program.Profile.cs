using System.Collections.Concurrent;
using System.Text;
using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeProfileScenarios()
    {
        var temporary = Directory.CreateTempSubdirectory("xui-windows-profile-");
        try
        {
            string directory = Path.Combine(temporary.FullName, "data");
            var storage = new ProfileStorageProbe(new P.DirectoryApplicationStorage(directory));
            using var application = new Application();
            using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
            var surface = Surface(window);
            uint baseline = HandleCount(window);
            using var dispatcher = new WindowsDispatcher(window);
            using var host = new P.Host(dispatcher);
            P.Host activeHost = host;
            window.Closed += _ => activeHost.Dispose();
            var fatal = new ConcurrentQueue<Exception>();
            var profile = ProfileWorkspace.Create(host, storage, fatal.Enqueue);
            var backend = new WindowsBackend(surface, dispatcher);
            host.Attach(backend);
            Check(storage.Reads == 0 && storage.Writes == 0 && storage.Deletes == 0 && !Directory.Exists(directory),
                "Profile startup performed storage work.");
            application.Show(window);
            var native = new Driver(application, window, host, () => backend);
            var driver = new ProfileDriver(native, profile.Controller);
            var work = Task.Run(() =>
            {
                try
                {
                    native.Ui(() =>
                    {
                        var status = native.Native("profile-status").GetBounds();
                        var heading = native.Native("profile-edit-title").GetBounds();
                        Check(Math.Abs(heading.Y - status.Y - status.Height - 12) < 0.1f,
                            $"Hidden profile error/busy/cancel controls retained layout gaps: next={heading.Y}, status end={status.Y + status.Height}.");
                    });
                    Interlocked.Add(ref assertions,
                        ApplicationScenarioRunner.Run(Resource("ProfileWorkspaceScenarios.json"), "profile-workspace", driver));
                    Check(storage.Reads == 3 && storage.Writes == 2 && storage.Deletes == 1,
                        "Profile navigation or editor reset performed unrequested storage work.");
                    Check(!File.Exists(Path.Combine(directory, ProfileWorkspaceController.StorageKey + ".data")),
                        "Deleting the temporary profile draft did not remove its data file.");
                    native.Ui(() =>
                    {
                        Check(profile.Controller.NavigationDepth == 2 && profile.GetState().Page == ProfilePage.Preview &&
                            profile.GetState().Draft.DisplayName == "Ada Lovelace",
                            "The shared profile did not finish on its unsaved preview page.");
                    });
                    var session = native.Ui(() => profile.Controller.CaptureSession());
                    var restored = ProfileWorkspaceSessionCodec.Restore(ProfileWorkspaceSessionCodec.Serialize(session));
                    native.Ui(() =>
                    {
                        var oldPageLifetime = profile.Controller.CurrentPageLifetime;
                        string oldPageKey = profile.GetState().PageKey;
                        activeHost.Dispose();
                        Check(oldPageLifetime.IsCancellationRequested && HandleCount(window) == baseline,
                            "Capturing a transient profile session did not allow complete native teardown.");
                        var nextHost = new P.Host(dispatcher);
                        try
                        {
                            var next = ProfileWorkspace.Create(nextHost, storage, fatal.Enqueue, session: restored);
                            var nextBackend = new WindowsBackend(surface, dispatcher);
                            nextHost.Attach(nextBackend);
                            activeHost = nextHost;
                            profile = next;
                            backend = nextBackend;
                        }
                        catch
                        {
                            nextHost.Dispose();
                            throw;
                        }
                        Check(profile.GetState().Page == ProfilePage.Preview && profile.GetState().PageKey != oldPageKey &&
                            profile.Controller.NavigationDepth == 2 && !profile.GetState().Busy &&
                            profile.GetState().Draft.DisplayName == "Ada Lovelace",
                            "Transient restore did not create fresh idle navigation identities with retained data.");
                        Check(storage.Reads == 3 && storage.Writes == 2 && storage.Deletes == 1,
                            "Transient profile restoration accessed persistent storage.");
                    });
                    native = new Driver(application, window, activeHost, () => backend);

                    var lateRead = storage.HoldRead();
                    var pending = native.Ui(() =>
                    {
                        profile.Controller.Load();
                        Check(profile.GetState().Busy, "Asynchronous profile loading did not publish its busy state.");
                        return profile.Controller.LastOperation;
                    });
                    storage.ReadStarted.Task.WaitAsync(Timeout).GetAwaiter().GetResult();
                    var pageLifetime = native.Ui(() => profile.Controller.CurrentPageLifetime);
                    native.Ui(() =>
                    {
                        activeHost.Dispose();
                        Check(pageLifetime.IsCancellationRequested && HandleCount(window) == baseline,
                            "Profile disposal did not cancel page work and release its native arena.");
                    });
                    byte[] data = Encoding.UTF8.GetBytes(ProfileWorkspaceCodec.Serialize(new("Late profile", "", "", "")));
                    lateRead.SetResult(P.OperationResult<P.StoredValue>.Completed(new(true, data)));
                    pending.WaitAsync(Timeout).GetAwaiter().GetResult();
                    native.Ui(() => Check(HandleCount(window) == baseline && fatal.IsEmpty,
                        "A late profile result resurrected native content or escaped the disposed owner."));
                }
                finally { native.Ui(window.Close); }
            });
            try { application.Run(); }
            finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
            Check(fatal.IsEmpty, "The profile controller reported an unhandled host failure.");
        }
        finally { temporary.Delete(recursive: true); }
    }

    private sealed class ProfileDriver(Driver native, ProfileWorkspaceController controller) : IApplicationScenarioDriver
    {
        public void Change(string id, string value) => native.Change(id, value);
        public void Click(string id)
        {
            native.Click(id);
            native.Ui(() => controller.LastOperation).WaitAsync(Timeout).GetAwaiter().GetResult();
        }
        public void Submit(string id)
        {
            native.Submit(id);
            native.Ui(() => controller.LastOperation).WaitAsync(Timeout).GetAwaiter().GetResult();
        }
        public string Text(string id) => native.Text(id);
        public bool Enabled(string id) => native.Enabled(id);
        public bool Visible(string id) => native.Visible(id);
    }

    private sealed class ProfileStorageProbe(P.IApplicationStorage storage) : P.IApplicationStorage
    {
        private int reads, writes, deletes;
        private TaskCompletionSource<P.OperationResult<P.StoredValue>>? readGate;
        internal int Reads => Volatile.Read(ref reads);
        internal int Writes => Volatile.Read(ref writes);
        internal int Deletes => Volatile.Read(ref deletes);
        internal TaskCompletionSource ReadStarted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal TaskCompletionSource<P.OperationResult<P.StoredValue>> HoldRead()
        {
            var gate = new TaskCompletionSource<P.OperationResult<P.StoredValue>>(TaskCreationOptions.RunContinuationsAsynchronously);
            Volatile.Write(ref readGate, gate);
            return gate;
        }

        public Task<P.OperationResult<P.StoredValue>> ReadAsync(string key, CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref reads);
            if (Volatile.Read(ref readGate) is { } pending)
            {
                // This fixture deliberately simulates a provider completing after owner cancellation.
                ReadStarted.TrySetResult();
                return pending.Task;
            }
            return storage.ReadAsync(key, cancellationToken);
        }
        public Task<P.OperationResult<bool>> WriteAsync(string key, ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref writes);
            return storage.WriteAsync(key, data, cancellationToken);
        }
        public Task<P.OperationResult<bool>> DeleteAsync(string key, CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref deletes);
            return storage.DeleteAsync(key, cancellationToken);
        }
    }
}

using System.Collections.Concurrent;
using Android.Util;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task ProfileChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        string privateDirectory = FilesDir?.AbsolutePath
            ?? throw new InvalidOperationException("The native test app has no private files directory.");
        string directory = Path.Combine(privateDirectory, "xui-profile-tests", Guid.NewGuid().ToString("N"));
        var storage = new CountedStorage(new DirectoryApplicationStorage(directory));
        var errors = new ConcurrentQueue<Exception>();
        string sessionJson;
        string originalPageKey;
        int shared;
        try
        {
            using (var host = new Host(dispatcher))
            {
                var workspace = ProfileWorkspace.Create(host, storage, errors.Enqueue);
                var controller = workspace.Controller;
                var backend = new AndroidBackend(surface, dispatcher);
                host.Attach(backend);
                Assert(storage.Reads == 0 && storage.Writes == 0 && storage.Deletes == 0 && !Directory.Exists(directory),
                    "Creating and mounting Profile performs no automatic document IO.");
                using var stream = typeof(TestActivity).Assembly.GetManifestResourceStream("ProfileWorkspaceScenarios.json")
                    ?? throw new InvalidOperationException("The shared profile corpus is missing.");
                using var reader = new StreamReader(stream);
                string json = reader.ReadToEnd();
                try
                {
                    var driver = new ProfileDriver(host, dispatcher, workspace, new NativeDriver(backend));
                    shared = await Task.Run(() => ApplicationScenarioRunner.Run(json, "profile-workspace", driver));
                    assertions += shared;
                    Assert(storage.Writes == 2 && storage.Reads == 3 && storage.Deletes == 1 &&
                        !File.Exists(Path.Combine(directory, ProfileWorkspaceController.StorageKey + ".data")),
                        "Native profile corpus saves, loads and deletes only its isolated real-directory document.");
                    Assert(workspace.Controller.NavigationDepth == 2 && !workspace.GetState().Busy &&
                        workspace.GetState().Draft.DisplayName == "Ada Lovelace",
                        "Off-UI corpus waits let the UI pump and reach the expected unsaved preview.");
                    sessionJson = ProfileWorkspaceSessionCodec.Serialize(workspace.Controller.CaptureSession());
                    originalPageKey = workspace.GetState().PageKey;
                    var lifetime = workspace.Controller.CurrentPageLifetime;
                    var views = CaptureViews(surface.GetChildAt(0)!);
                    host.Dispose();
                    Assert(lifetime.IsCancellationRequested && surface.ChildCount == 0 &&
                        views.All(view => view.Handle == IntPtr.Zero),
                        "Root disposal retires profile navigation and native views.");
                }
                finally
                {
                    Task pending = controller.LastOperation;
                    host.Dispose();
                    await pending;
                }
            }
            using (var host = new Host(dispatcher))
            {
                var session = ProfileWorkspaceSessionCodec.Restore(sessionJson);
                var workspace = ProfileWorkspace.Create(host, storage, errors.Enqueue, session: session);
                var backend = new AndroidBackend(surface, dispatcher);
                host.Attach(backend);
                Assert(workspace.GetState().Page == ProfilePage.Preview && workspace.Controller.NavigationDepth == 2 &&
                    workspace.GetState().PageKey != originalPageKey && !workspace.GetState().Busy &&
                    workspace.Controller.LastOperation.IsCompleted,
                    "Transient restoration creates fresh idle routes without retaining old work or UI keys.");
                Assert(storage.Writes == 2 && storage.Reads == 3 && storage.Deletes == 1,
                    "Transient profile restoration does not auto-load or auto-save.");
                var driver = new NativeDriver(backend);
                driver.Click("profile-back");
                Assert(workspace.Controller.NavigationDepth == 1 && driver.Text("profile-name") == "Ada Lovelace" &&
                    driver.Text("profile-role") == "Developer" && driver.Text("profile-focus") == "Accessible tools",
                    "Restored Preview can return to its newly created native Edit page.");
                host.Dispose();
                Assert(surface.ChildCount == 0, "The restored profile workspace releases its surface.");
            }
            Assert(errors.IsEmpty, "The actual directory/native profile fixture reports no unhandled worker errors.");
            Log.Info("Xui.Android.Orders", $"Profile: {shared} shared expectations; {assertions - before - shared} native storage/lifetime assertions.");
        }
        finally
        {
            if (Directory.Exists(directory)) await Task.Run(() => Directory.Delete(directory, recursive: true));
        }
    }

    private sealed class ProfileDriver(Host host, AndroidDispatcher dispatcher, ProfileWorkspace workspace, NativeDriver controls)
        : IApplicationScenarioDriver
    {
        private T OnUi<T>(Func<T> operation)
        {
            if (dispatcher.CheckAccess()) throw new InvalidOperationException("The blocking profile corpus driver must run off the UI thread.");
            T result = default!;
            host.DispatchAsync(() => result = operation()).GetAwaiter().GetResult();
            return result;
        }

        private void Action(Action action)
        {
            Task pending = OnUi(() =>
            {
                action();
                return workspace.Controller.LastOperation;
            });
            pending.GetAwaiter().GetResult();
        }

        public void Click(string id) => Action(() => controls.Click(id));
        public void Change(string id, string value) => Action(() => controls.Change(id, value));
        public void Submit(string id) => Action(() => controls.Submit(id));
        public string Text(string id) => OnUi(() => controls.Text(id));
        public bool Enabled(string id) => OnUi(() => controls.Enabled(id));
        public bool Visible(string id) => OnUi(() => controls.Visible(id));
    }

    private sealed class CountedStorage(IApplicationStorage inner) : IApplicationStorage
    {
        private int reads;
        private int writes;
        private int deletes;
        internal int Reads => Volatile.Read(ref reads);
        internal int Writes => Volatile.Read(ref writes);
        internal int Deletes => Volatile.Read(ref deletes);
        public Task<OperationResult<StoredValue>> ReadAsync(string key, CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref reads);
            return inner.ReadAsync(key, cancellationToken);
        }
        public Task<OperationResult<bool>> WriteAsync(string key, ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref writes);
            return inner.WriteAsync(key, data, cancellationToken);
        }
        public Task<OperationResult<bool>> DeleteAsync(string key, CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref deletes);
            return inner.DeleteAsync(key, cancellationToken);
        }
    }
}

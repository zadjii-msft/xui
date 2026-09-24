using System.Collections.Concurrent;
using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeApplicationScenarios()
    {
        foreach (string kind in new[] { "Cart", "Services" })
        {
            using var application = new Application();
            using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
            var surface = Surface(window);
            uint baseline = HandleCount(window);
            using var dispatcher = new WindowsDispatcher(window);
            using var host = new P.Host(dispatcher);
            var errors = new ConcurrentQueue<Exception>();
            Func<Task> pending;
            FixturePlatformServices? services = null;
            FixturePicker? picker = null;
            if (kind == "Cart")
            {
                var app = EditableCart.Create(host, new LocalCartQuoteService(), errors.Enqueue);
                pending = () => app.Controller.LastOperation;
            }
            else
            {
                services = new FixturePlatformServices();
                picker = new FixturePicker();
                var app = PlatformServicesWorkbench.Create(host, services, picker, errors.Enqueue);
                pending = () => app.Controller.LastOperation;
                Check(services.Calls == 0 && picker.Calls == 0, "Service application startup performed provider I/O.");
            }
            var backend = new WindowsBackend(surface, dispatcher);
            host.Attach(backend);
            application.Show(window);
            var ui = new Driver(application, window, host, () => backend);
            var driver = new AsyncApplicationDriver(ui, pending);
            var work = Task.Run(() =>
            {
                try
                {
                    Interlocked.Add(ref assertions, ApplicationScenarioRunner.Run(
                        Resource(kind == "Cart" ? "EditableCartScenarios.json" : "PlatformServicesScenarios.json"),
                        kind == "Cart" ? "editable-cart" : "platform-services", driver));
                    ui.Ui(() =>
                    {
                        if (kind == "Services")
                            Check(services!.Calls == 3 && picker!.Calls == 1 && picker.StreamDisposed,
                                "The fake services corpus did not settle and dispose its owned stream.");
                        host.Dispose();
                        Check(HandleCount(window) == baseline && errors.IsEmpty, "Shared application cleanup leaked its native arena.");
                    });
                }
                finally { ui.Ui(window.Close); }
            });
            RunNativeWork(application, work);
            if (picker is not null)
            {
                Check(!picker.Disposed, "The shared app disposed its caller-owned picker.");
                picker.Dispose();
            }
        }
    }

    private sealed class AsyncApplicationDriver(Driver ui, Func<Task> pending) : IApplicationScenarioDriver
    {
        public void Click(string id) { ui.Click(id); ui.Ui(pending).WaitAsync(Timeout).GetAwaiter().GetResult(); }
        public void Change(string id, string value) => ui.Change(id, value);
        public void Submit(string id) { ui.Submit(id); ui.Ui(pending).WaitAsync(Timeout).GetAwaiter().GetResult(); }
        public string Text(string id) => ui.Text(id);
        public bool Enabled(string id) => ui.Enabled(id);
        public bool Visible(string id) => ui.Visible(id);
    }

    private sealed class FixturePlatformServices : P.IPlatformServices
    {
        internal int Calls;
        public P.CapabilityAvailability GetAvailability(P.ServiceCapability capability) => P.CapabilityAvailability.RequiresUserGesture;
        public Task<P.OperationResult<string>> ReadClipboardAsync(CancellationToken cancellationToken = default)
        {
            Calls++;
            return Task.FromResult(P.OperationResult<string>.Completed(new string('x', 14)));
        }
        public Task<P.OperationResult<bool>> WriteClipboardAsync(string text, CancellationToken cancellationToken = default)
        {
            Calls++;
            return Task.FromResult(P.OperationResult<bool>.Completed(true));
        }
        public Task<P.OperationResult<bool>> OpenUriAsync(Uri uri, CancellationToken cancellationToken = default)
        {
            Calls++;
            return Task.FromResult(P.OperationResult<bool>.Completed(true));
        }
    }
    private sealed class FixturePicker : P.IFilePicker
    {
        internal int Calls;
        internal bool StreamDisposed;
        internal bool Disposed;
        public P.CapabilityAvailability GetAvailability(P.ServiceCapability capability) => P.CapabilityAvailability.RequiresUserGesture;
        public Task<P.OperationResult<P.PickedFile>> OpenAsync(P.FileSelectionOptions options, CancellationToken cancellationToken = default)
        {
            Calls++;
            return Task.FromResult(P.OperationResult<P.PickedFile>.Completed(
                new P.PickedFile("example.txt", new FixtureStream(() => StreamDisposed = true), options, 12)));
        }
        public void Dispose() => Disposed = true;
    }
    private sealed class FixtureStream(Action disposed) : MemoryStream(new byte[12], writable: false)
    {
        protected override void Dispose(bool disposing) { if (disposing) disposed(); base.Dispose(disposing); }
    }
}

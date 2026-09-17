using System.Reflection;
using System.Runtime.Loader;

namespace Xui.Designer;

internal sealed class PreviewHost(Action<long, string, bool> report) : IDisposable
{
    private sealed record Request(long Version, byte[] Assembly, Theme Theme);
    private readonly object gate = new();
    private readonly AutoResetEvent wake = new(false);
    private Thread? thread;
    private Window? active;
    private Candidate? next;
    private Request? pending;
    private long version;
    private bool stopping;

    internal void Supersede(long value)
    {
        lock (gate) { version = value; pending = null; }
    }

    internal void Publish(long value, byte[] assembly, Theme theme)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("The designer requires Windows.");
        lock (gate)
        {
            if (stopping || version != value) return;
            pending = new(value, assembly, theme);
            if (thread is null)
            {
                thread = new Thread(Run) { IsBackground = true, Name = "XUI designer preview" };
                thread.SetApartmentState(ApartmentState.STA);
                thread.Start();
            }
            if (active is not null) active.Post(Refresh);
            wake.Set();
        }
    }

    private Request? Take()
    {
        lock (gate)
        {
            var request = pending;
            pending = null;
            return stopping || request?.Version != version ? null : request;
        }
    }

    private Candidate? Build(Request request)
    {
        try { return new Candidate(request); }
        catch (Exception error)
        {
            ReportError(request.Version, "Preview construction failed", error);
            return null;
        }
    }

    private void Refresh()
    {
        var request = Take();
        if (request is null) return;
        var candidate = Build(request);
        if (candidate is null) return;
        lock (gate)
        {
            if (stopping || request.Version != version) { candidate.Dispose(); return; }
            next = candidate;
            active?.Close();
        }
    }

    private void Run()
    {
        try
        {
            while (true)
            {
                lock (gate) { if (stopping) break; }
                var candidate = next;
                next = null;
                if (candidate is null)
                {
                    wake.WaitOne();
                    var request = Take();
                    if (request is null) continue;
                    candidate = Build(request);
                    if (candidate is null) continue;
                }
                using (candidate)
                {
                    lock (gate)
                    {
                        if (stopping) break;
                        active = candidate.Window;
                        active.Post(() => report(candidate.Version, "Preview updated. Component state was reset.", true));
                        if (pending is not null) active.Post(Refresh);
                    }
                    bool failed = false;
                    try { candidate.Window.Run(); }
                    catch (Exception error) { failed = true; ReportError(candidate.Version, "Preview stopped", error); }
                    finally { lock (gate) active = null; }
                    if (!failed) report(candidate.Version, "Preview closed. Edit the source or select Render / reopen.", false);
                }
            }
        }
        catch (Exception error)
        {
            ReportError(version, "Preview host failed", error);
        }
        finally
        {
            next?.Dispose();
            next = null;
        }
    }

    private void ReportError(long value, string message, Exception error)
    {
        var detail = error is TargetInvocationException { InnerException: { } inner } ? inner : error;
        Console.Error.WriteLine($"{message}: {detail}");
        report(value, $"{message}: {detail.Message}", false);
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (stopping) return;
            stopping = true;
            pending = null;
            active?.Post(active.Close);
            wake.Set();
        }
        if (thread is null || thread.Join(TimeSpan.FromSeconds(2))) wake.Dispose();
        else Console.Error.WriteLine("Preview code did not stop within two seconds. The background preview thread will end with the designer process.");
    }

    private sealed class Candidate : IDisposable
    {
        private readonly AssemblyLoadContext context;
        private object? component;
        internal Window Window { get; }
        internal long Version { get; }

        internal Candidate(Request request)
        {
            Version = request.Version;
            Window = new Window("XUI Live Preview", 560, 600, request.Theme);
            context = new("XUI preview", isCollectible: true);
            try
            {
                Window.SetShowActivated(false);
                using var stream = new MemoryStream(request.Assembly, writable: false);
                var assembly = context.LoadFromStream(stream);
                var build = assembly.GetType("Xui.Designer.GeneratedPreview", throwOnError: true)!
                    .GetMethod("Build", BindingFlags.Public | BindingFlags.Static)
                    ?? throw new MissingMethodException("The preview entry point is missing.");
                component = build.Invoke(null, [Window]);
            }
            catch
            {
                Dispose();
                throw;
            }
        }

        public void Dispose()
        {
            Window.Dispose();
            GC.KeepAlive(component);
            component = null;
            context.Unload();
        }
    }
}

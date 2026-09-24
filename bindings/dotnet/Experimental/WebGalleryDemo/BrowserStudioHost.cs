using Microsoft.JSInterop;
using PortableDemo;
using Xui.Experimental.Web;

internal sealed class BrowserStudioHost : IDisposable
{
    private readonly WorkspaceStudio studio;
    private readonly BrowserErrorReporter reporter;
    private readonly DotNetObjectReference<BrowserStudioHost> reference;
    private readonly IJSInProcessObjectReference module;
    private readonly IJSInProcessObjectReference binding;
    private bool disposed;
#if DEBUG
    private IJSInProcessObjectReference? probe;
#endif
    private BrowserStudioHost(WorkspaceStudio studio, BrowserErrorReporter reporter, IJSInProcessObjectReference module)
    {
        this.studio = studio;
        this.reporter = reporter;
        this.module = module;
        reference = DotNetObjectReference.Create(this);
        try { binding = module.Invoke<IJSInProcessObjectReference>("bindBack", reference, "app", "errors"); }
        catch { reference.Dispose(); module.Dispose(); throw; }
    }
    internal static async Task<BrowserStudioHost> Create(IJSRuntime js, WorkspaceStudio studio, BrowserErrorReporter reporter, bool test)
    {
        var module = await js.InvokeAsync<IJSInProcessObjectReference>("import", "./studio-host.js");
        var owner = new BrowserStudioHost(studio, reporter, module);
#if DEBUG
        if (test)
        {
            try
            {
                owner.probe = await js.InvokeAsync<IJSInProcessObjectReference>("import", "./studio-probe.js");
                owner.probe.InvokeVoid("install", owner.reference);
            }
            catch { owner.Dispose(); throw; }
        }
#endif
        return owner;
    }
    [JSInvokable]
    public object Back()
    {
        if (disposed) return new { handled = true, blocked = true };
        try { return new { handled = studio.TryNavigateBack(), blocked = studio.BackBlocked }; }
        catch (Exception error) { reporter.Report(error); return new { handled = true, blocked = true }; }
    }
#if DEBUG
    [JSInvokable]
    public async Task WaitForIdle() => await studio.LastOperation;
#endif
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        List<Exception> errors = [];
        void Run(Action action) { try { action(); } catch (Exception error) { errors.Add(error); } }
#if DEBUG
        if (probe is not null) { Run(() => probe.InvokeVoid("uninstall")); Run(probe.Dispose); }
#endif
        Run(() => binding.InvokeVoid("dispose"));
        Run(reference.Dispose);
        Run(binding.Dispose);
        Run(module.Dispose);
        if (errors.Count != 0) throw new AggregateException("Studio host cleanup failed.", errors);
    }
}

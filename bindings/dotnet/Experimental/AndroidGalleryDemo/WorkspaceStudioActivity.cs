using Android.App;
using Android.OS;
using Android.Util;
using Android.Views;
using Android.Widget;
using Android.Window;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidGalleryDemo;

[Activity(Name = "dev.xui.portable.gallery.WorkspaceStudioActivity", Label = "XUI Studio",
    MainLauncher = true, Exported = true, Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class WorkspaceStudioActivity : Activity
{
    private Host? host;
    private WorkspaceStudio? studio;
    private AndroidBackend? backend;
    private OrderSurface? surface;
    private AndroidDispatcher? dispatcher;
    private Action<Exception>? report;
    private BackCallback? back;
    private string? focusedId;
    private TextSelection selection;
    private bool restoreFocus;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        dispatcher = new AndroidDispatcher();
        surface = new OrderSurface(this);
        SetContentView(surface);
        surface.ViewTreeObserver!.GlobalLayout += RestoreInput;
        report = ProfileWorkspaceActivity.CreateReporter(this, dispatcher, "Xui.Android.Studio");
        host = new Host(dispatcher);
        try
        {
            string? json = savedInstanceState?.GetString("studio-session");
            studio = WorkspaceStudio.Create(host, new LocalStudioAnalysisService(), report,
                json is null ? null : WorkspaceStudioSessionCodec.Restore(json));
            focusedId = savedInstanceState?.GetString("studio-focus");
            selection = new(savedInstanceState?.GetInt("studio-start", 0) ?? 0,
                savedInstanceState?.GetInt("studio-end", 0) ?? 0);
            if (OperatingSystem.IsAndroidVersionAtLeast(33))
            {
                back = new BackCallback(this);
                OnBackInvokedDispatcher.RegisterOnBackInvokedCallback(IOnBackInvokedDispatcher.PriorityDefault, back);
            }
        }
        catch (Exception creationError)
        {
            try { host.Dispose(); }
            catch (Exception cleanupError) { throw new AggregateException(creationError, cleanupError); }
            throw;
        }
    }

    protected override void OnStart()
    {
        base.OnStart();
        var attached = new AndroidBackend(surface!, dispatcher!);
        backend = attached;
        host!.Attach(attached);
        studio!.AttachView();
        restoreFocus = focusedId is not null;
    }

    public override void OnWindowFocusChanged(bool hasFocus)
    {
        base.OnWindowFocusChanged(hasFocus);
        if (hasFocus) RestoreInput(this, EventArgs.Empty);
    }

    private void RestoreInput(object? sender, EventArgs args)
    {
        if (HasWindowFocus && restoreFocus && focusedId is not null && backend is { } attached)
        {
            var editor = attached.FindViews(focusedId).OfType<EditText>().SingleOrDefault();
            if (editor is not null && editor.Enabled && editor.IsShown && editor.Width > 0 && editor.Height > 0)
            {
                var range = selection.ClampTo(editor.Text ?? "");
                if (editor.RequestFocus())
                {
                    editor.SetSelection(range.Start, range.End);
                    restoreFocus = false;
                }
            }
        }
    }

    private void CaptureInput()
    {
        if (backend is null || host?.IsAttached != true) return;
        if (restoreFocus) return;
        focusedId = null;
        if (CurrentFocus is not EditText input || input.Tag?.ToString() is not { Length: > 0 } id) return;
        if (!backend.FindViews(id).Any(view => ReferenceEquals(view, input))) return;
        focusedId = id;
        selection = new TextSelection(Math.Max(0, input.SelectionStart), Math.Max(0, input.SelectionEnd)).ClampTo(input.Text ?? "");
    }

    protected override void OnSaveInstanceState(Bundle outState)
    {
        CaptureInput();
        outState.PutString("studio-session", WorkspaceStudioSessionCodec.Serialize(studio!.CaptureSession()));
        if (focusedId is not null)
        {
            outState.PutString("studio-focus", focusedId);
            outState.PutInt("studio-start", selection.Start);
            outState.PutInt("studio-end", selection.End);
        }
        base.OnSaveInstanceState(outState);
    }

    protected override void OnStop()
    {
        try
        {
            CaptureInput();
            host!.Detach();
            backend = null;
            studio!.PrepareForAttachment();
        }
        finally { base.OnStop(); }
    }

    public override void OnBackPressed() => RouteBack();
    private void RouteBack()
    {
        try
        {
            if (studio?.TryNavigateBack() == true) return;
#pragma warning disable CA1422
            base.OnBackPressed();
#pragma warning restore CA1422
        }
        catch (Exception error) { report!(error); }
    }

    protected override void OnDestroy()
    {
        Task pending = studio?.LastOperation ?? Task.CompletedTask;
        var reporter = report;
        try
        {
            if (back is not null)
            {
                if (OperatingSystem.IsAndroidVersionAtLeast(33)) OnBackInvokedDispatcher.UnregisterOnBackInvokedCallback(back);
                back.Dispose();
                back = null;
            }
            host?.Dispose();
        }
        finally
        {
            if (reporter is not null) _ = ObserveRetirement(pending, reporter);
            studio = null;
            backend = null;
            host = null;
            report = null;
            if (surface?.ViewTreeObserver is { IsAlive: true } observer)
                observer.GlobalLayout -= RestoreInput;
            surface?.Dispose();
            base.OnDestroy();
        }
    }

    private static async Task ObserveRetirement(Task pending, Action<Exception> report)
    {
        try { await pending.ConfigureAwait(false); }
        catch (System.OperationCanceledException) { }
        catch (Exception error) { report(error); }
    }

    private sealed class BackCallback(WorkspaceStudioActivity activity) : Java.Lang.Object, IOnBackInvokedCallback
    {
        private readonly WeakReference<WorkspaceStudioActivity> owner = new(activity);
        public void OnBackInvoked()
        {
            if (owner.TryGetTarget(out var current) && current.Handle != IntPtr.Zero &&
                !current.IsDestroyed && !current.IsFinishing) current.RouteBack();
        }
    }
}

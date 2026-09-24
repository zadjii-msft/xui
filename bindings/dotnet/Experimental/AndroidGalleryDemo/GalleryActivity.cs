using Android.App;
using Android.OS;
using Android.Views;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidGalleryDemo;

public abstract class GalleryActivity : Activity
{
    private Host? host;
    private OrderSurface? surface;
    private AndroidDispatcher? dispatcher;
    private AndroidBackend? backend;
    private string? focusedId;
    private int selectionStart;
    private int selectionEnd;
    protected AndroidDispatcher UiDispatcher => dispatcher ?? throw new InvalidOperationException("The Activity dispatcher is not available.");

    protected abstract IReadOnlyList<string> InputIds { get; }
    protected abstract void CreateComponent(Host owner, string? savedState);
    protected abstract string SaveComponent();
    protected abstract void ReleaseComponent();

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        dispatcher = new AndroidDispatcher();
        surface = new OrderSurface(this);
        SetContentView(surface);
        host = new Host(dispatcher);
        try
        {
            CreateComponent(host, savedInstanceState?.GetString("gallery.state"));
            focusedId = savedInstanceState?.GetString("gallery.focus");
            selectionStart = savedInstanceState?.GetInt("gallery.selectionStart") ?? 0;
            selectionEnd = savedInstanceState?.GetInt("gallery.selectionEnd") ?? 0;
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
        backend = new AndroidBackend(surface!, dispatcher!);
        host!.Attach(backend);
        if (focusedId is not null)
        {
            var input = (EditText)backend.FindViews(focusedId).Single();
            input.RequestFocus();
            int length = input.Text?.Length ?? 0;
            input.SetSelection(Math.Clamp(selectionStart, 0, length), Math.Clamp(selectionEnd, 0, length));
        }
    }

    private void CaptureInput()
    {
        if (!host!.IsAttached) return;
        focusedId = null;
        foreach (string id in InputIds)
        {
            var input = (EditText)backend!.FindViews(id).Single();
            if (!input.HasFocus) continue;
            focusedId = id;
            selectionStart = input.SelectionStart;
            selectionEnd = input.SelectionEnd;
            break;
        }
    }

    protected override void OnSaveInstanceState(Bundle outState)
    {
        CaptureInput();
        outState.PutString("gallery.state", SaveComponent());
        outState.PutString("gallery.focus", focusedId);
        outState.PutInt("gallery.selectionStart", selectionStart);
        outState.PutInt("gallery.selectionEnd", selectionEnd);
        base.OnSaveInstanceState(outState);
    }

    protected override void OnStop()
    {
        try
        {
            CaptureInput();
            host!.Detach();
        }
        finally
        {
            backend = null;
            base.OnStop();
        }
    }

    protected override void OnDestroy()
    {
        try { host?.Dispose(); }
        finally
        {
            host = null;
            ReleaseComponent();
            surface?.Dispose();
            base.OnDestroy();
        }
    }
}

[Activity(Name = "dev.xui.portable.gallery.TaskBoardActivity", Label = "XUI task board",
    MainLauncher = true, Exported = true, Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class TaskBoardActivity : GalleryActivity
{
    private TaskBoard? component;
    protected override IReadOnlyList<string> InputIds { get; } = ["task-name"];
    protected override void CreateComponent(Host owner, string? savedState)
    {
        component = new TaskBoard(owner);
        if (savedState is not null) component.State = GalleryStateCodec.RestoreTaskBoard(savedState);
    }
    protected override string SaveComponent() => GalleryStateCodec.Serialize(component!.State);
    protected override void ReleaseComponent() => component = null;
}

[Activity(Name = "dev.xui.portable.gallery.ExpenseLedgerActivity", Label = "XUI expense ledger",
    MainLauncher = true, Exported = true, Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class ExpenseLedgerActivity : GalleryActivity
{
    private ExpenseLedger? component;
    protected override IReadOnlyList<string> InputIds { get; } =
        ["ledger-budget", "ledger-food", "ledger-travel", "ledger-supplies"];
    protected override void CreateComponent(Host owner, string? savedState)
    {
        component = new ExpenseLedger(owner);
        if (savedState is not null) component.State = GalleryStateCodec.RestoreExpenseLedger(savedState);
    }
    protected override string SaveComponent() => GalleryStateCodec.Serialize(component!.State);
    protected override void ReleaseComponent() => component = null;
}

[Activity(Name = "dev.xui.portable.gallery.SessionPlannerActivity", Label = "XUI session planner",
    MainLauncher = true, Exported = true, Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class SessionPlannerActivity : GalleryActivity
{
    private SessionPlanner? component;
    protected override IReadOnlyList<string> InputIds { get; } =
        ["planner-start", "planner-first", "planner-second", "planner-third", "planner-break"];
    protected override void CreateComponent(Host owner, string? savedState)
    {
        component = new SessionPlanner(owner);
        if (savedState is not null) component.State = GalleryStateCodec.RestoreSessionPlanner(savedState);
    }
    protected override string SaveComponent() => GalleryStateCodec.Serialize(component!.State);
    protected override void ReleaseComponent() => component = null;
}

[Activity(Name = "dev.xui.portable.gallery.DynamicTaskBoardActivity", Label = "XUI dynamic task board",
    MainLauncher = true, Exported = true, Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class DynamicTaskBoardActivity : GalleryActivity
{
    private DynamicTaskBoard? component;
    protected override IReadOnlyList<string> InputIds => component is null ? [] :
        ["dynamic-draft", .. component.GetState().VisibleItems.Select(item => item.Key + "-title")];
    protected override void CreateComponent(Host owner, string? savedState) =>
        component = DynamicTaskBoard.Create(owner, savedState is null ? null : DynamicTaskBoardCodec.Restore(savedState));
    protected override string SaveComponent() => DynamicTaskBoardCodec.Serialize(component!.GetState());
    protected override void ReleaseComponent() => component = null;
}

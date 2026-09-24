using Android.App;
using Android.OS;
using Android.Util;
using Android.Views;
using Android.Widget;
using Android.Window;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidGalleryDemo;

[Activity(Name = "dev.xui.portable.gallery.ProfileWorkspaceActivity", Label = "XUI profile workspace",
    MainLauncher = true, Exported = true, Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed class ProfileWorkspaceActivity : GalleryActivity
{
    private ProfileWorkspace? component;
    private ProfileWorkspaceController? controller;
    private Action<Exception>? reporter;
    private BackCallback? backCallback;

    protected override IReadOnlyList<string> InputIds => component?.GetState().Page == ProfilePage.Edit
        ? ["profile-name", "profile-role", "profile-location", "profile-focus"] : [];

    protected override void CreateComponent(Host owner, string? savedState)
    {
        string directory = FilesDir?.AbsolutePath ?? throw new InvalidOperationException("Android private files storage is unavailable.");
        var storage = new DirectoryApplicationStorage(Path.Combine(directory, "xui-profile"));
        reporter = CreateReporter(this, UiDispatcher);
        var session = savedState is null ? null : ProfileWorkspaceSessionCodec.Restore(savedState);
        component = ProfileWorkspace.Create(owner, storage, reporter, session: session);
        controller = component.Controller;
    }

    protected override string SaveComponent() =>
        ProfileWorkspaceSessionCodec.Serialize(component!.Controller.CaptureSession());

    protected override void ReleaseComponent()
    {
        var pending = controller?.LastOperation;
        component = null;
        controller = null;
        if (pending is not null && reporter is not null) _ = ObserveRetirement(pending, reporter);
        reporter = null;
    }

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        if (OperatingSystem.IsAndroidVersionAtLeast(33))
        {
            backCallback = new BackCallback(this);
            OnBackInvokedDispatcher.RegisterOnBackInvokedCallback(IOnBackInvokedDispatcher.PriorityDefault, backCallback);
        }
    }

    public override void OnBackPressed() => RouteBack();

    private void RouteBack()
    {
        try
        {
            if (controller is { } current && !current.State.Busy && current.NavigationDepth > 1)
                current.Back();
            else
            {
                // Preserve Activity's platform default (including root-task backgrounding).
#pragma warning disable CA1422
                base.OnBackPressed();
#pragma warning restore CA1422
            }
        }
        catch (Exception error)
        {
            Log.Error("Xui.Android.Profile", error.ToString());
            throw;
        }
    }

    protected override void OnDestroy()
    {
        try
        {
            if (backCallback is not null)
            {
                if (OperatingSystem.IsAndroidVersionAtLeast(33))
                    OnBackInvokedDispatcher.UnregisterOnBackInvokedCallback(backCallback);
                backCallback.Dispose();
                backCallback = null;
            }
        }
        finally { base.OnDestroy(); }
    }

    internal static Action<Exception> CreateReporter(Activity activity, AndroidDispatcher dispatcher, string tag = "Xui.Android.Profile")
    {
        var weak = new WeakReference<Activity>(activity);
        return error =>
        {
            Log.Error(tag, error.ToString());
            try
            {
                dispatcher.Post(() =>
                {
                    if (!weak.TryGetTarget(out var owner) || owner.Handle == IntPtr.Zero ||
                        owner.IsDestroyed || owner.IsFinishing || !owner.HasWindowFocus) return;
                    try
                    {
                        var toast = Toast.MakeText(owner.ApplicationContext, "Operation failed. See " + tag + " in logcat.", ToastLength.Long)
                            ?? throw new InvalidOperationException("Android could not create the profile error notification.");
                        toast.Show();
                    }
                    catch (Exception notificationError) { Log.Error(tag, notificationError.ToString()); }
                });
            }
            catch (Exception notificationError) { Log.Error(tag, notificationError.ToString()); }
        };
    }

    private static async Task ObserveRetirement(Task pending, Action<Exception> report)
    {
        try { await pending.ConfigureAwait(false); }
        catch (System.OperationCanceledException) { }
        catch (Exception error) { report(error); }
    }

    private sealed class BackCallback(ProfileWorkspaceActivity owner) : Java.Lang.Object, IOnBackInvokedCallback
    {
        private readonly WeakReference<ProfileWorkspaceActivity> activity = new(owner);
        public void OnBackInvoked()
        {
            if (activity.TryGetTarget(out var current) && current.Handle != IntPtr.Zero &&
                !current.IsDestroyed && !current.IsFinishing) current.RouteBack();
        }
    }
}

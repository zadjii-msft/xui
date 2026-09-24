using Android.Graphics;
using Android.Graphics.Drawables;
using Android.Util;
using Android.Views.Accessibility;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private void ValueControlChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var settings = new SettingsShowcase(host);
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        var driver = new SettingsDriver(backend);
        using var stream = typeof(TestActivity).Assembly.GetManifestResourceStream("SettingsScenarios.json")
            ?? throw new InvalidOperationException("The shared settings scenario resource is missing.");
        using var reader = new StreamReader(stream);
        int shared = SettingsScenarioRunner.Run(reader.ReadToEnd(), driver);
        assertions += shared;
        driver.Click("reset-settings");
        settings.Notifications = false;
        settings.Sync = CheckState.Checked;
        Assert(settings.Changes == 0 && !driver.Checked("notifications") && driver.CheckState("sync-policy") == "Checked",
            "Programmatic native value changes are silent.");
        driver.Click("reset-settings");
        driver.Change("settings-draft", "Draft");
        var input = (EditText)backend.FindViews("settings-draft").Single();
        input.RequestFocus();
        input.SetSelection(1, 4);
        using var editable = input.EditableText!;
        BaseInputConnection.SetComposingSpans(editable);
        int start = BaseInputConnection.GetComposingSpanStart(editable);
        int end = BaseInputConnection.GetComposingSpanEnd(editable);
        driver.Click("notifications");
        driver.Click("sync-policy");
        driver.Click("advance-progress");
        Assert(ReferenceEquals(input, backend.FindViews("settings-draft").Single()) &&
            input.HasFocus && input.Text == "Draft" && input.SelectionStart == 1 && input.SelectionEnd == 4 &&
            start >= 0 && BaseInputConnection.GetComposingSpanStart(editable) == start &&
            BaseInputConnection.GetComposingSpanEnd(editable) == end,
            "Native value controls and progress updates preserve unrelated input identity and composition.");
        BaseInputConnection.RemoveComposingSpans(editable);
        int changes = settings.Changes;
        settings.Content.Enabled = false;
        driver.Click("notifications");
        driver.Click("sync-policy");
        Assert(settings.Changes == changes, "Disabled ancestry rejects native value callbacks.");
        settings.Content.Enabled = true;
        settings.Content.Visible = false;
        driver.Click("notifications");
        driver.Click("sync-policy");
        Assert(settings.Changes == changes, "Hidden ancestry rejects native value callbacks.");
        settings.Content.Visible = true;
        settings.Sync = CheckState.Indeterminate;
        settings.CycleMixed = false;
        var check = (NativeChoice)backend.FindViews("sync-policy").Single();
        using (var info = check.CreateAccessibilityNodeInfo()
            ?? throw new InvalidOperationException("Native checkbox accessibility information is missing."))
        {
            Assert(info.Checkable && !check.Checked && driver.CheckState("sync-policy") == "Indeterminate",
                "The native mixed checkbox exposes a distinct accessible state, not checked=true.");
        }
        var mixed = check.ButtonDrawable ?? throw new InvalidOperationException("The mixed checkbox has no native indicator.");
        Assert(mixed is LayerDrawable && mixed.IntrinsicWidth > 0 && mixed.IntrinsicHeight > 0,
            "Mixed state uses a real native indicator with intrinsic dimensions.");
        using (var bounds = mixed.CopyBounds())
        using (var bitmap = Bitmap.CreateBitmap(mixed.IntrinsicWidth, mixed.IntrinsicHeight, Bitmap.Config.Argb8888!))
        using (var canvas = new Canvas(bitmap))
        {
            try
            {
                mixed.SetBounds(0, 0, bitmap.Width, bitmap.Height);
                mixed.Draw(canvas);
                Assert(((uint)bitmap.GetPixel(bitmap.Width / 2, bitmap.Height / 2) >> 24) > 0 &&
                    ((uint)bitmap.GetPixel(bitmap.Width / 2, bitmap.Height / 3) >> 24) == 0,
                    "The native mixed indicator renders a minus mark with an unfilled interior.");
            }
            finally { mixed.SetBounds(bounds.Left, bounds.Top, bounds.Right, bounds.Bottom); }
        }
        driver.Click("sync-policy");
        Assert(driver.CheckState("sync-policy") == "Unchecked" && settings.Changes == changes + 1,
            "Binary cycling preserves mixed programmatic state until one user activation moves to unchecked.");

        settings.UnitProgress.Range = new NumericRange(1e100, 2e100);
        var progress = (ProgressBar)backend.FindViews("unit-progress").Single();
        using (var info = progress.CreateAccessibilityNodeInfo()
            ?? throw new InvalidOperationException("Native progress accessibility information is missing."))
        {
            Assert(info.GetRangeInfo() is null && info.Extras!.GetDouble(NativeProgress.MinimumKey) == 1e100 &&
                info.Extras.GetDouble(NativeProgress.ValueKey) == 1e100 && progress.Progress == 0,
                "Large authored double ranges retain exact accessibility metadata without infinite float range information.");
        }
        settings.UnitProgress.Range = new NumericRange(1, Math.BitIncrement(1));
        settings.UnitProgress.Value = 1;
        using (var info = progress.CreateAccessibilityNodeInfo()!)
            Assert(info.GetRangeInfo() is null && info.Extras!.GetDouble(NativeProgress.MaximumKey) == Math.BitIncrement(1),
                "Float-collapsed ranges omit misleading native range information.");
        var views = CaptureViews(surface.GetChildAt(0)!);
        host.Dispose();
        Assert(views.All(view => view.Handle == IntPtr.Zero) && mixed.Handle == IntPtr.Zero,
            "Value-control disposal releases views and the owned mixed-state drawable.");
        Log.Info("Xui.Android.Orders", $"Settings: {shared} shared expectations; {assertions - before - shared} native value-control assertions.");
    }

    private sealed class SettingsDriver(AndroidBackend backend) : ISettingsScenarioDriver
    {
        private readonly NativeDriver controls = new(backend);
        public void Click(string id) => controls.Click(id);
        public void Change(string id, string value) => controls.Change(id, value);
        public string Text(string id) => controls.Text(id);
        public bool Enabled(string id) => controls.Enabled(id);
        public bool Visible(string id) => controls.Visible(id);
        public bool Checked(string id) => ((CompoundButton)backend.FindViews(id).Single()).Checked;
        public string CheckState(string id)
        {
            var view = (CompoundButton)backend.FindViews(id).Single();
            using var info = view.CreateAccessibilityNodeInfo() ?? throw new InvalidOperationException("Missing checkbox accessibility information.");
            if (OperatingSystem.IsAndroidVersionAtLeast(36))
                return info.CheckedState == global::Android.Views.Accessibility.CheckedState.Partial ? "Indeterminate" :
                    info.CheckedState == global::Android.Views.Accessibility.CheckedState.True ? "Checked" : "Unchecked";
            bool mixed = OperatingSystem.IsAndroidVersionAtLeast(30)
                ? info.StateDescription == "Mixed"
                : info.ContentDescription?.EndsWith(", Mixed", StringComparison.Ordinal) == true;
            return mixed ? "Indeterminate" : view.Checked ? "Checked" : "Unchecked";
        }
        public SettingsProgressSnapshot Progress(string id)
        {
            var view = (ProgressBar)backend.FindViews(id).Single();
            using var info = view.CreateAccessibilityNodeInfo() ?? throw new InvalidOperationException("Missing progress accessibility information.");
            var extras = info.Extras ?? throw new InvalidOperationException("Missing native progress metadata.");
            if (!extras.ContainsKey(NativeProgress.MinimumKey) || !extras.ContainsKey(NativeProgress.MaximumKey) ||
                !extras.ContainsKey(NativeProgress.IndeterminateKey))
                throw new InvalidOperationException("Incomplete native progress metadata.");
            double minimum = extras.GetDouble(NativeProgress.MinimumKey);
            double maximum = extras.GetDouble(NativeProgress.MaximumKey);
            bool indeterminate = extras.GetBoolean(NativeProgress.IndeterminateKey);
            if (view.Indeterminate != indeterminate) throw new InvalidOperationException("Native progress state disagrees with its accessibility state.");
            using var nativeRange = info.GetRangeInfo();
            if (indeterminate)
            {
                if (extras.ContainsKey(NativeProgress.ValueKey) || nativeRange is not null)
                    throw new InvalidOperationException("Indeterminate progress exposes a current value.");
                return new(minimum, maximum, null, true);
            }
            if (!extras.ContainsKey(NativeProgress.ValueKey)) throw new InvalidOperationException("Determinate progress omits its current value.");
            double value = extras.GetDouble(NativeProgress.ValueKey);
            int expected = (int)Math.Round((value - minimum) / (maximum - minimum) * 10000, MidpointRounding.AwayFromZero);
            if (view.Min != 0 || view.Max != 10000 || view.Progress != expected || nativeRange is null ||
                nativeRange.Min != (float)minimum || nativeRange.Max != (float)maximum || nativeRange.Current != (float)value)
                throw new InvalidOperationException("Native progress visuals or standard accessibility range disagree with authored units.");
            return new(minimum, maximum, value, false);
        }
    }
}

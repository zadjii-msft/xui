using Android.App;
using Android.OS;
using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.Portable;
using Axis = Xui.Experimental.Portable.Axis;
using NativeButton = Android.Widget.Button;
using NativeScrollView = Android.Widget.ScrollView;

namespace Xui.Experimental.AndroidDeviceTests;

[Activity(Label = "XUI Android checks", MainLauncher = true, Exported = true,
    Theme = "@android:style/Theme.Material.Light.NoActionBar")]
public sealed class TestActivity : Activity
{
    private int assertions;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        RunTests();
    }

    private void Assert(bool condition, string description)
    {
        if (!condition) throw new InvalidOperationException(description);
        assertions++;
    }

    private async void RunTests()
    {
        try
        {
            using var surface = new FrameLayout(this);
            SetContentView(surface);
            var dispatcher = new AndroidDispatcher();
            using var host = new Host(dispatcher);
            var demo = new Greeting(host);
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var input = (EditText)backend.FindViews("name").Single();
            var increment = (NativeButton)backend.FindViews("increment").Single();
            int changed = 0;
            demo.Input.Changed += _ => changed++;
            input.Text = "Ada";
            Assert(demo.Entry == "Ada" && changed == 1, "Native EditText callback updates authored C#.");
            input.RequestFocus();
            input.SetSelection(1, 2);
            using var editable = input.EditableText!;
            BaseInputConnection.SetComposingSpans(editable);
            int composingStart = BaseInputConnection.GetComposingSpanStart(editable);
            int composingEnd = BaseInputConnection.GetComposingSpanEnd(editable);
            Assert(composingStart >= 0 && composingEnd > composingStart, "The composition fixture contains active spans.");
            increment.PerformClick();
            demo.Root.Padding(20);
            Assert(demo.Count == 1, "Native button runs authored C#.");
            Assert(ReferenceEquals(input, backend.FindViews("name").Single()), "Input identity remains stable.");
            Assert(input.HasFocus && input.SelectionStart == 1 && input.SelectionEnd == 2, "Unrelated updates preserve focus and selection.");
            Assert(BaseInputConnection.GetComposingSpanStart(editable) == composingStart &&
                BaseInputConnection.GetComposingSpanEnd(editable) == composingEnd, "Unrelated updates preserve composition spans.");
            input.OnEditorAction(ImeAction.Done);
            Assert(demo.Message == "Hello, Ada!", "The IME action submits authored C#.");
            demo.Entry = "Grace";
            Assert(input.Text == "Grace" && changed == 1, "Programmatic updates suppress native change callbacks.");
            Assert(input.SelectionStart == 1 && input.SelectionEnd == 2, "Programmatic replacement clamps existing selection.");
            demo.Count = 10;
            Assert(!increment.Enabled, "Authored enabled binding reaches Android.");
            ((NativeButton)backend.FindViews("reset").Single()).PerformClick();
            Assert(demo.Count == 0 && input.Text == "" && changed == 1, "Reset is silent and uses the retained EditText.");
            demo.Input.SetCaptionVisible(false);
            demo.Input.Name = "Account name";
            demo.Input.Help = "Enter your first name";
            demo.Input.SetPlaceholder("Name");
            Assert(input.ContentDescription == "Account name" && input.Hint == "Name", "Hidden caption preserves the native accessible name.");
            Assert(input.TooltipText == "Enter your first name", "Help reaches the native tooltip.");
            var scrollModel = (Xui.Experimental.Portable.ScrollView)demo.Root.Children[1];
            scrollModel.Enabled = false;
            Assert(!input.Enabled && !increment.Enabled, "Disabled ancestor disables native descendants.");
            scrollModel.Enabled = true;
            Measure(surface, 320, 160);
            var scroll = (NativeScrollView)backend.FindViews("content").Single();
            Assert(scroll.GetChildAt(0)!.MeasuredHeight > scroll.MeasuredHeight, "Scroll content keeps its unbounded desired height.");
            int oldTop = scroll.ScrollY;
            scroll.ScrollTo(0, 100);
            Assert(scroll.ScrollY > oldTop, "Native scrolling has a real nonzero range.");
            await Task.Run(async () => await host.DispatchAsync(() => demo.Count = 3));
            Assert(demo.Count == 3 && dispatcher.CheckAccess(), "Background dispatch reaches the Android UI thread.");
            host.Detach();
            Assert(surface.ChildCount == 0, "Detach unmounts the owned root.");
            bool oldWidgetRejected = false;
            try { increment.PerformClick(); }
            catch (ObjectDisposedException) { oldWidgetRejected = true; }
            Assert(oldWidgetRejected || demo.Count == 3, "Disposed widgets cannot call authored handlers.");
            var replacement = new AndroidBackend(surface, dispatcher);
            host.Attach(replacement);
            Assert(!ReferenceEquals(input, replacement.FindViews("name").Single()), "Reattachment creates new peers.");
            Assert(demo.Count == 3, "Detach retains the authored state.");
            host.Dispose();
            Assert(surface.ChildCount == 0, "Terminal disposal unmounts the root.");
            LayoutCases(surface, dispatcher);
            string result = $"PASS: {assertions} Android native assertions.";
            Log.Info("Xui.Android.Tests", result);
            SetContentView(new TextView(this) { Text = result });
        }
        catch (Exception error)
        {
            Log.Error("Xui.Android.Tests", $"FAIL: {error}");
            throw;
        }
    }

    private void LayoutCases(FrameLayout surface, AndroidDispatcher dispatcher)
    {
        using var host = new Host(dispatcher);
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical).Padding(10).Spacing(8);
            var first = host.Label("first");
            first.AutomationId = "duplicate";
            ElementExtensions.FixedSize(first, 500, 40);
            var hidden = host.Label("hidden");
            hidden.Visible = false;
            var row = host.Stack(Axis.Horizontal).Spacing(4);
            var left = host.Button("left");
            left.AutomationId = "left";
            var right = host.Button("right");
            right.AutomationId = "right";
            row.Add(left, 1).Add(right, 2);
            var last = host.Label("last");
            last.AutomationId = "duplicate";
            root.Add(first).Add(hidden).Add(row, 1).Add(last);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        Measure(surface, 300, 300);
        var duplicates = backend.FindViews("duplicate");
        Assert(duplicates.Count == 2 && !ReferenceEquals(duplicates[0], duplicates[1]), "Automation IDs stay host-scoped and preserve duplicates.");
        var firstFrame = (View)duplicates[0].Parent!;
        int dp = (int)Math.Round(Resources!.DisplayMetrics!.Density * 10, MidpointRounding.AwayFromZero);
        Assert(firstFrame.MeasuredWidth == Math.Max(0, surface.MeasuredWidth - 2 * dp), "Fixed width is bounded by the parent allocation.");
        var leftView = backend.FindViews("left").Single();
        var rightView = backend.FindViews("right").Single();
        Assert(Math.Abs(rightView.MeasuredWidth - 2 * leftView.MeasuredWidth) <= 2, "Horizontal flex uses the authored weights.");
        var rootView = (ViewGroup)firstFrame.Parent!;
        var rowFrame = rootView.GetChildAt(2)!;
        int spacing = (int)Math.Round(Resources.DisplayMetrics.Density * 8, MidpointRounding.AwayFromZero);
        Assert(rowFrame.Top == firstFrame.Bottom + spacing, "Gone children do not add spacing.");
    }

    private void Measure(View view, int widthDp, int heightDp)
    {
        float density = Resources!.DisplayMetrics!.Density;
        int width = (int)Math.Round(widthDp * density);
        int height = (int)Math.Round(heightDp * density);
        view.Measure(View.MeasureSpec.MakeMeasureSpec(width, MeasureSpecMode.Exactly),
            View.MeasureSpec.MakeMeasureSpec(height, MeasureSpecMode.Exactly));
        view.Layout(0, 0, width, height);
    }
}

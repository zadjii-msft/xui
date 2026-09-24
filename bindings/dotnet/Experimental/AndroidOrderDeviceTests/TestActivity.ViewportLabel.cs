using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;
using Axis = Xui.Experimental.Portable.Axis;
using Size = Xui.Experimental.Portable.Size;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task ViewportLabelChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var mount = new FrameLayout(this);
        float density = Resources!.DisplayMetrics!.Density;
        int Px(float value) => Xui.Experimental.Android.LayoutMath.Pixels(value, density);
        mount.SetPadding(Px(8), Px(4), Px(8), Px(4));
        surface.AddView(mount, new FrameLayout.LayoutParams(Px(336), Px(488)));
        using (var host = new Host(dispatcher))
        {
            var greeting = new Greeting(host);
            var backend = new AndroidBackend(mount, dispatcher);
            host.Attach(backend);
            var sizes = new List<Size>();
            using var observation = host.ObserveViewport(sizes.Add);
            Assert(sizes.Count == 0, "Owned viewport observation does not synchronously invoke authored code.");
            await WaitFor(() => sizes.Count != 0 && mount.Width != 0 && HasWindowFocus);
            var input = (EditText)backend.FindViews("name").Single();
            input.Text = "Retained viewport";
            host.TryFocus(greeting.Input);
            await host.DispatchAsync(() => { });
            host.SetSelection(greeting.Input, new(1, 4));
            using var editable = input.EditableText!;
            BaseInputConnection.SetComposingSpans(editable);
            int start = BaseInputConnection.GetComposingSpanStart(editable);
            foreach (int width in new[] { 719, 720, 1119, 1120, 320 })
            {
                mount.LayoutParameters = new FrameLayout.LayoutParams(Px(width) + mount.PaddingLeft + mount.PaddingRight, Px(488));
                await WaitFor(() => sizes[^1].Width == (mount.Width - mount.PaddingLeft - mount.PaddingRight) / density &&
                    Math.Abs(sizes[^1].Width - width) < 0.4);
                var expectedMode = width >= 1120 ? WidthMode.Expanded : width >= 720 ? WidthMode.Medium : WidthMode.Compact;
                Assert(WidthBreakpoints.Default.Select(sizes[^1].Width) == expectedMode,
                    $"Actual native mount allocation selects the exact {width}dp responsive boundary.");
                Assert(ReferenceEquals(input, backend.FindViews("name").Single()) && input.HasFocus &&
                    input.SelectionStart == 1 && input.SelectionEnd == 4 &&
                    BaseInputConnection.GetComposingSpanStart(editable) == start,
                    $"Responsive viewport at {width}dp retains identity={ReferenceEquals(input, backend.FindViews("name").Single())}, focus={input.HasFocus}, selection={input.SelectionStart}/{input.SelectionEnd}, composition={BaseInputConnection.GetComposingSpanStart(editable)}/{start}.");
            }
            int previousWidth = mount.Width;
            mount.SetPadding(Px(16), Px(8), Px(16), Px(8));
            await WaitFor(() => sizes[^1].Width == (previousWidth - Px(32)) / density);
            Assert(mount.Width == previousWidth && sizes[^1].Height == (mount.Height - mount.PaddingTop - mount.PaddingBottom) / density,
                "Padding-only native layout updates report owned content space, not outer Activity bounds.");
            BaseInputConnection.RemoveComposingSpans(editable);
            host.Detach();
            int count = sizes.Count;
            mount.LayoutParameters = new FrameLayout.LayoutParams(Px(500), Px(500));
            await Task.Delay(30);
            Assert(sizes.Count == count && mount.ChildCount == 0, "Retired viewport subscriptions cannot deliver stale native layout events.");
        }
        surface.RemoveView(mount);
        LabelLayoutChecks(dispatcher);
        Log.Info("Xui.Android.Orders", $"Viewport/label: {assertions - before} native assertions.");

        async Task WaitFor(Func<bool> condition)
        {
            var deadline = DateTime.UtcNow.AddSeconds(10);
            while (!condition())
            {
                if (DateTime.UtcNow > deadline) throw new InvalidOperationException("Native owned viewport allocation did not settle.");
                await Task.Delay(10);
            }
        }
    }

    private void LabelLayoutChecks(AndroidDispatcher dispatcher)
    {
        foreach (float scale in new[] { 1f, 2f })
        {
            using var configuration = new global::Android.Content.Res.Configuration(Resources!.Configuration!) { FontScale = scale };
            using var configured = CreateConfigurationContext(configuration)!;
            using var context = new ContextThemeWrapper(configured, global::Android.Resource.Style.ThemeMaterialLightNoActionBar);
            using var surface = new OrderSurface(context);
            using var host = new Host(dispatcher);
            Label label;
            using (var build = host.BeginBuild())
            {
                var root = host.Stack(Axis.Vertical);
                label = host.Label(new string('W', 160));
                label.AutomationId = "native-label";
                root.Add(label);
                host.SetContent(root);
                build.Complete();
            }
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var text = (TextView)backend.FindViews("native-label").Single();
            int max = text.MaxLines;
            int min = text.MinLines;
            var ellipsize = text.Ellipsize;
            bool horizontal = OperatingSystem.IsAndroidVersionAtLeast(29) && text.IsHorizontallyScrollable;
            var transformation = text.TransformationMethod;
            label.TextLayout = LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis);
            MeasureNative(surface, 120, 480);
            using (var info = text.CreateAccessibilityNodeInfo()!)
                Assert(text.Layout!.LineCount == 1 && text.Layout.GetEllipsisCount(0) > 0 &&
                    text.Text!.Length == 160 && info.Text?.Length == 160 && text.TransformationMethod == transformation,
                    "Native single-line ellipsis keeps the complete text/accessibility value without a whitespace transformation.");
            bool rejected = false;
            try { label.Text = "first\nsecond"; }
            catch (ArgumentException) { rejected = true; }
            Assert(rejected && text.Text!.Length == 160, "A single-line hard break is rejected before native text mutation.");
            label.TextLayout = LabelTextLayout.Wrap(2);
            label.Text = "first line\nsecond line\nthird line";
            MeasureNative(surface, 120, 480);
            Assert(text.MaxLines == 2 && text.Layout!.LineCount >= 2 && text.Text?.Contains('\n') == true,
                "Native wrapping uses the requested line cap and preserves literal hard breaks.");
            label.TextLayout = null;
            MeasureNative(surface, 120, 480);
            Assert(text.MaxLines == max && text.MinLines == min && text.Ellipsize == ellipsize &&
                (!OperatingSystem.IsAndroidVersionAtLeast(29) || text.IsHorizontallyScrollable == horizontal) &&
                text.TransformationMethod == transformation,
                "Null label layout restores captured native wrapping, line limits and ellipsis exactly.");
            host.Dispose();
        }
    }
}

using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableLayout;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;
using Axis = Xui.Experimental.Portable.Axis;
using NativeMath = Xui.Experimental.Android.LayoutMath;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private void AdvancedLayoutChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        float density = surface.Resources!.DisplayMetrics!.Density;
        int Px(float value) => NativeMath.Pixels(value, density);
        using (var host = new Host(dispatcher))
        {
            var demo = new AxisSizingShowcase(host);
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var input = (EditText)backend.FindViews("axes-input").Single();
            MeasureNative(surface, 360, 480);
            Assert(ElementBox(input).Width == Px(280), "A width override replaces only the legacy width.");
            AssertCompleteEditor(input, "Independent-axis editor");
            var wrapper = (ViewGroup)input.Parent!;
            Assert(ElementBox(input).Height == wrapper.GetChildAt(0)!.Height + input.Height,
                "Explicit Auto height measures the complete native caption and editor instead of legacy fixed height.");
            input.Text = "A\U0001F642B";
            host.TryFocus(demo.Input);
            host.SetSelection(demo.Input, new(1, 3));
            using var editable = input.EditableText!;
            BaseInputConnection.SetComposingSpans(editable);
            int start = BaseInputConnection.GetComposingSpanStart(editable);
            int end = BaseInputConnection.GetComposingSpanEnd(editable);
            new NativeDriver(backend).Click("axes-narrow");
            MeasureNative(surface, 360, 480);
            Assert(ElementBox(input).Width == Px(220) && ReferenceEquals(input, backend.FindViews("axes-input").Single()) &&
                host.HasFocus(demo.Input) && host.GetSelection(demo.Input) == new TextSelection(1, 3) &&
                start >= 0 && BaseInputConnection.GetComposingSpanStart(editable) == start &&
                BaseInputConnection.GetComposingSpanEnd(editable) == end,
                "Axis updates retain the real editor, focus, selection and composing range.");
            BaseInputConnection.RemoveComposingSpans(editable);
            new NativeDriver(backend).Click("axes-inherit");
            MeasureNative(surface, 360, 480);
            Assert(ElementBox(input).Width == Px(180) && ElementBox(input).Height == Px(90),
                "Null axis overrides restore both legacy fixed dimensions.");
            new NativeDriver(backend).Click("axes-preserve-height");
            MeasureNative(surface, 360, 480);
            var widthOnly = backend.FindViews("width-only-input").Single();
            Assert(ElementBox(widthOnly).Width == Px(240) && ElementBox(widthOnly).Height == Px(96),
                "A generated width update preserves an independently set height.");
            demo.HeightOnlyInput.SetWidth(200);
            demo.FieldHeight = 100;
            demo.FieldHeight = AxisConstraints.Auto;
            MeasureNative(surface, 360, 480);
            Assert(ElementBox(backend.FindViews("height-only-input").Single()).Width == Px(200),
                "A generated height update preserves an independently set width.");
            var first = ElementBox(backend.FindViews("first-minimum").Single());
            var second = ElementBox(backend.FindViews("second-minimum").Single());
            Assert(first.Width == Px(120) && second.Width == Px(180) - Px(120) &&
                second.Right <= ((View)second.Parent!).Width,
                "A smaller parent allocation wins over competing minimum widths.");
            var capped = ElementBox(backend.FindViews("capped-child").Single());
            var flex = ElementBox(backend.FindViews("flex-child").Single());
            int expectedOffset = (int)Math.Floor(Px(60) + (Px(180) - Px(60)) / 2f);
            Assert(capped.Width == Px(40) && flex.Left == expectedOffset && flex.Right <= Px(180),
                "Native flex placement uses shared slots while child maximums remain bounded.");
            var unboundedFirst = ElementBox(backend.FindViews("unbounded-first").Single());
            var unboundedSecond = ElementBox(backend.FindViews("unbounded-second").Single());
            Assert(unboundedFirst.Height == Px(32) && unboundedSecond.Height == Px(48),
                "Scroll descendants keep natural flex sizes after finite native arrangement.");
            demo.UnboundedRow.SetHeight(new AxisConstraints(Maximum: 100));
            MeasureNative(surface, 360, 480);
            Assert(((View)unboundedFirst.Parent!).Height == Px(32) + Px(8) + Px(48),
                "Auto maximum caps preserve unbounded context instead of growing flex children.");
        }

        using (var host = new Host(dispatcher))
        {
            var demo = new GridSizingShowcase(host);
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            MeasureNative(surface, 360, 480);
            var input = (EditText)backend.FindViews("grid-input").Single();
            AssertCompleteEditor(input, "Spanned Grid editor");
            input.Text = "Retained grid draft";
            host.TryFocus(demo.Input);
            host.SetSelection(demo.Input, new(1, 5));
            using var editable = input.EditableText!;
            BaseInputConnection.SetComposingSpans(editable);
            int start = BaseInputConnection.GetComposingSpanStart(editable);
            int end = BaseInputConnection.GetComposingSpanEnd(editable);
            new NativeDriver(backend).Click("grid-compact");
            MeasureNative(surface, 360, 480);
            Assert(ReferenceEquals(input, backend.FindViews("grid-input").Single()) && input.Text == "Retained grid draft" &&
                host.HasFocus(demo.Input) && host.GetSelection(demo.Input) == new TextSelection(1, 5) &&
                start >= 0 && BaseInputConnection.GetComposingSpanStart(editable) == start &&
                BaseInputConnection.GetComposingSpanEnd(editable) == end,
                "Grid track updates retain native input and composition.");
            BaseInputConnection.RemoveComposingSpans(editable);
            var grid = (ViewGroup)ElementBox(input).Parent!;
            Assert(ElementBox(input).Left == Px(40) && ElementBox(input).Right <= grid.Width,
                "Grid placement honors fixed columns and parent-clipped spans.");
            new NativeDriver(backend).Click("grid-toggle-note");
            MeasureNative(surface, 360, 480);
            var note = backend.FindViews("grid-note").Single();
            new NativeDriver(backend).Click("grid-toggle-note");
            MeasureNative(surface, 360, 480);
            Assert(note.Handle == IntPtr.Zero && backend.FindViews("grid-note").Count == 0 &&
                ReferenceEquals(input, backend.FindViews("grid-input").Single()),
                "A keyed scope inside a static Grid adds and retires peers without replacing sibling cells.");
            Assert(ElementBox(backend.FindViews("grid-natural-first").Single()).Height == Px(40) &&
                ElementBox(backend.FindViews("grid-natural-second").Single()).Height == Px(80),
                "Unbounded star Grid rows remain natural inside a finite scroll viewport.");
            Assert(ElementBox(backend.FindViews("fixed-scope-first").Single()).Height == Px(48) &&
                ElementBox(backend.FindViews("fixed-scope-second").Single()).Height == Px(144),
                "An all-fixed Grid span establishes a bounded flex budget.");
            Assert(ElementBox(backend.FindViews("mixed-scope-first").Single()).Height == Px(32) &&
                ElementBox(backend.FindViews("mixed-scope-second").Single()).Height == Px(48),
                $"A mixed fixed/auto span preserves natural descendant flex sizes: actual {ElementBox(backend.FindViews("mixed-scope-first").Single()).Height}/{ElementBox(backend.FindViews("mixed-scope-second").Single()).Height}, expected {Px(32)}/{Px(48)}.");
            demo.Layout.SetConstraints(0, 0);
            MeasureNative(surface, 360, 480);
            Assert(ElementBox(input).Width == 0 && ElementBox(input).Height == 0 &&
                grid.MeasuredWidth == 0 && grid.MeasuredHeight == 0,
                "Zero Grid allocations remain zero without overflow or invalid native dimensions.");
        }

        using (var host = new Host(dispatcher))
        {
            using (var build = host.BeginBuild())
            {
                var root = host.Stack(Axis.Vertical).Spacing(8);
                var first = host.Label("First");
                first.AutomationId = "context-first";
                ElementExtensions.PreferredSize(first, 100, 32);
                var second = host.Label("Second");
                second.AutomationId = "context-second";
                ElementExtensions.PreferredSize(second, 100, 48);
                root.Add(first, 1).Add(second, 3);
                host.SetContent(root);
                build.Complete();
            }
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var frame = (ElementFrame)surface.GetChildAt(0)!;
            void MeasureContext(bool unbounded)
            {
                frame.MeasureWith(MeasureConstraint.Exactly(Px(300)), MeasureConstraint.Exactly(Px(200), unbounded));
                frame.Layout(0, 0, frame.MeasuredWidth, frame.MeasuredHeight);
            }
            MeasureContext(true);
            Assert(ElementBox(backend.FindViews("context-first").Single()).Height == Px(32),
                "A finite unbounded-context allocation does not grow flex.");
            MeasureContext(false);
            Assert(ElementBox(backend.FindViews("context-first").Single()).Height == Px(48),
                "Changing only semantic context invalidates Android's same-spec measure cache.");
            MeasureContext(true);
            Assert(ElementBox(backend.FindViews("context-first").Single()).Height == Px(32),
                "Returning to unbounded context invalidates the cache again.");
        }
        GridFontChecks(dispatcher);
        Assert(surface.ChildCount == 0, "Axis and Grid fixtures release their native surfaces.");
        Log.Info("Xui.Android.Orders", $"Axis/Grid native geometry: {assertions - before} Android assertions.");
    }

    private void GridFontChecks(AndroidDispatcher dispatcher)
    {
        foreach (float scale in new[] { 1f, 2f })
        {
            using var configuration = new global::Android.Content.Res.Configuration(Resources!.Configuration!) { FontScale = scale };
            using var configured = CreateConfigurationContext(configuration)
                ?? throw new InvalidOperationException("Cannot create the Grid font-scale context.");
            using var context = new ContextThemeWrapper(configured, global::Android.Resource.Style.ThemeMaterialLightNoActionBar);
            using var surface = new OrderSurface(context);
            using var host = new Host(dispatcher);
            var demo = new GridSizingShowcase(host);
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            foreach (int width in new[] { 148, 180, 240, 360 })
            {
                MeasureNative(surface, width, 480);
                var input = (EditText)backend.FindViews("grid-input").Single();
                AssertCompleteEditor(input, $"Grid fontScale={scale}, width={width}");
                var frame = ElementBox(input);
                Assert(frame.Left >= 0 && frame.Right <= ((View)frame.Parent!).Width,
                    "Grid spans remain inside the actual parent width under fixed-track pressure.");
            }
        }
    }

    private void AssertCompleteEditor(EditText input, string description)
    {
        var layout = input.Layout ?? throw new InvalidOperationException($"{description} has no native text layout.");
        int required = layout.Height + input.CompoundPaddingTop + input.CompoundPaddingBottom;
        Assert(input.Height >= required && input.Bottom <= ((View)input.Parent!).Height,
            $"{description} does not clip native editor text or padding ({input.Height}px >= {required}px).");
    }

    private static View ElementBox(View control) => control is EditText
        ? (View)((View)control.Parent!).Parent! : (View)control.Parent!;

    private static void MeasureNative(View view, int width, int height)
    {
        float density = view.Resources!.DisplayMetrics!.Density;
        int pixelsWide = NativeMath.Pixels(width, density);
        int pixelsHigh = NativeMath.Pixels(height, density);
        view.ForceLayout();
        view.Measure(View.MeasureSpec.MakeMeasureSpec(pixelsWide, MeasureSpecMode.Exactly),
            View.MeasureSpec.MakeMeasureSpec(pixelsHigh, MeasureSpecMode.Exactly));
        view.Layout(0, 0, pixelsWide, pixelsHigh);
    }
}

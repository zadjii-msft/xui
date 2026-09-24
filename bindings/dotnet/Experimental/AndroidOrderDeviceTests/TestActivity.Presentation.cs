using Android.Graphics.Drawables;
using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;
using Axis = Xui.Experimental.Portable.Axis;
using PortableButton = Xui.Experimental.Portable.Button;
using PortableCheckBox = Xui.Experimental.Portable.CheckBox;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task PresentationChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var workbench = PresentationWorkbench.Create(host);
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        for (int i = 0; !surface.IsAttachedToWindow && i < 100; i++) await Task.Delay(10);
        var driver = new NativeDriver(backend);
        var input = (EditText)backend.FindViews("presentation-draft").Single();
        var choice = (CompoundButton)backend.FindViews("presentation-check").Single();
        var toggle = (CompoundButton)backend.FindViews("presentation-toggle").Single();
        var title = (TextView)backend.FindViews("presentation-title").Single();
        var caption = (TextView)backend.FindViews("presentation-caption").Single();
        float Sp(float value) => TypedValue.ApplyDimension(ComplexUnitType.Sp, value, surface.Resources!.DisplayMetrics);
        Assert(Math.Abs(title.TextSize - Sp(24)) < 0.5 && title.Typeface!.IsBold &&
            Math.Abs(caption.TextSize - Sp(12)) < 0.5, "Native semantic roles use their actual scaled sizes and weights.");
        var originalColors = input.TextColors;
        var originalHint = input.HintTextColors;
        var originalTint = input.BackgroundTintList;
        var originalChoice = choice.ButtonTintList;
        var originalBackground = surface.Background;
        input.Text = "A retained draft";
        host.TryFocus(workbench.DraftInput);
        host.SetSelection(workbench.DraftInput, new(2, 8));
        using var editable = input.EditableText!;
        BaseInputConnection.SetComposingSpans(editable);
        int start = BaseInputConnection.GetComposingSpanStart(editable);
        int end = BaseInputConnection.GetComposingSpanEnd(editable);
        driver.Click("presentation-larger");
        driver.Click("presentation-bold");
        MeasureNative(surface, 360, 640);
        Assert(Math.Abs(input.TextSize - Sp(28)) < 0.5 && input.Typeface!.IsBold &&
            Math.Abs(toggle.TextSize - Sp(28)) < 0.5 && toggle.Typeface!.IsBold &&
            Math.Abs(choice.TextSize - Sp(28)) < 0.5 && choice.Typeface!.IsBold,
            "Explicit sizes and weights update the native editor and both choice controls.");
        AssertCompleteEditor(input, "Styled 28sp captioned editor");
        driver.Click("presentation-dark");
        int[][] states =
        [
            [global::Android.Resource.Attribute.StateEnabled],
            [global::Android.Resource.Attribute.StateEnabled, global::Android.Resource.Attribute.StateHovered],
            [global::Android.Resource.Attribute.StateEnabled, global::Android.Resource.Attribute.StatePressed],
            [global::Android.Resource.Attribute.StateEnabled, global::Android.Resource.Attribute.StateFocused],
            [global::Android.Resource.Attribute.StateEnabled, global::Android.Resource.Attribute.StateChecked],
            [global::Android.Resource.Attribute.StateEnabled, global::Android.Resource.Attribute.StateChecked, global::Android.Resource.Attribute.StatePressed]
        ];
        var darkChoice = choice.ButtonTintList ?? throw new InvalidOperationException("Missing native checkbox tint.");
        var darkUnderline = input.BackgroundTintList ?? throw new InvalidOperationException("Missing native input tint.");
        var choiceBefore = states.Select(state => darkChoice.GetColorForState(state, new global::Android.Graphics.Color(darkChoice.DefaultColor))).ToArray();
        var inputBefore = states.Select(state => darkUnderline.GetColorForState(state, new global::Android.Graphics.Color(darkUnderline.DefaultColor))).ToArray();
        driver.Click("presentation-colors");
        Assert(input.CurrentTextColor == unchecked((int)0xffffffff) &&
            surface.Background is ColorDrawable { Color: var background } && background.ToArgb() == unchecked((int)0xff202020),
            "Dark custom resources change actual native foreground and owned background.");
        Assert(choice.ButtonTintList?.GetColorForState(states[4], global::Android.Graphics.Color.Transparent) == unchecked((int)0xff60cdff) &&
            toggle.ButtonTintList?.GetColorForState(states[4], global::Android.Graphics.Color.Transparent) == unchecked((int)0xff60cdff),
            "The authored accent reaches actual native checked choices.");
        for (int i = 0; i < states.Length; i++)
        {
            int Expected(int before, int accent) => (before & 0xffffff) == (accent & 0xffffff)
                ? (before & unchecked((int)0xff000000)) | 0x60cdff : before;
            Assert(choice.ButtonTintList!.GetColorForState(states[i], global::Android.Graphics.Color.Transparent) == Expected(choiceBefore[i], choiceBefore[4]),
                "Custom accent preserves native checkbox non-accent states and alpha.");
            Assert(input.BackgroundTintList!.GetColorForState(states[i], global::Android.Graphics.Color.Transparent) == Expected(inputBefore[i], inputBefore[3]),
                "Custom accent preserves native input non-accent states and alpha.");
        }
        int disabledBefore = originalChoice?.GetColorForState([-global::Android.Resource.Attribute.StateEnabled],
            new global::Android.Graphics.Color(originalChoice.DefaultColor)) ?? 0;
        choice.Enabled = false;
        Assert(choice.ButtonTintList!.GetColorForState([-global::Android.Resource.Attribute.StateEnabled],
            new global::Android.Graphics.Color(choice.ButtonTintList.DefaultColor)) != unchecked((int)0xff60cdff),
            "A custom accent retains a distinct native disabled color.");
        choice.Enabled = true;
        Assert(ReferenceEquals(input, backend.FindViews("presentation-draft").Single()) &&
            input.Text == "A retained draft" && input.HasFocus && input.SelectionStart == 2 && input.SelectionEnd == 8 &&
            BaseInputConnection.GetComposingSpanStart(editable) == start &&
            BaseInputConnection.GetComposingSpanEnd(editable) == end,
            "Native theme and typography retain editor text, identity, selection and composition.");
        driver.Click("presentation-inherit");
        Assert(input.TextColors == originalColors && input.HintTextColors == originalHint &&
            input.BackgroundTintList == originalTint && choice.ButtonTintList == originalChoice &&
            surface.Background == originalBackground,
            "Null theme restores the captured native color-state lists and original surface background.");
        Assert((choice.ButtonTintList?.GetColorForState([-global::Android.Resource.Attribute.StateEnabled],
            new global::Android.Graphics.Color(choice.ButtonTintList.DefaultColor)) ?? 0) == disabledBefore,
            "Inherited disabled-state colors are restored exactly.");
        driver.Click("presentation-light");
        driver.Click("presentation-colors");
        Assert(input.CurrentTextColor == unchecked((int)0xff1a1a1a) &&
            surface.Background is ColorDrawable { Color: var lightBackground } && lightBackground.ToArgb() == unchecked((int)0xffffffff),
            "Light custom resources select the authored native color variant.");
        driver.Click("presentation-system");
        Assert(host.Theme is { Mode: ThemeMode.System }, "Explicit System remains an opted-in native theme.");
        driver.Click("presentation-inherit");
        Assert(Math.Abs(input.TextSize - Sp(28)) < 0.5, "Resetting the theme does not silently reset independent authored typography.");
        surface.SetBackgroundColor(new global::Android.Graphics.Color(unchecked((int)0xffececec)));
        var callerBackground = surface.Background;
        driver.Click("presentation-dark");
        driver.Click("presentation-inherit");
        Assert(surface.Background == callerBackground, "Theme reacquisition captures the caller's new inherited surface style.");
        using var otherSurface = new OrderSurface(this);
        using var otherHost = new Host(dispatcher);
        _ = new Greeting(otherHost);
        var otherBackend = new AndroidBackend(otherSurface, dispatcher);
        otherHost.Attach(otherBackend);
        var otherInput = (EditText)otherBackend.FindViews("name").Single();
        var otherColors = otherInput.TextColors;
        driver.Click("presentation-dark");
        Assert(otherInput.TextColors == otherColors && otherSurface.Background is null,
            "A host theme does not recolor another host or its surface.");
        BaseInputConnection.RemoveComposingSpans(editable);
        host.Dispose();
        Assert(surface.Background == callerBackground && surface.ChildCount == 0,
            "Theme retirement restores the caller-owned surface and releases native controls.");
        otherSurface.SetBackgroundColor(new global::Android.Graphics.Color(unchecked((int)0xffdddddd)));
        var otherBackground = otherSurface.Background;
        otherHost.Dispose();
        Assert(otherSurface.Background == otherBackground, "A backend that never acquired a theme leaves caller surface changes untouched on disposal.");
        NativeTypographyResetChecks(dispatcher);
        Log.Info("Xui.Android.Orders", $"Presentation: {assertions - before} native typography, geometry and theme assertions.");
    }

    private void NativeTypographyResetChecks(AndroidDispatcher dispatcher)
    {
        foreach (float fontScale in new[] { 1f, 2f })
        {
            using var configuration = new global::Android.Content.Res.Configuration(Resources!.Configuration!) { FontScale = fontScale };
            using var configured = CreateConfigurationContext(configuration)
                ?? throw new InvalidOperationException("Cannot create the font-scale test context.");
            using var context = new ContextThemeWrapper(configured, global::Android.Resource.Style.ThemeMaterialLightNoActionBar);
            using var surface = new OrderSurface(context);
            using var host = new Host(dispatcher);
            var controls = new List<Control>();
            using (var build = host.BeginBuild())
            {
                var root = host.Stack(Axis.Vertical);
                var content = host.Stack(Axis.Vertical);
                controls.Add(host.Label("Native text"));
                controls.Add(host.Button("Native button"));
                controls.Add(host.TextInput("Native caption"));
                controls.Add(host.Toggle("Native toggle"));
                controls.Add(host.CheckBox("Native choice"));
                for (int i = 0; i < controls.Count; i++)
                {
                    controls[i].AutomationId = $"native-font-{i}";
                    content.Add(controls[i]);
                }
                root.Add(host.ScrollView(content, "Native font targets"), 1);
                host.SetContent(root);
                build.Complete();
            }
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var views = controls.Select(control => (TextView)backend.FindViews(control.AutomationId).Single()).ToArray();
            var baseline = views.Select(view => (Size: view.TextSize, Face: view.Typeface)).ToArray();
            foreach (var control in controls) control.Typography = new(fontSize: 28, fontWeight: 700);
            foreach (int width in new[] { 240, 360 })
            {
                MeasureNative(surface, width, 640);
                float size = TypedValue.ApplyDimension(ComplexUnitType.Sp, 28, surface.Resources!.DisplayMetrics);
                Assert(views.All(view => Math.Abs(view.TextSize - size) < 0.5 && view.Typeface!.IsBold),
                    $"All five native font targets honor 28sp/bold at fontScale={fontScale}, width={width}.");
                AssertCompleteEditor((EditText)views[2], $"Native caption/editor fontScale={fontScale}, width={width}");
                var inputParent = (ViewGroup)views[2].Parent!;
                Assert(((TextView)inputParent.GetChildAt(0)!).TextSize == views[2].TextSize &&
                    inputParent.Height >= inputParent.GetChildAt(0)!.Height + views[2].Height,
                    "Styled caption and editor have complete native geometry inside their shared wrapper.");
            }
            foreach (var control in controls) control.Typography = null;
            MeasureNative(surface, 360, 640);
            for (int i = 0; i < controls.Count; i++)
                Assert(views[i].TextSize == baseline[i].Size && views[i].Typeface == baseline[i].Face,
                    "Null typography restores the exact captured native font family, size and weight.");
            host.Dispose();
            Assert(surface.ChildCount == 0, "Scaled native typography fixtures release their attachment.");
        }
    }
}

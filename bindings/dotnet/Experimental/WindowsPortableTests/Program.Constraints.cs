using System.Runtime.InteropServices;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void ConstraintValueContracts()
    {
        Check(AxisConstraints.Auto == default && AxisConstraints.Auto.Length is null &&
            AxisConstraints.Auto.Minimum == 0 && AxisConstraints.Auto.Maximum is null,
            "An explicit automatic axis must retain unbounded natural measurement.");
        AxisConstraints? inherited = null;
        AxisConstraints? automatic = AxisConstraints.Auto;
        Check(inherited != automatic, "Explicit automatic sizing was conflated with legacy inheritance.");
        var fixedWidth = new AxisConstraints(280, 20, 400);
        Check(fixedWidth.Length == 280 && fixedWidth.Minimum == 20 && fixedWidth.Maximum == 400,
            "Axis constraints lost their validated values.");
        Check(new AxisConstraints(0, 0, 0).Length == 0, "Zero-length constraints must be supported.");
        Check(new AxisConstraints(null, 100, 100).Length is null, "Equal min/max must not rewrite the authored sizing mode.");
        foreach (float invalid in new[] { -1f, float.NaN, float.PositiveInfinity, float.NegativeInfinity })
        {
            Throws<ArgumentOutOfRangeException>(() => _ = new AxisConstraints(invalid));
            Throws<ArgumentOutOfRangeException>(() => _ = new AxisConstraints(minimum: invalid));
            Throws<ArgumentOutOfRangeException>(() => _ = new AxisConstraints(maximum: invalid));
        }
        Throws<ArgumentOutOfRangeException>(() => _ = new AxisConstraints(minimum: 30, maximum: 20));
        Throws<ArgumentOutOfRangeException>(() => _ = new AxisConstraints(10, minimum: 20));
        Throws<ArgumentOutOfRangeException>(() => _ = new AxisConstraints(30, maximum: 20));
        Check(new AxisConstraints(float.MaxValue, maximum: float.MaxValue).Length == float.MaxValue,
            "A finite maximum length was rejected.");
    }

    private static void NativeAxisConstraintScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Windows independent axis constraints", 620, 960,
            visualStyle: VisualStyle.WinUI);
        var natural = window.TextInput("Natural caption").SetText("Retained axis draft");
        var fixedSize = window.TextInput("Legacy fixed").FixedSize(260, 96);
        var preferred = window.TextInput("Legacy preferred").PreferredSize(300, 104);
        var constrained = window.TextInput("Constrained caption");
        var automatic = window.TextInput("Automatic caption");
        automatic.SetAxisConstraints(null, AxisConstraints.Auto);
        var root = window.Stack().Spacing(4).Add(natural).Add(fixedSize).Add(preferred).Add(constrained).Add(automatic);
        window.SetContent(root);

        var explicitWidth = new AxisConstraints(280, 20, 400);
        Check(ReferenceEquals(constrained.SetAxisConstraints(explicitWidth, null), constrained),
            "Setting axis constraints replaced the managed element.");
        Check(constrained.GetAxisConstraints() == (explicitWidth, null), "Native axis values did not round-trip.");
        var invalid = new AxisNativePair
        {
            Size = 40, Version = 0x10000,
            Width = new() { Flags = 3, Length = 333 },
            Height = new() { Flags = 5, Minimum = 20, Maximum = 10 }
        };
        Check(Marshal.SizeOf<AxisNativePair>() == 40 && SetAxisNative(constrained.Id, in invalid) == 1 &&
            constrained.GetAxisConstraints() == (explicitWidth, null),
            "An invalid second axis partially applied its valid first axis.");
        var familyElements = new Xui.Element[]
        {
            window.Stack(), window.CreateContentHost(), window.Label("Axis label"), window.Button("Axis button"),
            window.TextInput("Axis input"), window.ScrollView(window.Stack(), "Axis scroll"), window.Toggle("Axis toggle"),
            window.CheckBox("Axis check"), window.Progress("Axis progress")
        };
        foreach (var element in familyElements)
        {
            element.SetAxisConstraints(new(150, 20, 180), new(null, 10, 120));
            Check(element.GetAxisConstraints() == (new AxisConstraints(150, 20, 180), new AxisConstraints(null, 10, 120)),
                $"Axis constraints did not round-trip on {element.GetType().Name}.");
            element.SetAxisConstraints(null, null);
            Check(element.GetAxisConstraints() == (null, null), "Inherited axis reset did not round-trip.");
        }
        if (ScrollView.SupportsFillViewport)
        {
            var scroll = (ScrollView)familyElements[5];
            Check(scroll.FillViewport, "The existing native Windows viewport-filling default changed.");
            Check(ReferenceEquals(scroll.SetFillViewport(false), scroll) && !scroll.FillViewport,
                "The native unbounded-scroll opt-out did not round-trip.");
            scroll.FillViewport = true;
            Check(scroll.FillViewport, "Native viewport filling could not be restored.");
        }
        try
        {
            window.Image("Unsupported axes").SetAxisConstraints(new(150), null);
            throw new InvalidOperationException("An unsupported native family accepted axis constraints.");
        }
        catch (XuiException error) { Check(error.Status == 3, "Unsupported axes did not report the explicit native kind boundary."); }
        fixedSize.SetAxisConstraints(AxisConstraints.Auto, null);
        preferred.SetAxisConstraints(new(280), null);
        Check(fixedSize.GetAxisConstraints() == (AxisConstraints.Auto, null), "Native Auto was confused with inheritance.");
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                nint edit = 0;
                float naturalHeight = 0;
                float automaticHeight = 0;
                int changes = 0;
                ui.Ui(() =>
                {
                    natural.Focus();
                    edit = GetFocus();
                    natural.Selection = new(2, 5);
                    natural.Changed += _ => changes++;
                    naturalHeight = natural.GetBounds().Height;
                    automaticHeight = automatic.GetBounds().Height;
                    Check(naturalHeight > 40 && Math.Abs(constrained.GetBounds().Height - naturalHeight) < 0.1f &&
                        Math.Abs(constrained.GetBounds().Width - 280) < 0.1f,
                        "A fresh fixed width changed the native captioned editor's natural height.");
                    Check(Math.Abs(fixedSize.GetBounds().Height - 96) < 0.1f &&
                        Math.Abs(fixedSize.GetBounds().Width - natural.GetBounds().Width) < 0.1f,
                        "Width Auto did not override legacy fixed width independently of its height.");
                    Check(Math.Abs(preferred.GetBounds().Height - 104) < 0.1f &&
                        Math.Abs(preferred.GetBounds().Width - 280) < 0.1f,
                        "A width override changed the legacy preferred height.");
                    fixedSize.FixedSize(210, 110);
                });
                ui.Wait(() => Math.Abs(fixedSize.GetBounds().Height - 110) < 0.1f);
                ui.Ui(() =>
                {
                    Check(Math.Abs(fixedSize.GetBounds().Width - natural.GetBounds().Width) < 0.1f,
                        "A legacy setter overwrote an explicit axis override.");
                    fixedSize.SetAxisConstraints(null, null);
                });
                ui.Wait(() => Math.Abs(fixedSize.GetBounds().Width - 210) < 0.1f);
                ui.Ui(() =>
                {
                    Check(Math.Abs(fixedSize.GetBounds().Height - 110) < 0.1f &&
                        fixedSize.GetAxisConstraints() == (null, null),
                        "Null reset did not restore the latest legacy fixed dimensions.");
                    fixedSize.SetAxisConstraints(null, AxisConstraints.Auto);
                    preferred.SetAxisConstraints(AxisConstraints.Auto, AxisConstraints.Auto);
                });
                ui.Wait(() => Math.Abs(fixedSize.GetBounds().Height - automaticHeight) < 0.1f &&
                    Math.Abs(preferred.GetBounds().Height - automaticHeight) < 0.1f,
                    () => $"Axis Auto heights did not settle: natural Auto={automaticHeight}, fixed={fixedSize.GetBounds()}, preferred={preferred.GetBounds()}, fixed axes={fixedSize.GetAxisConstraints()}, preferred axes={preferred.GetAxisConstraints()}.");
                ui.Ui(() =>
                {
                    Check(Math.Abs(fixedSize.GetBounds().Width - 210) < 0.1f,
                        "Height Auto lost the other axis's inherited fixed width.");
                    preferred.PreferredSize(350, 122);
                    preferred.SetAxisConstraints(null, null);
                });
                ui.Wait(() => Math.Abs(preferred.GetBounds().Height - 122) < 0.1f);
                ui.Ui(() =>
                {
                    Check(GetFocus() == edit && natural.Selection == new TextSelection(2, 5) && changes == 0,
                        "Axis transitions disturbed native focus, selection, or programmatic silence.");
                    constrained.SetAxisConstraints(new(275), AxisConstraints.Auto);
                    constrained.MinimumSize(0, 200).MaximumSize(500, 220);
                });
                ui.Wait(() => Math.Abs(constrained.GetBounds().Width - 275) < 0.1f);
                ui.Ui(() =>
                {
                    Check(Math.Abs(constrained.GetBounds().Height - automaticHeight) < 0.1f,
                        "Explicit Auto inherited legacy minimum/maximum bounds on its axis.");
                    constrained.SetAxisConstraints(null, null);
                });
                ui.Wait(() => Math.Abs(constrained.GetBounds().Height - 200) < 0.1f);
                ui.Ui(() =>
                {
                    Check(Math.Abs(constrained.GetBounds().Width - 500) < 0.1f,
                        "Resetting overrides did not restore the current legacy maximum width.");
                    constrained.MinimumSize(0, 0).MaximumSize(float.MaxValue, float.MaxValue);
                    natural.SetAxisConstraints(new(280), AxisConstraints.Auto);
                    constrained.SetAxisConstraints(new(280), AxisConstraints.Auto);
                    window.SetPresentation("Segoe UI", 28);
                });
                ui.Wait(() => natural.GetBounds().Height > automaticHeight);
                ui.Ui(() =>
                {
                    Check(Math.Abs(natural.GetBounds().Height - constrained.GetBounds().Height) < 0.1f &&
                        Math.Abs(natural.GetBounds().Width - 280) < 0.1f,
                        "The constrained editor did not preserve font-aware natural caption height.");
                    Check(GetWindowRect(edit, out var rectangle) && rectangle.Bottom - rectangle.Top >= 28,
                        "Scaled-font native input was clipped below a usable editor height.");
                    constrained.SetAxisConstraints(new(null, 400, 450), null);
                    Check(SetWindowPos(GetAncestor(edit, 2), 0, 0, 0, 260, 700, 0x0016),
                        "The narrow parent allocation could not be applied.");
                });
                ui.Wait(() => root.GetBounds().Width < 260);
                ui.Ui(() =>
                {
                    Check(constrained.GetBounds().Width <= root.GetBounds().Width + 0.1f &&
                        constrained.GetBounds().Width < 400,
                        "A minimum constraint overflowed a smaller parent allocation.");
                    Check(GetFocus() == edit && natural.Selection == new TextSelection(2, 5) && changes == 0,
                        "Native editor identity changed during axis layout.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }

    private static void PortableAxisConstraintScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable axis constraints", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        P.TextInput input;
        P.TextInput natural;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(P.Axis.Vertical);
            input = host.TextInput("Portable constrained caption");
            input.AutomationId = "constrained";
            input.Text = "Retained portable axis draft";
            P.ElementExtensions.FixedSize(input, 260, 96);
            input.SetWidth(P.AxisConstraints.Auto);
            natural = host.TextInput("Portable natural caption");
            natural.AutomationId = "natural";
            root.Add(input).Add(natural);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new WindowsBackend(surface, dispatcher);
        Check(backend.SupportsConstraints == SupportsPortableConstraints,
            "The Windows peer capability does not match the loaded native axis API.");
        if (!backend.SupportsConstraints)
        {
            Throws<NotSupportedException>(() => host.Attach(backend));
            Check(!host.IsAttached && HandleCount(window) == baseline,
                "Unsupported constrained attachment leaked a partial native arena.");
            input.SetConstraints(null, null);
            backend = new WindowsBackend(surface, dispatcher);
            host.Attach(backend);
            uint attached = HandleCount(window);
            Throws<NotSupportedException>(() => input.SetWidth(new(280)));
            Check(host.IsAttached && input.WidthConstraints is null && HandleCount(window) == attached,
                "A rejected live constraint changed the model or detached the existing native tree.");
            host.Detach();
            Check(HandleCount(window) == baseline, "Fixed-only fallback left native resources alive.");
            return;
        }
        natural.SetHeight(P.AxisConstraints.Auto);
        host.Attach(backend);
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                nint edit = 0;
                var nativeInput = ui.Ui(() => (TextInput)backend.FindControls("constrained").Single());
                var nativeNatural = ui.Ui(() => (TextInput)backend.FindControls("natural").Single());
                int changes = 0;
                ui.Ui(() =>
                {
                    input.Changed += _ => changes++;
                    nativeInput.Focus();
                    edit = GetFocus();
                    nativeInput.Selection = new(3, 7);
                    Check(Math.Abs(nativeInput.GetBounds().Height - 96) < 0.1f &&
                        nativeInput.GetBounds().Width > 260, "Portable width Auto did not preserve the inherited fixed height.");
                    input.SetConstraints(new(280), P.AxisConstraints.Auto);
                });
                ui.Wait(() => Math.Abs(nativeInput.GetBounds().Width - 280) < 0.1f &&
                    Math.Abs(nativeInput.GetBounds().Height - nativeNatural.GetBounds().Height) < 0.1f);
                ui.Ui(() =>
                {
                    Check(nativeInput.GetAxisConstraints() == (new AxisConstraints(280), AxisConstraints.Auto),
                        "The portable pair was not applied atomically to both native axes.");
                    var before = nativeInput.GetAxisConstraints();
                    Throws<ArgumentException>(() => input.SetConstraints(new(300), new(10, 20)));
                    Check(nativeInput.GetAxisConstraints() == before && host.IsAttached,
                        "An invalid portable second axis changed the native first axis.");
                    input.SetHeight(null);
                });
                ui.Wait(() => Math.Abs(nativeInput.GetBounds().Height - 96) < 0.1f);
                ui.Ui(() =>
                {
                    Check(input.WidthConstraints == new P.AxisConstraints(280) &&
                        Math.Abs(nativeInput.GetBounds().Width - 280) < 0.1f,
                        "The one-axis setter did not preserve the opposite override.");
                    P.ElementExtensions.FixedSize(input, 220, 102);
                    input.SetConstraints(null, null);
                });
                ui.Wait(() => Math.Abs(nativeInput.GetBounds().Width - 220) < 0.1f &&
                    Math.Abs(nativeInput.GetBounds().Height - 102) < 0.1f);
                ui.Ui(() =>
                {
                    Check(GetFocus() == edit && nativeInput.Selection == new TextSelection(3, 7) && changes == 0,
                        "Portable axis reset replaced or modified the native editing session.");
                    input.SetWidth(new(290));
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(edit),
                        "Constrained detach leaked native handles or an editor.");
                    backend = new WindowsBackend(surface, dispatcher);
                    host.Attach(backend);
                    Check(((TextInput)backend.FindControls("constrained").Single()).GetAxisConstraints() ==
                        (new AxisConstraints(290), null), "Reattachment lost current portable axis constraints.");
                    host.Detach();
                    Check(HandleCount(window) == baseline, "Repeated constrained attachment leaked its arena.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }

    private static void SharedAxisSizingScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = new PortableLayout.AxisSizingShowcase(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var driver = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                driver.Ui(() =>
                {
                    Check(!((ScrollView)driver.Native("axes-content")).FillViewport,
                        "The portable scroll peer did not opt out of native viewport filling.");
                    var input = driver.Input("axes-input");
                    var natural = driver.Input("height-only-input");
                    Check(Math.Abs(input.GetBounds().Width - 280) < 0.1f &&
                        Math.Abs(input.GetBounds().Height - natural.GetBounds().Height) < 0.1f,
                        "The authored fixed-width/natural-height fixture did not use native intrinsic height.");
                    var first = driver.Native("first-minimum").GetBounds();
                    var second = driver.Native("second-minimum").GetBounds();
                    Check(Math.Abs(first.Width - 120) < 0.1f && Math.Abs(second.Width - 60) < 0.1f &&
                        Math.Abs(second.X - first.X - 120) < 0.1f,
                        "Minimum pressure did not preserve sequential parent allocation.");
                    var fixedChild = driver.Native("fixed-child").GetBounds();
                    var capped = driver.Native("capped-child").GetBounds();
                    var flex = driver.Native("flex-child").GetBounds();
                    Check(Math.Abs(fixedChild.Width - 60) < 0.1f && Math.Abs(capped.Width - 40) < 0.1f &&
                        Math.Abs(flex.Width - 60) < 0.1f && Math.Abs(flex.X - fixedChild.X - 120) < 0.1f,
                        "A constrained flex child changed the authored flex allocation slots.");
                    var unboundedFirst = driver.Native("unbounded-first").GetBounds();
                    var unboundedSecond = driver.Native("unbounded-second").GetBounds();
                    Check(Math.Abs(unboundedFirst.Height - 32) < 0.1f && Math.Abs(unboundedSecond.Height - 48) < 0.1f &&
                        Math.Abs(unboundedSecond.Y - unboundedFirst.Y - 40) < 0.1f,
                        $"Flex in an unbounded scroll axis did not use natural constrained child extents: first={unboundedFirst}, second={unboundedSecond}.");
                });
                driver.Change("axes-input", "Shared axis draft");
                driver.Click("axes-narrow");
                driver.Wait(() => Math.Abs(driver.Input("axes-input").GetBounds().Width - 220) < 0.1f);
                driver.Click("axes-inherit");
                driver.Wait(() => Math.Abs(driver.Input("axes-input").GetBounds().Width - 180) < 0.1f &&
                    Math.Abs(driver.Input("axes-input").GetBounds().Height - 90) < 0.1f);
                driver.Click("axes-preserve-height");
                driver.Wait(() => Math.Abs(driver.Input("width-only-input").GetBounds().Width - 240) < 0.1f &&
                    Math.Abs(driver.Input("width-only-input").GetBounds().Height - 96) < 0.1f);
                nint edit = 0;
                driver.Ui(() =>
                {
                    Check(sample.WidthOnlyInput.HeightConstraints == new P.AxisConstraints(96),
                        "The generated one-axis binding overwrote an independently changed height.");
                    var input = driver.Input("axes-input");
                    input.Focus();
                    input.Selection = new(1, 6);
                    edit = GetFocus();
                    sample.FieldWidth = 250;
                    sample.FieldHeight = P.AxisConstraints.Auto;
                });
                driver.Wait(() => Math.Abs(driver.Input("axes-input").GetBounds().Width - 250) < 0.1f);
                driver.Ui(() =>
                {
                    Check(GetFocus() == edit && driver.Input("axes-input").Selection == new TextSelection(1, 6) &&
                        sample.Draft == "Shared axis draft", "Generated axis refresh changed the native editing session.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(edit),
                        "Shared constrained content leaked its native arena.");
                });
            }
            finally { driver.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct AxisNativeValue
    {
        internal uint Flags;
        internal float Length, Minimum, Maximum;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct AxisNativePair
    {
        internal uint Size, Version;
        internal AxisNativeValue Width, Height;
    }
    [DllImport("xui", EntryPoint = "xui_element_set_axis_constraints")]
    private static extern int SetAxisNative(ulong element, in AxisNativePair value);
}

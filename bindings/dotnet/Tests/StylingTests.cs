using Xui;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

internal static class StylingTests
{
    private static int assertions;
    private static void Assert(bool value, [CallerArgumentExpression(nameof(value))] string? expression = null)
    {
        if (!value) throw new Exception($"Styling assertion failed: {expression}");
        ++assertions;
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { ++assertions; return; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }
    private static void Fails(int status, Action action)
    {
        try { action(); }
        catch (XuiException error) { Assert(error.Status == status && !string.IsNullOrEmpty(error.Message)); return; }
        throw new Exception($"Expected XUI status {status}.");
    }
    internal static void Definitions()
    {
        var assembly = typeof(Element).Assembly;
        foreach (var name in new[] { "ButtonStyle", "ButtonStyleValues", "ButtonStyleState", "ButtonStyleRule" })
            Assert(assembly.GetType($"Xui.{name}") is null);
        foreach (var type in new[] { typeof(Button), typeof(Toggle), typeof(ToggleButton), typeof(ToggleSwitch), typeof(HyperlinkButton) })
        {
            Assert(type.GetProperty("Style") is null && type.GetProperty("StyleValues") is null && type.GetProperty("EffectiveStyleValues") is null);
            Assert(!type.GetMethods().Any(method => method.Name is "SetStyle" or "SetStyleValues" or "GetStyleValues"));
            Assert(type.GetMethod("SetControlStyle")!.DeclaringType == typeof(Element));
        }
        var entries = new Dictionary<string, object>
        {
            ["accent"] = new ThemeColor(0x123456, 0x654321), ["alias"] = "accent"
        };
        var parent = new ResourceScope(entries);
        entries["accent"] = new ThemeColor(0);
        Assert(parent.Resolve("alias") == new ThemeColor(0x123456, 0x654321));
        var child = new ResourceScope(new Dictionary<string, object> { ["local"] = "accent" }, parent);
        Assert(child.Resolve("local") == parent.Resolve("accent"));
        Throws<KeyNotFoundException>(() => child.Resolve("missing"));
        Throws<KeyNotFoundException>(() => new ResourceScope(new Dictionary<string, object> { ["alias"] = "missing" }));
        Throws<ArgumentException>(() => new ResourceScope(new Dictionary<string, object> { ["a"] = "b", ["b"] = "a" }));
        Throws<ArgumentException>(() => new ResourceScope([new("a", new ThemeColor(0)), new("a", new ThemeColor(1))]));
        Throws<ArgumentException>(() => new ResourceScope(new Dictionary<string, object> { ["a"] = 1 }));
        Throws<ArgumentOutOfRangeException>(() => new ResourceScope(new Dictionary<string, object> { ["a"] = new ThemeColor(0xff000000) }));
        Throws<ArgumentException>(() => new ResourceScope(Enumerable.Range(0, 257).Select(i => new KeyValuePair<string, object>($"c{i}", new ThemeColor(0)))));
        var deep = parent;
        for (int i = 1; i < 16; ++i) deep = new ResourceScope([], deep);
        Throws<ArgumentException>(() => new ResourceScope([], deep));

        var values = new PartStyleValues { Background = new ThemeColor(0), Padding = new Insets(4), CornerRadius = 0 };
        ControlStyleRule[] rules = [new(StylePart.Root, StyleState.Hovered, new() { Foreground = new ThemeColor(0xffffff) })];
        var style = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, values)], rules);
        rules[0] = new(StylePart.Root, StyleState.Pressed, new());
        Assert(style.Rules[0].State == StyleState.Hovered);
        Throws<ArgumentOutOfRangeException>(() => new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { CornerRadius = float.NaN })]));
        Throws<ArgumentOutOfRangeException>(() => new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { Padding = new Insets(-1) })]));
        Throws<ArgumentOutOfRangeException>(() => new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { Background = new ThemeColor(0xff000000) })]));
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Button, [], [new(StylePart.Root, (StyleState)3, new())]));
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Button, [], [new(StylePart.Root, StyleState.Hovered, new()),
            new(StylePart.Root, StyleState.Hovered, new())]));
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new()), new(StylePart.Root, new())]));
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new())], Enumerable.Repeat(rules[0], 257).ToArray()));
        for (int i = 1; i < 16; ++i) style = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new())], basedOn: style);
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new())], basedOn: style));
        Console.WriteLine("C# immutable style and resource tests passed.");
        var toggleParts = new[] { new PartStyle(StylePart.Indicator, new() { Size = 18, Background = new(0) }) };
        var toggleStyle = new ControlStyle(StyleTarget.Toggle, toggleParts,
            [new(StylePart.Indicator, StyleState.Checked, new() { Background = new(0x123456, 0x654321) })]);
        toggleParts[0] = new(StylePart.Mark, new());
        Assert(toggleStyle.Parts[0].Part == StylePart.Indicator && toggleStyle.Parts[0].Values.Size == 18);
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Toggle, [new(StylePart.Label, new() { Size = 1 })]));
        Throws<ArgumentOutOfRangeException>(() => new ControlStyle(StyleTarget.Toggle, [new(StylePart.Indicator, new() { Size = float.NaN })]));
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Toggle, [], [new(StylePart.Root, (StyleState)3, new())]));
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Toggle, [], [new(StylePart.Indicator, StyleState.Checked, new()),
            new(StylePart.Indicator, StyleState.Checked, new())]));
        Throws<ArgumentException>(() => new ControlStyle((StyleTarget)99, []));
        Throws<ArgumentException>(() => new ControlStyle(StyleTarget.Toggle, [new((StylePart)99, new())]));
        Console.WriteLine("C# generic control style definition tests passed.");
    }
    private static void TooltipNative(Window window)
    {
        var style = new ControlStyle(StyleTarget.Tooltip,
            [new(StylePart.Root, new() { Background = new(0x112233), Foreground = new(0x445566) }),
             new(StylePart.Text, new() { FontFamily = "Segoe UI", FontSize = 18 })],
            [new(StylePart.Root, StyleState.Open, new() { Foreground = new(0x778899) })]);
        Assert(ReferenceEquals(window.SetTooltipStyle(style), window));
        Assert(ReferenceEquals(window.TooltipStyle, style));
        Assert(window.GetTooltipStyleValues(StylePart.Text, true).FontFamily == "Segoe UI");
        Assert(window.GetTooltipStyleValues(StylePart.Root, true).Foreground == new ThemeColor(0x445566));
        window.SetTooltipStyleValues(StylePart.Root, new() { Foreground = new(0) });
        Throws<ArgumentException>(() => window.SetTooltipStyle(new ControlStyle(StyleTarget.Toggle, [])));
        Assert(ReferenceEquals(window.TooltipStyle, style));
        window.SetTooltipStyle(null);
        Assert(window.GetTooltipStyleValues(StylePart.Root, true).Foreground == new ThemeColor(0));
        window.SetTooltipStyle(style);
        window.SetTooltipStyleValues(StylePart.Root, new());
        Assert(window.GetTooltipStyleValues(StylePart.Root, true).Background == new ThemeColor(0x112233));
        Task.Run(() => Fails(4, () => window.SetTooltipStyle(null))).GetAwaiter().GetResult();
        window.SetTooltipStyle(null);
        Assert(window.TooltipStyle is null && window.GetTooltipStyleValues(StylePart.Root, true) == new PartStyleValues());
        using var other = new Window();
        other.SetTooltipStyle(style);
        Assert(other.GetTooltipStyleValues(StylePart.Text, true).FontSize == 18);
    }
    internal static void Native()
    {
        using var w = new Window();
        TooltipNative(w);
        var button = w.Button("Styled");
        var values = new PartStyleValues { Background = new ThemeColor(0x123456, 0x654321), Padding = new Insets(3) };
        var style = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, values)]);
        var derived = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { CornerRadius = 4 })], basedOn: style);
        var local = new PartStyleValues { CornerRadius = 7 };
        Assert(button.ControlStyle is null && button.GetControlStyleValues(StylePart.Root) == new PartStyleValues());
        Assert(ReferenceEquals(button.SetControlStyleValues(StylePart.Root, local), button));
        Assert(ReferenceEquals(button.SetControlStyle(derived), button));
        Assert(ReferenceEquals(button.ControlStyle, derived));
        Assert(button.GetControlStyleValues(StylePart.Root) == local);
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == values with { CornerRadius = 7 });
        Throws<ArgumentOutOfRangeException>(() => button.SetControlStyleValues(StylePart.Root, new() { BorderThickness = new Insets(float.PositiveInfinity) }));
        Assert(button.GetControlStyleValues(StylePart.Root) == local);
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == values with { CornerRadius = 7 });
        button.ControlStyle = null;
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == local);
        button.SetControlStyleValues(StylePart.Root, new());
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == new PartStyleValues());
        using var other = new Window();
        var shared = other.Button("Shared").SetControlStyle(style);
        Assert(shared.GetControlStyleValues(StylePart.Root, effective: true) == values);
        Task.Run(() =>
        {
            Fails(4, () => button.ControlStyle = style);
            Fails(4, () => button.ControlStyle = null);
            Fails(4, () => _ = button.GetControlStyleValues(StylePart.Root));
            Fails(4, () => _ = button.GetControlStyleValues(StylePart.Root, effective: true));
        }).GetAwaiter().GetResult();
        Assert(button.ControlStyle is null && button.GetControlStyleValues(StylePart.Root) == new PartStyleValues());
        button.ControlStyle = derived;
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == values with { CornerRadius = 4 });
        w.Dispose();
        Throws<ObjectDisposedException>(() => button.ControlStyle = style);
        Throws<ObjectDisposedException>(() => button.ControlStyle = null);
        Throws<ObjectDisposedException>(() => _ = button.ControlStyle);
        Throws<ObjectDisposedException>(() => button.SetControlStyleValues(StylePart.Root, new()));
        Throws<ObjectDisposedException>(() => _ = button.GetControlStyleValues(StylePart.Root));
        Throws<ObjectDisposedException>(() => _ = button.GetControlStyleValues(StylePart.Root, effective: true));
        Assert(shared.GetControlStyleValues(StylePart.Root, effective: true) == values);
        StateAndInheritance();
        FailedApplicationCleanup();
        BoundedLifetime();
        SharedDefinitions();
        ToggleStyles();
        GeneratedToggleStyles();
        GeneratedExpandedStyles();
        Console.WriteLine($"C# styling assertions: {assertions} passed; architecture: {RuntimeInformation.ProcessArchitecture}");
    }

    private static void ToggleStyles()
    {
        using var window = new Window();
        var style = new ControlStyle(StyleTarget.Toggle,
            [new(StylePart.Root, new() { Foreground = new(0x123456, 0x654321) }),
             new(StylePart.Indicator, new() { Size = 18, Background = new(0) })],
            [new(StylePart.Indicator, StyleState.Checked, new() { Background = new(0x445566) }),
             new(StylePart.Root, StyleState.Disabled, new() { Foreground = new(0x777777) })]);
        var toggle = window.Toggle("Generic");
        toggle.SetControlStyle(style);
        Assert(toggle.GetControlStyleValues(StylePart.Label, true).Foreground == new ThemeColor(0x123456, 0x654321));
        toggle.Checked = true;
        Assert(toggle.GetControlStyleValues(StylePart.Indicator, true).Background == new ThemeColor(0x445566));
        toggle.Enabled = false;
        Assert(toggle.GetControlStyleValues(StylePart.Label, true).Foreground == new ThemeColor(0x777777));
        toggle.SetControlStyleValues(StylePart.Root, new() { Foreground = new(0) });
        toggle.ControlStyle = null;
        Assert(toggle.GetControlStyleValues(StylePart.Label, true).Foreground == new ThemeColor(0));
        toggle.SetControlStyleValues(StylePart.Root, new());
        Assert(toggle.GetControlStyleValues(StylePart.Label, true) == new PartStyleValues());
        toggle.ControlStyle = style;
        Fails(1, () => toggle.SetControlStyleValues(StylePart.Indicator, new() { Foreground = new(1) }));
        Assert(toggle.GetControlStyleValues(StylePart.Indicator, true).Size == 18);
        Fails(1, () => window.Button("Wrong target").SetControlStyle(style));
        using var other = new Window();
        var shared = other.Toggle("Shared").SetControlStyle(style);
        Assert(shared.GetControlStyleValues(StylePart.Indicator, true).Background == new ThemeColor(0));
        var marker = Handle(window.Button("Before shared assignments"));
        for (int i = 0; i < 256; ++i) toggle.ControlStyle = style;
        Assert(Handle(window.Button("After shared assignments")) == marker + 1);
        Task.Run(() => Fails(4, () => toggle.ControlStyle = style)).GetAwaiter().GetResult();
        window.Dispose();
        Throws<ObjectDisposedException>(() => toggle.ControlStyle = null);
        Assert(shared.GetControlStyleValues(StylePart.Indicator, true).Size == 18);
    }

    private static void GeneratedToggleStyles()
    {
        using var window = new Window();
        var component = new ToggleStylingFixture(window);
        var style = component.First.ControlStyle;
        Assert(style is not null && ReferenceEquals(style, component.Second.ControlStyle));
        Assert(component.First.GetControlStyleValues(StylePart.Label, true).Foreground == new ThemeColor(0));
        Assert(component.Second.GetControlStyleValues(StylePart.Label, true).Foreground == new ThemeColor(0x123456, 0x654321));
        Assert(component.First.GetControlStyleValues(StylePart.Indicator, true).Size == 18);
        component.First.Checked = true;
        Assert(component.First.GetControlStyleValues(StylePart.Indicator, true).Background == new ThemeColor(0x123456, 0x654321));
        component.Input.Text = "Keep native input";
        var input = Handle(component.Input);
        var toggle = Handle(component.First);
        var refresh = typeof(ToggleStylingFixture).GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!;
        var marker = Handle(window.Button("Before unchanged refresh"));
        for (int i = 0; i < 32; ++i) refresh.Invoke(component, null);
        Assert(Handle(window.Button("After unchanged refresh")) == marker + 1);
        Assert(ReferenceEquals(style, component.First.ControlStyle));
        typeof(ToggleStylingFixture).GetField("__xuiStyleRevision", BindingFlags.Static | BindingFlags.NonPublic)!.SetValue(null, "stale");
        refresh.Invoke(component, null);
        Assert(!ReferenceEquals(style, component.First.ControlStyle) && ReferenceEquals(component.First.ControlStyle, component.Second.ControlStyle));
        Assert(Handle(component.First) == toggle && Handle(component.Input) == input);
        Assert(component.Input.Text == "Keep native input");
        Assert(component.First.GetControlStyleValues(StylePart.Indicator, true).Background == new ThemeColor(0x123456, 0x654321));
    }

    private static void GeneratedExpandedStyles()
    {
        using var window = new Window();
        var component = new ExpandedStylingFixture(window);
        var foundation = component.FoundationButton.ControlStyle;
        var generic = component.GenericButton.ControlStyle;
        Assert(foundation is not null && generic is not null);
        Assert(ReferenceEquals(foundation, generic!.BasedOn));
        Assert(ReferenceEquals(generic!.BasedOn, component.OtherButton.ControlStyle!.BasedOn));
        Assert(component.FoundationButton.GetControlStyleValues(StylePart.Root, effective: true).Background == new ThemeColor(0x123456));
        Assert(component.GenericButton.GetControlStyleValues(StylePart.Root, true).Background == new ThemeColor(0x123456));
        Assert(component.GenericButton.GetControlStyleValues(StylePart.Label, true).FontSize == 24);
        Assert(component.OtherButton.GetControlStyleValues(StylePart.Label, true).FontSize == 18);
        Assert(component.HeadingLabel.GetControlStyleValues(StylePart.Root, true).FontSize == 20);
        Assert(component.PanelRoot.GetControlStyleValues(StylePart.Root, true).Padding == new Insets(7));
        Assert(component.PanelRoot.GetControlStyleValues(StylePart.Root, true).Spacing == 9);
        component.GenericButton.Enabled = false;
        Assert(component.GenericButton.GetControlStyleValues(StylePart.Root, true).Foreground == new ThemeColor(0x112233));
        Assert(component.GenericButton.GetControlStyleValues(StylePart.Root, true).Background == new ThemeColor(0x445566));
        Assert(component.GenericButton.GetControlStyleValues(StylePart.Label, true).Foreground == new ThemeColor(0xABCDEF));
        component.Input.Text = "Keep expanded native input";
        var input = Handle(component.Input);
        var button = Handle(component.GenericButton);
        var panel = Handle(component.PanelRoot);
        var refresh = typeof(ExpandedStylingFixture).GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!;
        var marker = Handle(window.Button("Before unchanged expanded refresh"));
        for (int i = 0; i < 32; ++i) refresh.Invoke(component, null);
        Assert(Handle(window.Button("After unchanged expanded refresh")) == marker + 1);
        Assert(ReferenceEquals(foundation, component.FoundationButton.ControlStyle));
        Assert(ReferenceEquals(generic, component.GenericButton.ControlStyle));
        typeof(ExpandedStylingFixture).GetField("__xuiStyleRevision", BindingFlags.Static | BindingFlags.NonPublic)!.SetValue(null, "stale");
        refresh.Invoke(component, null);
        Assert(!ReferenceEquals(foundation, component.FoundationButton.ControlStyle));
        Assert(!ReferenceEquals(generic, component.GenericButton.ControlStyle));
        Assert(ReferenceEquals(component.GenericButton.ControlStyle!.BasedOn, component.OtherButton.ControlStyle!.BasedOn));
        Assert(Handle(component.Input) == input && Handle(component.GenericButton) == button && Handle(component.PanelRoot) == panel);
        Assert(component.Input.Text == "Keep expanded native input");
        Assert(component.GenericButton.GetControlStyleValues(StylePart.Root, true).Background == new ThemeColor(0x445566));
        Assert(component.GenericButton.GetControlStyleValues(StylePart.Label, true).FontSize == 24);
        Assert(component.HeadingLabel.GetControlStyleValues(StylePart.Root, true).Foreground == new ThemeColor(0xFEDCBA));
    }

    private static void StateAndInheritance()
    {
        using var w = new Window();
        var baseValues = new PartStyleValues
        {
            Background = new(0x123456, 0x654321), Foreground = new(0x101010),
            BorderBrush = new(0x202020), BorderThickness = new(1, 2, 3, 4),
            Padding = new(4, 3, 2, 1), CornerRadius = 2
        };
        var style = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, baseValues)],
        [
            new(StylePart.Root, StyleState.Checked, new() { Background = new(0x333333), Padding = new(6) }),
            new(StylePart.Root, StyleState.Disabled, new() { Background = new(0x444444) })
        ]);
        var derived = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { CornerRadius = 5 })],
        [
            new(StylePart.Root, StyleState.Checked, new() { Foreground = new(0x666666) }),
            new(StylePart.Root, StyleState.Disabled, new() { BorderBrush = new(0x777777) })
        ], style);
        var button = w.Button("State").Behavior(ButtonBehavior.Toggle);
        button.SetControlStyle(derived);
        var normal = baseValues with { CornerRadius = 5 };
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == normal);
        button.Invoke();
        Assert(button.IsChecked());
        var selected = normal with { Background = new(0x333333), Foreground = new(0x666666), Padding = new(6) };
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == selected);
        button.Enabled = false;
        var disabled = selected with { Background = new(0x444444), BorderBrush = new(0x777777) };
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == disabled);
        button.SetControlStyleValues(StylePart.Root, new() { Background = new(0), CornerRadius = 0 });
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == disabled with { Background = new(0), CornerRadius = 0 });
        w.SetTheme(Theme.HighContrast);
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == disabled with { Background = new(0), CornerRadius = 0 });
        button.SetControlStyleValues(StylePart.Root, new());
        button.Enabled = true;
        button.Invoke();
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == normal);

        int callbacks = 0;
        var fresh = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new())], basedOn: derived);
        using var source = w.ImmutableSource(new CallbackSource(() =>
        {
            ++callbacks;
            Fails(7, () => button.ControlStyle = fresh);
            Fails(7, () => button.ControlStyle = style);
            Fails(7, () => button.ControlStyle = derived);
            Fails(7, () => button.ControlStyle = null);
            Fails(7, () => button.SetControlStyleValues(StylePart.Root, new()));
        }));
        w.ItemsView("Failure atomicity").SetSource(source).Select(new(1, 0));
        Assert(callbacks > 0);
        Assert(ReferenceEquals(button.ControlStyle, derived));
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == normal);
        button.ControlStyle = fresh;
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == normal);
    }

    private sealed class CallbackSource(Action callback) : IReadOnlyImmutableSource
    {
        public ulong Count => 1;
        public ItemKey Key(ulong index) => new(1, 0);
        public ulong? Find(ItemKey key) { callback(); return key == new ItemKey(1, 0) ? 0ul : null; }
        public ItemContent Item(ulong index, ulong column = 0) => new("Row");
    }

    // The ABI has no handle counter. Probe the monotonic token interval without releasing it.
    [DllImport("xui", EntryPoint = "xui_control_get_style_values", CallingConvention = CallingConvention.Cdecl)]
    private static extern int ProbeStyleHandle(ulong handle, uint part, uint effective, nint values, uint capacity, out uint count);
    private static ulong Handle(Element element) =>
        (ulong)typeof(Element).GetProperty("Handle", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(element)!;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference ApplyWithoutKeepingWrapper(Window window, ControlStyle style)
    {
        var button = window.Button("Collected representative").SetControlStyle(style);
        return new WeakReference(button);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference ApplyInTemporaryWindow(ControlStyle style)
    {
        using var window = new Window();
        window.Button("Temporary window").SetControlStyle(style);
        return new WeakReference(window);
    }

    private static void SharedDefinitions()
    {
        using var w = new Window();
        var buttons = Enumerable.Range(0, 256).Select(i => w.Button($"Shared {i}")).ToArray();
        var values = new PartStyleValues { Background = new(0x123456), Padding = new(3) };
        var parent = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, values)]);
        var style = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { CornerRadius = 4 })], basedOn: parent);
        var first = Handle(buttons[^1]);
        buttons[0].ControlStyle = style;
        var begin = Handle(w.Button("First definition interval"));
        Assert(begin - first == 3); // Two temporary handles, then this marker.
        for (int update = 0; update < 32; ++update)
            foreach (var button in buttons)
            {
                button.ControlStyle = style;
                Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == values with { CornerRadius = 4 });
            }
        buttons[0].ControlStyle = null;
        buttons[0].ControlStyle = style;
        var end = Handle(w.Button("Shared definition interval"));
        Assert(end == begin + 1);
        Console.WriteLine("C# shared style: 256 buttons, 8192 updates, zero new native handles after first application.");

        // Equal values do not merge distinct language definitions.
        buttons[0].ControlStyle = new ControlStyle(StyleTarget.Button, style.Parts, basedOn: parent);
        var distinct = Handle(w.Button("Distinct definition interval"));
        Assert(distinct == end + 3);
        buttons[0].ControlStyle = style;
        foreach (var button in buttons) button.ControlStyle = null;
        buttons[0].ControlStyle = style;
        var rebuilt = Handle(w.Button("Expired definition interval"));
        Assert(rebuilt == distinct + 3);
        buttons[0].ControlStyle = null;

        var weakButton = ApplyWithoutKeepingWrapper(w, style);
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Assert(!weakButton.IsAlive);
        var before = Handle(w.Button("Collected wrapper interval"));
        buttons[0].ControlStyle = style;
        Assert(Handle(w.Button("Collected wrapper reused")) == before + 1);

        var weakWindow = ApplyInTemporaryWindow(style);
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Assert(!weakWindow.IsAlive);
        Assert(buttons[0].GetControlStyleValues(StylePart.Root, effective: true) == values with { CornerRadius = 4 });
        w.Dispose();
        Throws<ObjectDisposedException>(() => buttons[0].ControlStyle = style);
        GC.KeepAlive(style);
    }

    private static void FailedApplicationCleanup()
    {
        var native = typeof(Window).Assembly.GetType("Xui.Native")!;
        Assert(Marshal.SizeOf(native.GetNestedType("ThemeColor", BindingFlags.NonPublic)!) == 8);
        Assert(Marshal.SizeOf(native.GetNestedType("StyleInsets", BindingFlags.NonPublic)!) == 16);
        Assert(Marshal.SizeOf(native.GetNestedType("StyleProperty", BindingFlags.NonPublic)!) == 80);
        var options = native.GetNestedType("ControlStyleOptions", BindingFlags.NonPublic)!;
        Assert(Marshal.SizeOf(options) == 40);
        Assert(Marshal.OffsetOf(options, "Properties") == 16);
        Assert(Marshal.OffsetOf(options, "BasedOn") == 32);

        using var w = new Window();
        var label = w.Label("Not a button");
        var first = Handle(label);
        var style = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new())]);
        for (int i = 1; i < 16; ++i) style = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new())], basedOn: style);
        for (int i = 0; i < 32; ++i)
        {
            Fails(1, () => label.SetControlStyle(style));
            Assert(label.ControlStyle is null);
        }
        var button = w.Button("Retry");
        for (ulong handle = first + 1; handle < Handle(button); ++handle)
            Assert(ProbeStyleHandle(handle, (uint)StylePart.Root, 0, 0, 0, out _) == 2);
        button.ControlStyle = style;
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == new PartStyleValues());
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference ApplyTemporary(Button button, uint color)
    {
        var style = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { Background = new(color) })]);
        var derived = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { CornerRadius = 3 })], basedOn: style);
        button.ControlStyle = derived;
        Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == new PartStyleValues { Background = new(color), CornerRadius = 3 });
        button.ControlStyle = null;
        return new WeakReference(derived);
    }

    private static void BoundedLifetime()
    {
        using var w = new Window();
        var button = w.Button("Lifetime");
        var first = Handle(button);
        WeakReference? weak = null;
        const int iterations = 512;
        for (uint i = 0; i < iterations; ++i)
        {
            weak = ApplyTemporary(button, i);
            button.SetControlStyleValues(StylePart.Root, new() { Padding = new(1) });
            button.SetControlStyleValues(StylePart.Root, new());
            Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == new PartStyleValues());
        }
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Assert(weak is { IsAlive: false });
        var last = Handle(w.Button("End of lifetime interval"));
        int live = 0;
        for (ulong handle = first + 1; handle < last; ++handle)
        {
            var status = ProbeStyleHandle(handle, (uint)StylePart.Root, 0, 0, 0, out _);
            Assert(status is 2 or 3);
            if (status == 3) ++live;
        }
        Console.WriteLine($"C# native style handles: {last - first - 1} issued, {live} live after {iterations} apply/clear cycles and GC.");
        Assert(live == 0);
        Assert(last - first - 1 >= iterations * 2);

        var shared = new ControlStyle(StyleTarget.Button, [new(StylePart.Root, new() { Padding = new(2) })]);
        for (int i = 0; i < 16; ++i)
        {
            using var other = new Window();
            other.Button("Shared").SetControlStyle(shared);
            button.ControlStyle = shared;
            other.Dispose();
            Assert(button.GetControlStyleValues(StylePart.Root, effective: true) == shared.Parts[0].Values);
            button.ControlStyle = null;
        }
    }
}

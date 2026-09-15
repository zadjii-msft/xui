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

        var values = new ButtonStyleValues { Background = new ThemeColor(0), Padding = new Insets(4), CornerRadius = 0 };
        ButtonStyleRule[] rules = [new(ButtonStyleState.Hovered, new() { Foreground = new ThemeColor(0xffffff) })];
        var style = new ButtonStyle(values, rules);
        rules[0] = new(ButtonStyleState.Pressed, new());
        Assert(style.Rules[0].State == ButtonStyleState.Hovered);
        Throws<ArgumentOutOfRangeException>(() => new ButtonStyle(new() { CornerRadius = float.NaN }));
        Throws<ArgumentOutOfRangeException>(() => new ButtonStyle(new() { Padding = new Insets(-1) }));
        Throws<ArgumentOutOfRangeException>(() => new ButtonStyle(new() { Background = new ThemeColor(0xff000000) }));
        Throws<ArgumentOutOfRangeException>(() => new ButtonStyle(new(), [new((ButtonStyleState)5, new())]));
        Throws<ArgumentException>(() => new ButtonStyle(new(), Enumerable.Repeat(rules[0], 257).ToArray()));
        for (int i = 1; i < 16; ++i) style = new ButtonStyle(new(), basedOn: style);
        Throws<ArgumentException>(() => new ButtonStyle(new(), basedOn: style));
        Console.WriteLine("C# immutable style and resource tests passed.");
    }
    internal static void Native()
    {
        using var w = new Window();
        var button = w.Button("Styled");
        var values = new ButtonStyleValues { Background = new ThemeColor(0x123456, 0x654321), Padding = new Insets(3) };
        var style = new ButtonStyle(values);
        var derived = new ButtonStyle(new() { CornerRadius = 4 }, basedOn: style);
        var local = new ButtonStyleValues { CornerRadius = 7 };
        Assert(button.Style is null && button.StyleValues == new ButtonStyleValues());
        Assert(ReferenceEquals(button.SetStyleValues(local), button));
        Assert(ReferenceEquals(button.SetStyle(derived), button));
        Assert(ReferenceEquals(button.Style, derived));
        Assert(button.StyleValues == local);
        Assert(button.EffectiveStyleValues == values with { CornerRadius = 7 });
        Throws<ArgumentOutOfRangeException>(() => button.StyleValues = new() { BorderThickness = new Insets(float.PositiveInfinity) });
        Assert(button.StyleValues == local);
        Assert(button.EffectiveStyleValues == values with { CornerRadius = 7 });
        button.Style = null;
        Assert(button.EffectiveStyleValues == local);
        button.StyleValues = new();
        Assert(button.EffectiveStyleValues == new ButtonStyleValues());
        using var other = new Window();
        var shared = other.Button("Shared").SetStyle(style);
        Assert(shared.EffectiveStyleValues == values);
        Task.Run(() =>
        {
            Fails(4, () => button.Style = style);
            Fails(4, () => button.Style = null);
            Fails(4, () => _ = button.StyleValues);
            Fails(4, () => _ = button.EffectiveStyleValues);
        }).GetAwaiter().GetResult();
        Assert(button.Style is null && button.StyleValues == new ButtonStyleValues());
        button.Style = derived;
        Assert(button.EffectiveStyleValues == values with { CornerRadius = 4 });
        w.Dispose();
        Throws<ObjectDisposedException>(() => button.Style = style);
        Throws<ObjectDisposedException>(() => button.Style = null);
        Throws<ObjectDisposedException>(() => _ = button.Style);
        Throws<ObjectDisposedException>(() => button.StyleValues = new());
        Throws<ObjectDisposedException>(() => _ = button.StyleValues);
        Throws<ObjectDisposedException>(() => _ = button.EffectiveStyleValues);
        Assert(shared.EffectiveStyleValues == values);
        StateAndInheritance();
        FailedApplicationCleanup();
        BoundedLifetime();
        SharedDefinitions();
        Console.WriteLine($"C# styling assertions: {assertions} passed; architecture: {RuntimeInformation.ProcessArchitecture}");
    }

    private static void StateAndInheritance()
    {
        using var w = new Window();
        var baseValues = new ButtonStyleValues
        {
            Background = new(0x123456, 0x654321), Foreground = new(0x101010),
            BorderBrush = new(0x202020), BorderThickness = new(1, 2, 3, 4),
            Padding = new(4, 3, 2, 1), CornerRadius = 2
        };
        var style = new ButtonStyle(baseValues,
        [
            new(ButtonStyleState.Checked, new() { Background = new(0x333333), Padding = new(6) }),
            new(ButtonStyleState.Disabled, new() { Background = new(0x444444) })
        ]);
        var derived = new ButtonStyle(new() { CornerRadius = 5 },
        [
            new(ButtonStyleState.Checked, new() { Foreground = new(0x555555) }),
            new(ButtonStyleState.Checked, new() { Foreground = new(0x666666) }),
            new(ButtonStyleState.Disabled, new() { BorderBrush = new(0x777777) })
        ], style);
        var button = w.Button("State").Behavior(ButtonBehavior.Toggle).SetStyle(derived);
        var normal = baseValues with { CornerRadius = 5 };
        Assert(button.EffectiveStyleValues == normal);
        button.Invoke();
        Assert(button.IsChecked());
        var selected = normal with { Background = new(0x333333), Foreground = new(0x666666), Padding = new(6) };
        Assert(button.EffectiveStyleValues == selected);
        button.Enabled = false;
        var disabled = selected with { Background = new(0x444444), BorderBrush = new(0x777777) };
        Assert(button.EffectiveStyleValues == disabled);
        button.StyleValues = new() { Background = new(0), CornerRadius = 0 };
        Assert(button.EffectiveStyleValues == disabled with { Background = new(0), CornerRadius = 0 });
        w.SetTheme(Theme.HighContrast);
        Assert(button.EffectiveStyleValues == disabled with { Background = new(0), CornerRadius = 0 });
        button.StyleValues = new();
        button.Enabled = true;
        button.Invoke();
        Assert(button.EffectiveStyleValues == normal);

        int callbacks = 0;
        var fresh = new ButtonStyle(new(), basedOn: derived);
        using var source = w.ImmutableSource(new CallbackSource(() =>
        {
            ++callbacks;
            Fails(7, () => button.Style = fresh);
            Fails(7, () => button.Style = style);
            Fails(7, () => button.Style = derived);
            Fails(7, () => button.Style = null);
            Fails(7, () => button.StyleValues = new());
        }));
        w.ItemsView("Failure atomicity").SetSource(source).Select(new(1, 0));
        Assert(callbacks > 0);
        Assert(ReferenceEquals(button.Style, derived));
        Assert(button.EffectiveStyleValues == normal);
        button.Style = fresh;
        Assert(button.EffectiveStyleValues == normal);
    }

    private sealed class CallbackSource(Action callback) : IReadOnlyImmutableSource
    {
        public ulong Count => 1;
        public ItemKey Key(ulong index) => new(1, 0);
        public ulong? Find(ItemKey key) { callback(); return key == new ItemKey(1, 0) ? 0ul : null; }
        public ItemContent Item(ulong index, ulong column = 0) => new("Row");
    }

    // The ABI has no handle counter. Probe the monotonic token interval without releasing it.
    [DllImport("xui", EntryPoint = "xui_button_get_style_values", CallingConvention = CallingConvention.Cdecl)]
    private static extern int ProbeStyleHandle(ulong handle, uint effective, nint values);
    private static ulong Handle(Element element) =>
        (ulong)typeof(Element).GetProperty("Handle", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(element)!;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference ApplyWithoutKeepingWrapper(Window window, ButtonStyle style)
    {
        var button = window.Button("Collected representative").SetStyle(style);
        return new WeakReference(button);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference ApplyInTemporaryWindow(ButtonStyle style)
    {
        using var window = new Window();
        window.Button("Temporary window").SetStyle(style);
        return new WeakReference(window);
    }

    private static void SharedDefinitions()
    {
        using var w = new Window();
        var buttons = Enumerable.Range(0, 256).Select(i => w.Button($"Shared {i}")).ToArray();
        var values = new ButtonStyleValues { Background = new(0x123456), Padding = new(3) };
        var parent = new ButtonStyle(values);
        var style = new ButtonStyle(new() { CornerRadius = 4 }, basedOn: parent);
        var first = Handle(buttons[^1]);
        buttons[0].Style = style;
        var begin = Handle(w.Button("First definition interval"));
        Assert(begin - first == 3); // Two temporary handles, then this marker.
        for (int update = 0; update < 32; ++update)
            foreach (var button in buttons)
            {
                button.Style = style;
                Assert(button.EffectiveStyleValues == values with { CornerRadius = 4 });
            }
        buttons[0].Style = null;
        buttons[0].Style = style;
        var end = Handle(w.Button("Shared definition interval"));
        Assert(end == begin + 1);
        Console.WriteLine("C# shared style: 256 buttons, 8192 updates, zero new native handles after first application.");

        // Equal values do not merge distinct language definitions.
        buttons[0].Style = new ButtonStyle(style.Values, basedOn: parent);
        var distinct = Handle(w.Button("Distinct definition interval"));
        Assert(distinct == end + 3);
        buttons[0].Style = style;
        foreach (var button in buttons) button.Style = null;
        buttons[0].Style = style;
        var rebuilt = Handle(w.Button("Expired definition interval"));
        Assert(rebuilt == distinct + 3);
        buttons[0].Style = null;

        var weakButton = ApplyWithoutKeepingWrapper(w, style);
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Assert(!weakButton.IsAlive);
        var before = Handle(w.Button("Collected wrapper interval"));
        buttons[0].Style = style;
        Assert(Handle(w.Button("Collected wrapper reused")) == before + 1);

        var weakWindow = ApplyInTemporaryWindow(style);
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Assert(!weakWindow.IsAlive);
        Assert(buttons[0].EffectiveStyleValues == values with { CornerRadius = 4 });
        w.Dispose();
        Throws<ObjectDisposedException>(() => buttons[0].Style = style);
        GC.KeepAlive(style);
    }

    private static void FailedApplicationCleanup()
    {
        var native = typeof(Window).Assembly.GetType("Xui.Native")!;
        Assert(Marshal.SizeOf(native.GetNestedType("ThemeColor", BindingFlags.NonPublic)!) == 8);
        Assert(Marshal.SizeOf(native.GetNestedType("StyleInsets", BindingFlags.NonPublic)!) == 16);
        Assert(Marshal.SizeOf(native.GetNestedType("ButtonStyleValues", BindingFlags.NonPublic)!) == 80);
        Assert(Marshal.SizeOf(native.GetNestedType("ButtonStyleRule", BindingFlags.NonPublic)!) == 88);
        var options = native.GetNestedType("ButtonStyleOptions", BindingFlags.NonPublic)!;
        Assert(Marshal.SizeOf(options) == 112);
        Assert(Marshal.OffsetOf(options, "Rules") == 88);
        Assert(Marshal.OffsetOf(options, "BasedOn") == 104);

        using var w = new Window();
        var label = w.Label("Not a button");
        var first = Handle(label);
        var invalid = (Button)Activator.CreateInstance(typeof(Button),
            BindingFlags.Instance | BindingFlags.NonPublic, null, [w, first], null)!;
        var style = new ButtonStyle(new());
        for (int i = 1; i < 16; ++i) style = new ButtonStyle(new(), basedOn: style);
        for (int i = 0; i < 32; ++i)
        {
            try { invalid.Style = style; throw new Exception("Expected wrong-kind failure."); }
            catch (XuiException error) { Assert(error.Status == 3 && error.Message.Contains("Wrong handle kind")); }
            Assert(invalid.Style is null);
        }
        var button = w.Button("Retry");
        for (ulong handle = first + 1; handle < Handle(button); ++handle)
            Assert(ProbeStyleHandle(handle, 0, 0) == 2);
        button.Style = style;
        Assert(button.EffectiveStyleValues == new ButtonStyleValues());
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference ApplyTemporary(Button button, uint color)
    {
        var style = new ButtonStyle(new() { Background = new(color) });
        var derived = new ButtonStyle(new() { CornerRadius = 3 }, basedOn: style);
        button.Style = derived;
        Assert(button.EffectiveStyleValues == new ButtonStyleValues { Background = new(color), CornerRadius = 3 });
        button.Style = null;
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
            button.StyleValues = new() { Padding = new(1) };
            button.StyleValues = new();
            Assert(button.EffectiveStyleValues == new ButtonStyleValues());
        }
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Assert(weak is { IsAlive: false });
        var last = Handle(w.Button("End of lifetime interval"));
        int live = 0;
        for (ulong handle = first + 1; handle < last; ++handle)
        {
            var status = ProbeStyleHandle(handle, 0, 0);
            Assert(status is 2 or 3);
            if (status == 3) ++live;
        }
        Console.WriteLine($"C# native style handles: {last - first - 1} issued, {live} live after {iterations} apply/clear cycles and GC.");
        Assert(live == 0);
        Assert(last - first - 1 >= iterations * 2);

        var shared = new ButtonStyle(new() { Padding = new(2) });
        for (int i = 0; i < 16; ++i)
        {
            using var other = new Window();
            other.Button("Shared").SetStyle(shared);
            button.Style = shared;
            other.Dispose();
            Assert(button.EffectiveStyleValues == shared.Values);
            button.Style = null;
        }
    }
}

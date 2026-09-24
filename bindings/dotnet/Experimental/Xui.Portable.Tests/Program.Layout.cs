using System.Text.Json;
using PortableLayout;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void AxisLayoutChecks()
    {
        using var corpus = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "AxisLayoutScenarios.json")));
        Assert(corpus.RootElement.GetProperty("version").GetInt32() == 1, "Axis corpus version.");
        var compatible = new MeasureConstraint(MeasureMode.AtMost, 120);
        var (oldMode, oldSize) = compatible;
        Assert(oldMode == MeasureMode.AtMost && oldSize == 120 && !compatible.IsUnbounded, "Two-argument construction and deconstruction keep their bounded meaning.");
        foreach (var item in corpus.RootElement.GetProperty("axes").EnumerateArray())
        {
            string name = item.GetProperty("name").GetString()!;
            var offer = new MeasureConstraint(Enum.Parse<MeasureMode>(item.GetProperty("mode").GetString()!), item.GetProperty("available").GetSingle(),
                item.TryGetProperty("unboundedContext", out var context) && context.GetBoolean());
            var authored = item.GetProperty("axis");
            AxisConstraints? axis = authored.ValueKind == JsonValueKind.Null ? null : new(
                authored.TryGetProperty("length", out var length) ? length.GetSingle() : null,
                authored.TryGetProperty("minimum", out var minimum) ? minimum.GetSingle() : 0,
                authored.TryGetProperty("maximum", out var maximum) ? maximum.GetSingle() : null);
            float? fixedLength = item.TryGetProperty("legacyFixed", out var fixedValue) ? fixedValue.GetSingle() : null;
            float? preferred = item.TryGetProperty("legacyPreferred", out var preferredValue) ? preferredValue.GetSingle() : null;
            var content = LayoutMath.ConstrainMeasure(offer, axis, fixedLength, preferred);
            Assert(content.Mode.ToString() == item.GetProperty("offerMode").GetString() && content.Size == item.GetProperty("offerSize").GetSingle(), name + " constrains native measure before intrinsic measurement.");
            Assert(content.IsUnbounded == (item.TryGetProperty("expectedUnbounded", out var unbounded) ? unbounded.GetBoolean() : content.Mode == MeasureMode.Unspecified),
                name + " propagates the declared intrinsic context independently of finite clipping.");
            Assert(LayoutMath.MeasureAxis(item.GetProperty("natural").GetSingle(), offer, axis, fixedLength, preferred) == item.GetProperty("expected").GetSingle(), name + " literal resolved size.");
        }
        foreach (var item in corpus.RootElement.GetProperty("stacks").EnumerateArray())
        {
            var offered = new MeasureConstraint(Enum.Parse<MeasureMode>(item.GetProperty("mode").GetString()!), item.GetProperty("available").GetSingle(),
                item.TryGetProperty("unboundedContext", out var context) && context.GetBoolean());
            var result = LayoutMath.AllocateStack(offered, item.GetProperty("spacing").GetSingle(),
                item.GetProperty("desired").EnumerateArray().Select(v => v.GetSingle()).ToArray(),
                item.GetProperty("weights").EnumerateArray().Select(v => v.GetSingle()).ToArray());
            var expected = item.GetProperty("slots").EnumerateArray().Select(v => new LayoutSlot(v[0].GetSingle(), v[1].GetSingle())).ToArray();
            Assert(result.Extent == item.GetProperty("extent").GetSingle() && result.Slots.SequenceEqual(expected), item.GetProperty("name").GetString() + " literal stack allocation.");
        }
        Assert(LayoutMath.ArrangeAxis(60, new AxisConstraints(Maximum: 40)) == 40, "A capped flex child uses only its maximum without redistributing its slot.");
        foreach (var axis in new[]
        {
            new AxisConstraints(float.NaN), new AxisConstraints(float.PositiveInfinity), new AxisConstraints(-1),
            new AxisConstraints(Minimum: -1), new AxisConstraints(Maximum: float.PositiveInfinity),
            new AxisConstraints(Minimum: 20, Maximum: 10), new AxisConstraints(30, 40), new AxisConstraints(30, Maximum: 20)
        })
            Throws<ArgumentException>(() => LayoutMath.MeasureAxis(10, MeasureConstraint.AtMost(100), axis));
        Throws<ArgumentOutOfRangeException>(() => LayoutMath.MeasureAxis(float.NaN, MeasureConstraint.Unspecified, null));
        Throws<ArgumentException>(() => LayoutMath.ConstrainMeasure(new(MeasureMode.Unspecified, 1), null));
        Throws<ArgumentException>(() => LayoutMath.AllocateStack(MeasureConstraint.AtMost(100), 0, [10], []));
        Throws<ArgumentOutOfRangeException>(() => LayoutMath.AllocateStack(MeasureConstraint.AtMost(100), 0, [10], [-1]));
        Throws<OverflowException>(() => LayoutMath.AllocateStack(MeasureConstraint.Unspecified, 0, [float.MaxValue, float.MaxValue], [0, 0]));
        for (int i = 1; i <= 100; i++)
        {
            var result = LayoutMath.AllocateStack(MeasureConstraint.Exactly(i / 7f), 0.1f, [1, 2, 3], [1, 2, 3]);
            Assert(result.Slots.All(slot => slot.Offset >= 0 && slot.Length >= 0 &&
                (double)slot.Offset + slot.Length <= result.Extent), "Fractional allocation never overflows parent bounds.");
        }

        using var host = new Host(new Dispatcher());
        var demo = new AxisSizingShowcase(host);
        Assert(demo.Input.FixedSize == new Size(180, 90) && demo.Input.WidthConstraints == AxisConstraints.Fixed(280) &&
            demo.Input.HeightConstraints == AxisConstraints.Auto, "New axis metadata preserves original two-axis legacy state.");
        var unsupported = new Backend();
        Throws<NotSupportedException>(() => host.Attach(unsupported));
        Assert(unsupported.Disposed && unsupported.Peers.All(peer => peer.Disposed) && !host.IsAttached, "Attach rejects missing constraint capability and releases partial peers.");
        var backend = new AxisBackend();
        host.Attach(backend);
        var input = backend.Find("axes-input");
        demo.WidthOnlyInput.SetHeight(96);
        input.Updates.Clear();
        demo.FieldWidth = 220;
        Assert(demo.WidthOnlyInput.HeightConstraints == AxisConstraints.Fixed(96), "A width-only authored binding preserves independent height.");
        Assert(input.Updates.SequenceEqual([ElementProperty.Constraints]), "Paired authored dimensions update in one coherent notification.");
        demo.HeightOnlyInput.SetWidth(210);
        demo.FieldHeight = 80;
        Assert(demo.HeightOnlyInput.WidthConstraints == AxisConstraints.Fixed(210), "A height-only authored binding preserves independent width.");
        demo.FieldHeight = AxisConstraints.Auto;
        var old = (demo.Input.WidthConstraints, demo.Input.HeightConstraints);
        Throws<ArgumentException>(() => demo.Input.SetConstraints(100, new AxisConstraints(Minimum: 40, Maximum: 30)));
        Assert(old == (demo.Input.WidthConstraints, demo.Input.HeightConstraints), "Both axes validate before either value changes.");
        demo.Input.SetWidth(AxisConstraints.Auto);
        Assert(demo.Input.FixedSize == new Size(180, 90) && demo.Input.HeightConstraints == AxisConstraints.Auto, "Explicit Auto does not overwrite either legacy dimension or the other override.");
        demo.Input.SetWidth(null);
        Assert(demo.Input.WidthConstraints is null && demo.Input.HeightConstraints == AxisConstraints.Auto, "Null restores inheritance on only the selected axis.");
        ElementExtensions.FixedSize(demo.Input, 200, 100);
        Assert(demo.Input.WidthConstraints is null && demo.Input.FixedSize == new Size(200, 100), "Legacy setters retain current values behind overrides.");
        input.Events.Change("retained draft");
        Assert(demo.Draft == "retained draft", "Constrained input retains normal native change delivery.");
        demo.FieldWidth = 280;
        input.FailUpdate = true;
        Throws<ApplicationException>(() => demo.FieldWidth = 240);
        Assert(!host.IsAttached && demo.Input.WidthConstraints == AxisConstraints.Fixed(240), "Native constraint update failure detaches and retains committed constraints.");
        demo.FieldWidth = 280;
        Assert(demo.Input.WidthConstraints == AxisConstraints.Fixed(280), "Returning to a previously cached axis binding after native failure updates the retained model.");
        var recovery = new AxisBackend();
        host.Attach(recovery);
        Assert(((TextInput)recovery.Find("axes-input").Element).Text == "retained draft", "Constraint updates and reattachment preserve authored editor text.");

        using var fixedHost = new Host(new Dispatcher());
        var greeting = new PortableDemo.Greeting(fixedHost);
        var fixedBackend = new Backend();
        fixedHost.Attach(fixedBackend);
        Throws<NotSupportedException>(() => greeting.Input.SetWidth(200));
        Assert(fixedHost.IsAttached && greeting.Input.WidthConstraints is null, "Runtime capability rejection is prevalidation, not a silent update or destructive detach.");
        Console.WriteLine("Axis layout: 23 literal sizing cases and 10 literal stack allocations passed.");
    }

    private sealed class AxisBackend : IBackend
    {
        public List<AxisPeer> Peers { get; } = [];
        public AxisPeer Find(string id) => Peers.Single(peer => peer.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new AxisPeer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { }
    }

    private sealed class AxisPeer(Element element, IControlEvents events) : IConstrainedElementPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public List<ElementProperty> Updates { get; } = [];
        public bool FailUpdate { get; set; }
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property)
        {
            if (FailUpdate) throw new ApplicationException("native constraints");
            Updates.Add(property);
        }
        public void Dispose() { }
    }
}

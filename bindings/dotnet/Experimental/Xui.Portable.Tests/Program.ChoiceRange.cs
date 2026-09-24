using System.Text.Json;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void ChoiceRangeChecks()
    {
        using var corpus = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "RangeMathScenarios.json")));
        Assert(corpus.RootElement.GetProperty("version").GetInt32() == 1, "Range math corpus version.");
        foreach (var item in corpus.RootElement.GetProperty("cases").EnumerateArray())
        {
            var fields = item.GetProperty("range");
            var range = new NumericRange(fields[0].GetDouble(), fields[1].GetDouble(), fields[2].GetDouble(), fields[3].GetDouble());
            double actual = item.TryGetProperty("fraction", out var fraction)
                ? RangeMath.Snap(range, fraction.GetDouble())
                : RangeMath.Step(range, item.GetProperty("value").GetDouble(), Enum.Parse<RangeKey>(item.GetProperty("key").GetString()!));
            Assert(actual == item.GetProperty("expected").GetDouble(), "Literal range arithmetic: " + item.GetProperty("name").GetString());
        }
        Throws<ArgumentOutOfRangeException>(() => RangeMath.Validate(new(0, 0)));
        Throws<ArgumentOutOfRangeException>(() => RangeMath.Validate(new(-double.MaxValue, double.MaxValue)));
        Throws<ArgumentOutOfRangeException>(() => RangeMath.Validate(new(0, 10, double.NaN)));
        Throws<ArgumentOutOfRangeException>(() => RangeMath.Snap(new(0, 10), double.NaN));
        Throws<ArgumentOutOfRangeException>(() => RangeMath.Step(new(0, 10), 2, (RangeKey)99));
        Assert(RangeMath.Step(new(0, 10, double.MaxValue), 5, RangeKey.Increase) == 10, "Oversized finite steps clamp at the maximum.");
        Assert(double.IsFinite(RangeMath.Snap(new(0, 10, double.Epsilon), 0.5)), "Overflowing step counts fall back to a finite pointer value.");

        using var host = new Host(new Dispatcher());
        var app = new ChoiceRangeWorkbench(host);
        var choice = app.Delivery;
        var rangeInput = app.Adjustment;
        var unsupported = new Backend();
        Throws<NotSupportedException>(() => host.Attach(unsupported));
        Assert(unsupported.Disposed && !host.IsAttached, "Old backends explicitly reject the new selection/range capability.");
        var backend = new ChoiceRangeBackend();
        host.Attach(backend);
        var choicePeer = backend.Find("delivery-choice");
        var rangePeer = backend.Find("amount-range");
        var peers = backend.Peers.ToArray();
        ulong highId = 9007199254740993UL;
        Assert(choice.Selected == 1 && choicePeer.NativeSelected == 1 && choice.Items.Count == 3, "Initial atomic items and selection.");
        int changed = 0;
        choice.Changed += id => { Assert(choice.Selected == id, "Native selected ID is recorded before callbacks."); changed++; };
        choicePeer.Select(highId);
        Assert(choice.Selected == highId && app.Options.Selected == highId && changed == 1, "A stable ID above double exact-integer range reaches authored C# unchanged.");
        Assert(choicePeer.Updates.Count == 0, "A native selection is not echoed back into its control.");
        choicePeer.Select(highId);
        Assert(changed == 1, "Repeated selection is silent.");
        Throws<ArgumentException>(() => choicePeer.Select(17));
        Throws<ArgumentException>(() => choicePeer.Select(99));
        Assert(choice.Selected == highId && changed == 1, "Disabled or nonexistent native IDs never mutate the model.");
        backend.Find("choice-reverse").Events.Click();
        Assert(choice.Selected == highId && choice.Items[0].Id == 17, "Reordering retains selected identity rather than selecting an ordinal.");
        choice.SetItems([new(1, "New standard"), new(highId, "Renamed priority"), new(17, "Disabled", false)]);
        Assert(choice.Selected == highId && changed == 1, "Omitted selection preserves an enabled survivor silently.");
        choice.SetItems([new(1, "Fallback"), new(highId, "Now disabled", false)]);
        Assert(choice.Selected == 1 && changed == 1, "Disabled survivor falls back to first enabled choice without a callback.");
        choice.SetItems([new(1, "Disabled", false)]);
        Assert(choice.Selected is null && choicePeer.NativeSelected is null, "All-disabled choices expose absence, not a fake disabled selection.");
        choice.SetItems([]);
        Assert(choice.Items.Count == 0 && choice.Selected is null, "Empty choices have no selected ID.");
        choice.SetItems([new(SingleChoice.MaximumId, "Maximum stable ID")]);
        Assert(choice.Selected == SingleChoice.MaximumId, "Maximum supported stable ID remains exact.");
        var priorItems = choice.Items;
        foreach (var invalid in new Choice[][]
        {
            [new(0, "Zero")], [new(SingleChoice.MaximumId + 1, "Too large")],
            [new(1, "Duplicate"), new(1, "Duplicate")], [new(1, null!)], [new(1, "bad\0label")]
        })
            Throws<ArgumentException>(() => choice.SetItems(invalid));
        Throws<ArgumentException>(() => choice.SetItems([new(1, "Disabled", false)], 1));
        Throws<ArgumentOutOfRangeException>(() => choice.SetItems(Enumerable.Range(1, 4097).Select(id => new Choice((ulong)id, "Choice")).ToArray()));
        Assert(ReferenceEquals(choice.Items, priorItems) && choice.Selected == SingleChoice.MaximumId, "Invalid snapshots preserve both items and selected ID.");
        Choice[] mutable = [new(1, "One"), new(2, "Two")];
        choice.SetItems(mutable, 2);
        mutable[1] = new(3, "Changed caller array");
        Assert(choice.Items[1].Id == 2 && choice.Selected == 2, "Caller mutations cannot change the retained choice snapshot.");
        Throws<NotSupportedException>(() => ((IList<Choice>)choice.Items)[0] = default);
        choicePeer.Reject = true;
        Throws<NotSupportedException>(() => choice.SetSelected(1));
        Assert(choice.Selected == 2 && host.IsAttached, "Native representability rejection is precommit.");
        choicePeer.Reject = false;

        int previews = 0, commits = 0, cancels = 0;
        rangeInput.Previewed += value => { Assert(rangeInput.PreviewValue == value, "Preview is visible before handler delivery."); previews++; };
        rangeInput.Changed += value => { Assert(rangeInput.Value == value && rangeInput.PreviewValue == value, "Commit updates both effective values before handlers."); commits++; };
        rangeInput.Canceled += value => { Assert(rangeInput.Value == value && rangeInput.PreviewValue == value, "Cancellation restores the committed value before handlers."); cancels++; };
        Assert(rangeInput.Value == 2.25 && rangePeer.NativeValue == 2.25, "Off-step programmatic values are not silently snapped.");
        rangePeer.Preview(7.5);
        Assert(previews == 1 && commits == 0 && rangeInput.Value == 2.25 && rangeInput.PreviewValue == 7.5, "Preview is not a committed edit.");
        rangePeer.Cancel();
        Assert(cancels == 1 && rangeInput.PreviewValue == 2.25 && commits == 0, "A real native cancellation restores the original committed value.");
        rangePeer.Preview(6);
        rangePeer.Commit(6);
        Assert(commits == 1 && rangeInput.Value == 6 && app.Amount == 6, "Actual native commit reaches authored state once.");
        Assert(rangePeer.Updates.Count == 0, "Preview, commit and cancellation do not echo programmatic setters.");
        rangePeer.Preview(8);
        rangePeer.Preview(6);
        Assert(rangeInput.PreviewValue == 6, "Returning to the committed value requires no invented drag-active state.");
        rangeInput.SetValue(6);
        Assert(commits == 1 && cancels == 1 && rangePeer.Updates.SequenceEqual([ElementProperty.Value]), "Same-value programmatic setter clears pending preview silently.");
        rangePeer.Preview(8);
        rangeInput.SetRange(new(0, 4, 0.25, 1));
        Assert(rangeInput.Value == 4 && rangeInput.PreviewValue == 4 && rangePeer.NativeValue == 4, "Range change atomically clamps committed value and clears preview.");
        Assert(commits == 1 && cancels == 1, "Programmatic range cancellation invokes no user callback.");
        Throws<ArgumentOutOfRangeException>(() => rangeInput.Value = double.NaN);
        Throws<ArgumentOutOfRangeException>(() => rangeInput.Value = 5);
        Throws<ArgumentOutOfRangeException>(() => rangePeer.Preview(5));
        Throws<ArgumentOutOfRangeException>(() => rangePeer.Commit(-1));
        Throws<InvalidOperationException>(() => ((IRangeControlEvents)rangePeer.Events).RangeCanceled(3));
        Assert(rangeInput.Value == 4 && host.IsAttached, "Invalid range events/setters preserve valid state.");
        rangePeer.Reject = true;
        Throws<NotSupportedException>(() => rangeInput.Value = 3);
        Assert(rangeInput.Value == 4 && host.IsAttached, "Native range representability rejection precedes mutation.");
        rangePeer.Reject = false;
        rangePeer.Preview(3);
        app.Content.Enabled = false;
        Assert(rangeInput.PreviewValue == rangeInput.Value && cancels == 1, "Disabled ancestry silently clears preview.");
        Assert(!((ISelectionControlEvents)choicePeer.Events).SelectionChanged(1) &&
            !((IRangeControlEvents)rangePeer.Events).RangePreviewed(2), "Disabled ancestry blocks choice and range user input.");
        ((IRangeControlEvents)rangePeer.Events).RangeCanceled(4);
        Assert(cancels == 1, "Cancellation after programmatic disable cannot become a user cancel callback.");
        app.Content.Enabled = true;
        rangePeer.Preview(2);
        app.Content.Visible = false;
        Assert(rangeInput.PreviewValue == 4 && cancels == 1, "Hidden ancestry also clears preview silently.");
        app.Content.Visible = true;
        rangeInput.Range = new(0, 10, 0.5, 2);
        rangeInput.Value = 2.25;
        app.Amount = 2.25;
        rangePeer.Commit(RangeMath.Step(rangeInput.Range, rangeInput.Value, RangeKey.Increase));
        Assert(rangeInput.Value == 2.75, "Keyboard steps preserve the off-grid origin.");
        backend.Find("choice-range-draft").Events.Change("retained \u674e");
        backend.Find("choice-range-reset").Events.Click();
        Assert(app.Draft == "" && app.Amount == 2.25 && app.ChoiceChanges == 0 && app.RangeChanges == 0 && app.RangeCancels == 0, "Reset restores the shared sample without user choice/range callbacks.");
        Assert(peers.SequenceEqual(backend.Peers), "Control updates do not replace any native editor or sibling peer.");
        Task.Run(() => Throws<InvalidOperationException>(() => ((IRangeControlEvents)rangePeer.Events).RangePreviewed(3))).GetAwaiter().GetResult();
        Throws<InvalidOperationException>(() => choicePeer.Events.Change("wrong kind"));
        Throws<InvalidOperationException>(() => rangePeer.Events.Click());
        host.Detach();
        Assert(!((ISelectionControlEvents)choicePeer.Events).SelectionChanged(1) &&
            !((IRangeControlEvents)rangePeer.Events).RangeCanceled(2.25), "Detached choice/range sinks remain stale.");
        Assert(rangeInput.PreviewValue == rangeInput.Value, "Detach clears any native-only preview without a cancellation event.");
        host.Dispose();
        rangeInput.Previewed -= _ => { };
        choice.Changed -= _ => { };
        Throws<ObjectDisposedException>(() => _ = rangeInput.Value);
        ChoiceRangeFailureChecks();
        ChoiceRangeCorpusChecks();
    }

    private static void ChoiceRangeFailureChecks()
    {
        using var host = new Host(new Dispatcher());
        var app = new ChoiceRangeWorkbench(host);
        var backend = new ChoiceRangeBackend();
        host.Attach(backend);
        var original = app.Options;
        Throws<ArgumentNullException>(() => app.Options = (null!, null));
        Assert(app.Delivery.Selected == 1 && host.IsAttached, "A null authored items array is rejected rather than silently clearing choices.");
        app.Options = original;
        backend.Find("delivery-choice").Validating = () => Throws<InvalidOperationException>(() => app.Draft = "reentrant");
        app.Delivery.SetSelected(9007199254740993UL);
        Assert(app.Draft == "", "Choice representability validation cannot reenter authored state mutation.");
        backend.Find("delivery-choice").Validating = null;
        app.Delivery.SetSelected(1);
        backend.Find("amount-range").Validating = () => Throws<InvalidOperationException>(() => app.Available = false);
        app.Adjustment.Value = 3.25;
        Assert(app.Available, "Range representability validation remains read-only.");
        backend.Find("amount-range").Validating = null;
        app.Adjustment.Value = 2.25;
        backend.Find("delivery-choice").FailUpdate = true;
        Throws<ApplicationException>(() => app.Options = ([new(2, "Replacement")], 2UL));
        Assert(!host.IsAttached && app.Delivery.Selected == 2, "Native choice update failure retains the coherent new snapshot.");
        app.Options = original;
        Assert(app.Delivery.Selected == 1 && app.Delivery.Items.SequenceEqual(original.Items), "Returning to the old choice binding after failure updates the retained control.");
        var next = new ChoiceRangeBackend();
        host.Attach(next);
        next.Find("amount-range").FailUpdate = true;
        Throws<ApplicationException>(() => app.Amount = 8.25);
        Assert(!host.IsAttached && app.Adjustment.Value == 8.25, "Native range update failure retains the committed value.");
        app.Amount = 2.25;
        Assert(app.Adjustment.Value == 2.25, "Previously cached amount is not skipped during recovery.");
        var recovered = new ChoiceRangeBackend();
        host.Attach(recovered);
        Assert(recovered.Find("amount-range").NativeValue == 2.25, "Recovery reconstructs the actual committed range value.");
    }

    private static void ChoiceRangeCorpusChecks()
    {
        using var host = new Host(new Dispatcher());
        _ = new ChoiceRangeWorkbench(host);
        var backend = new ChoiceRangeBackend();
        host.Attach(backend);
        var peers = backend.Peers.ToArray();
        var driver = new ChoiceRangeDriver(backend);
        int count = ChoiceRangeScenarioRunner.Run(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "ChoiceRangeScenarios.json")), driver);
        assertions += count;
        Assert(count > 50 && peers.SequenceEqual(backend.Peers), "The shared choice/range corpus retains native peers across all literal scenarios.");
        Console.WriteLine($"Choice/range corpus: {count} literal native-state expectations passed.");
        const string prefix = """{"version":1,"scenarios":[{"name":"invalid","steps":[{"action":"click","id":"choice-range-reset"},""";
        foreach (string step in new[]
        {
            """{"action":"select","id":"delivery-choice","selected":9007199254740993}""",
            """{"action":"select","id":"delivery-choice","selected":"01"}""",
            """{"action":"select","id":"delivery-choice","selected":"0"}""",
            """{"action":"select","id":"delivery-choice","selected":"9223372036854775708"}""",
            """{"action":"expect","id":"delivery-choice","selected":null}""",
            """{"action":"expect","id":"delivery-choice","items":["17","1"]}""",
            """{"action":"expect","id":"delivery-choice","itemEnabled":[true,true,true]}""",
            """{"action":"expect","id":"amount-range","minimum":1}""",
            """{"action":"expect","id":"amount-range","value":2}""",
            """{"action":"expect","id":"amount-range"}""",
            """{"action":"range-key","id":"amount-range","key":"unknown"}""",
            """{"action":"change","id":"choice-range-draft","value":null}"""
        })
        {
            var error = Throws<InvalidOperationException>(() => ChoiceRangeScenarioRunner.Run(prefix + step + "]}]}", driver));
            Assert(error.InnerException is not null && error.Message.StartsWith("Choice/range scenario 'invalid', step 2:", StringComparison.Ordinal), "Corpus failures identify the exact scenario and action.");
        }
        Throws<InvalidOperationException>(() => ChoiceRangeScenarioRunner.Run("""{"version":2,"scenarios":[]}""", driver));
        Throws<InvalidOperationException>(() => ChoiceRangeScenarioRunner.Run("""{"version":1,"scenarios":[]}""", driver));
        Throws<InvalidOperationException>(() => ChoiceRangeScenarioRunner.Run("""{"version":1,"scenarios":[{"name":"bad","steps":[{"action":"click","id":"choice-reverse"}]}]}""", driver));
    }

    private sealed class ChoiceRangeDriver(ChoiceRangeBackend backend) : IChoiceRangeScenarioDriver
    {
        public bool Enabled(string id)
        {
            for (var peer = backend.Find(id); peer is not null; peer = peer.Parent)
                if (!peer.NativeEnabled) return false;
            return true;
        }
        public void Click(string id)
        {
            if (!Enabled(id)) return;
            var peer = backend.Find(id);
            if (peer.Kind == ElementKind.Toggle)
            {
                peer.NativeChecked = !peer.NativeChecked;
                ((IValueControlEvents)peer.Events).ToggleChanged(peer.NativeChecked);
            }
            else peer.Events.Click();
        }
        public void Change(string id, string value)
        {
            if (!Enabled(id)) return;
            var peer = backend.Find(id);
            peer.NativeText = value;
            peer.Events.Change(value);
        }
        public void Select(string id, ulong selected)
        {
            if (!Enabled(id)) return;
            var peer = backend.Find(id);
            if (peer.NativeChoices.Any(item => item.Id == selected && item.Enabled)) peer.Select(selected);
        }
        public void PreviewRange(string id, double fraction)
        {
            if (!Enabled(id)) return;
            var peer = backend.Find(id);
            peer.Preview(RangeMath.Snap(peer.NativeRange, fraction));
        }
        public void CommitRange(string id)
        {
            if (!Enabled(id)) return;
            var peer = backend.Find(id);
            peer.Commit(peer.NativePreview);
        }
        public void CancelRange(string id) => backend.Find(id).Cancel();
        public void RangeKey(string id, string key)
        {
            if (!Enabled(id)) return;
            var peer = backend.Find(id);
            peer.Commit(RangeMath.Step(peer.NativeRange, peer.NativeValue, Enum.Parse<Xui.Experimental.Portable.RangeKey>(key)));
        }
        public string Text(string id) => backend.Find(id).NativeText;
        public ulong? Selected(string id) => backend.Find(id).NativeSelected;
        public IReadOnlyList<ChoiceSnapshotItem> Items(string id) =>
            backend.Find(id).NativeChoices.Select(item => new ChoiceSnapshotItem(item.Id, item.Text, item.Enabled)).ToArray();
        public RangeSnapshot Range(string id)
        {
            var peer = backend.Find(id);
            return new(peer.NativeRange.Minimum, peer.NativeRange.Maximum, peer.NativeValue);
        }
    }

    private sealed class ChoiceRangeBackend : IBackend
    {
        public List<ChoiceRangePeer> Peers { get; } = [];
        public ChoiceRangePeer Find(string id) => Peers.Single(peer => peer.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new ChoiceRangePeer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { }
    }

    private sealed class ChoiceRangePeer : ISingleChoiceElementPeer, IRangeElementPeer
    {
        private readonly Element element;
        public string Id { get; }
        public IControlEvents Events { get; }
        public List<ElementProperty> Updates { get; } = [];
        public bool Reject { get; set; }
        public bool FailUpdate { get; set; }
        public Action? Validating { get; set; }
        public ulong? NativeSelected { get; private set; }
        public Choice[] NativeChoices { get; private set; } = [];
        public NumericRange NativeRange { get; private set; }
        public double NativeValue { get; private set; }
        public double NativePreview { get; private set; }
        public string NativeText { get; set; } = "";
        public bool NativeEnabled { get; private set; } = true;
        public bool NativeChecked { get; set; }
        public ChoiceRangePeer? Parent { get; private set; }
        public ElementKind Kind => element.Kind;
        public ChoiceRangePeer(Element element, IControlEvents events)
        {
            this.element = element; Events = events; Id = element is Control control ? control.AutomationId : "";
            Capture();
        }
        private void Capture()
        {
            if (element is Control control)
            {
                NativeText = element is TextInput input ? input.Text : control.Name;
                NativeEnabled = control.Enabled;
            }
            if (element is Toggle toggle) NativeChecked = toggle.Checked;
            if (element is SingleChoice choice) { NativeChoices = choice.Items.ToArray(); NativeSelected = choice.Selected; }
            if (element is RangeInput range) { NativeRange = range.Range; NativeValue = range.Value; NativePreview = range.PreviewValue; }
        }
        public void Select(ulong id)
        {
            if (((ISelectionControlEvents)Events).SelectionChanged(id)) NativeSelected = id;
        }
        public void Preview(double value)
        {
            if (((IRangeControlEvents)Events).RangePreviewed(value)) NativePreview = value;
        }
        public void Commit(double value)
        {
            if (((IRangeControlEvents)Events).RangeChanged(value)) { NativeValue = value; NativePreview = value; }
        }
        public void Cancel()
        {
            NativePreview = NativeValue;
            ((IRangeControlEvents)Events).RangeCanceled(NativeValue);
        }
        public void ValidateChoices(IReadOnlyList<Choice> items, ulong? selected)
        {
            Validating?.Invoke();
            if (Reject) throw new NotSupportedException("Native choice snapshot cannot be represented.");
        }
        public void ValidateRange(NumericRange range, double value)
        {
            Validating?.Invoke();
            if (Reject) throw new NotSupportedException("Native range cannot be represented.");
        }
        public void AddChild(IElementPeer child) => ((ChoiceRangePeer)child).Parent = this;
        public void Update(ElementProperty property)
        {
            if (FailUpdate) throw new ApplicationException("native selection/range update");
            Updates.Add(property);
            Capture();
            if (element is SingleChoice && NativeSelected is ulong id)
                Assert(!((ISelectionControlEvents)Events).SelectionChanged(id), "Programmatic choice echoes are suppressed.");
            if (element is RangeInput)
            {
                Assert(!((IRangeControlEvents)Events).RangeChanged(NativeValue), "Programmatic range change echoes are suppressed.");
                Assert(!((IRangeControlEvents)Events).RangeCanceled(NativeValue), "Programmatic range cancellation echoes are suppressed.");
            }
        }
        public void Dispose() { }
    }
}

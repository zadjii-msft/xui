using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void PrimitiveControlChecks()
    {
        using var host = new Host(new Dispatcher());
        var settings = new SettingsShowcase(host);
        var backend = new SettingsBackend();
        host.Attach(backend);
        var driver = new SettingsDriver(backend);
        var toggle = settings.NotificationToggle;
        var check = settings.SyncCheckBox;
        var progress = settings.WorkProgress;
        var togglePeer = backend.Find("notifications");
        var checkPeer = backend.Find("sync-policy");
        var progressPeer = backend.Find("work-progress");
        int toggleChanges = 0, checkChanges = 0, textChanges = 0;
        toggle.Changed += value => { Assert(toggle.Checked == value, "Native boolean is synchronized before authored handlers."); toggleChanges++; };
        check.Changed += value => { Assert(check.State == value, "Native check state is synchronized before authored handlers."); checkChanges++; };
        settings.DraftInput.Changed += _ => textChanges++;

        driver.Click("notifications");
        driver.Click("sync-policy");
        Assert(toggleChanges == 1 && checkChanges == 1 && !toggle.Checked && check.State == CheckState.Unchecked, "Native primitive callbacks reach compiled authored code once.");
        Assert(togglePeer.Updates.Count == 0 && checkPeer.Updates.Count == 0, "Native value events do not echo setters into their widgets.");
        Assert(togglePeer.Values.ToggleChanged(false) && checkPeer.Values.CheckChanged(CheckState.Unchecked), "Duplicate native events are accepted without another callback.");
        Assert(toggleChanges == 1 && checkChanges == 1, "Unchanged values are silent.");
        toggle.Checked = true;
        check.State = CheckState.Indeterminate;
        check.ThreeState = false;
        Assert(toggleChanges == 1 && checkChanges == 1, "Checked, mixed-state, and three-state programmatic setters stay silent.");
        Assert(check.State == CheckState.Indeterminate && checkPeer.NativeCheck == CheckState.Indeterminate, "Changing to binary mode preserves an existing mixed state.");
        driver.Click("sync-policy");
        Assert(check.State == CheckState.Unchecked && checkChanges == 2, "The binary activation after mixed selects unchecked.");
        Throws<ArgumentOutOfRangeException>(() => check.State = (CheckState)99);
        Throws<ArgumentOutOfRangeException>(() => checkPeer.Values.CheckChanged((CheckState)99));
        Assert(check.State == CheckState.Unchecked, "Invalid native and authored check states preserve the model.");
        Throws<InvalidOperationException>(() => togglePeer.Values.CheckChanged(CheckState.Checked));
        Throws<InvalidOperationException>(() => checkPeer.Values.ToggleChanged(true));
        Throws<InvalidOperationException>(() => togglePeer.Events.Click());
        Throws<InvalidOperationException>(() => checkPeer.Events.Submit());
        Throws<InvalidOperationException>(() => progressPeer.Events.Click());
        Throws<InvalidOperationException>(() => progressPeer.Values.ToggleChanged(true));
        Throws<InvalidOperationException>(() => progressPeer.Events.Change("not input"));
        Assert(progress.Range == new NumericRange(0, 100) && progress.Value == 25 && progress.State == ProgressState.Determinate, "Progress defaults and initial generated value.");
        progressPeer.Updates.Clear();
        progress.Range = new NumericRange(10, 20, 0.5, 2);
        Assert(progress.Value == 20 && progressPeer.NativeProgress == new SettingsProgressSnapshot(10, 20, 20, false), "Range publishes bounds and clamped value together.");
        Assert(progressPeer.Updates.SequenceEqual([ElementProperty.Range]), "Clamped range is one coherent native update.");
        progress.Range = progress.Range;
        Assert(progressPeer.Updates.Count == 1, "An equal range/value tuple does not update the peer.");
        progress.Range = new NumericRange(30, 40);
        Assert(progress.Value == 30, "Raising minimum clamps the prior value.");
        progress.Value = 32.5;
        Assert(progressPeer.NativeProgress.Value == 32.5, "Fractional progress preserves authored units.");
        int updates = progressPeer.Updates.Count;
        foreach (var invalid in new[]
        {
            new NumericRange(0, 0), new NumericRange(10, 0), new NumericRange(double.NaN, 10),
            new NumericRange(0, double.PositiveInfinity), new NumericRange(-double.MaxValue, double.MaxValue),
            new NumericRange(0, 1, 0), new NumericRange(0, 1, -1), new NumericRange(0, 1, double.NaN),
            new NumericRange(0, 1, 1, 0), new NumericRange(0, 1, 1, double.PositiveInfinity)
        })
            Throws<ArgumentOutOfRangeException>(() => progress.Range = invalid);
        foreach (double invalid in new[] { double.NaN, double.NegativeInfinity, double.PositiveInfinity, 29.9, 40.1 })
            Throws<ArgumentOutOfRangeException>(() => progress.Value = invalid);
        Throws<ArgumentOutOfRangeException>(() => progress.State = (ProgressState)2);
        Assert(progress.Range == new NumericRange(30, 40) && progress.Value == 32.5 && progressPeer.Updates.Count == updates, "Invalid range/value/mode changes are rejected before native updates.");
        progress.State = ProgressState.Indeterminate;
        Assert(progressPeer.NativeProgress == new SettingsProgressSnapshot(30, 40, null, true), "Indeterminate progress omits determinate current-value semantics.");
        progress.State = ProgressState.Determinate;
        Assert(progressPeer.NativeProgress.Value == 32.5, "Returning to determinate retains the logical value.");
        progress.Range = new NumericRange(0, 100);
        progress.Value = 25;
        driver.Change("settings-draft", "unchanged \u674e");
        var input = backend.Find("settings-draft");
        int inputWrites = input.Updates.Count;
        settings.Notifications = false;
        settings.Sync = CheckState.Checked;
        settings.Busy = true;
        Assert(ReferenceEquals(input, backend.Find("settings-draft")) && input.NativeText == "unchanged \u674e" &&
            input.Updates.Count == inputWrites && textChanges == 1, "Primitive updates retain editor identity and never rewrite unrelated draft/composition.");
        settings.Content.Enabled = false;
        Assert(!togglePeer.Values.ToggleChanged(true) && !checkPeer.Values.CheckChanged(CheckState.Indeterminate), "Disabled ancestor rejects new value events.");
        settings.Content.Enabled = true;
        check.Visible = false;
        Assert(!checkPeer.Values.CheckChanged(CheckState.Indeterminate), "Hidden check box events are inert.");
        check.Visible = true;
        progress.Enabled = false;
        Assert(!progressPeer.Events.Click(), "A disabled progress control rejects even a malformed input attempt.");
        progress.Enabled = true;
        int previousToggleChanges = toggleChanges, previousCheckChanges = checkChanges;
        backend.Find("reset-settings").Events.Click();
        Assert(toggleChanges == previousToggleChanges && checkChanges == previousCheckChanges && textChanges == 1, "Reset programmatic updates do not produce any primitive or editor callbacks.");
        Assert(settings.Changes == 0 && toggle.Checked && check.State == CheckState.Indeterminate, "Silent reset restores the generated settings state.");
        Task.Run(() => Throws<InvalidOperationException>(() => togglePeer.Values.ToggleChanged(false))).GetAwaiter().GetResult();
        check.Changed += _ => throw new ApplicationException("authored check error");
        Throws<ApplicationException>(() => checkPeer.Values.CheckChanged(CheckState.Checked));
        host.Detach();
        Assert(!togglePeer.Values.ToggleChanged(false) && !checkPeer.Values.CheckChanged(CheckState.Checked), "Detached primitive sinks remain stale.");
        var next = new SettingsBackend();
        host.Attach(next);
        Assert(!togglePeer.Values.ToggleChanged(true), "Old value sinks do not reactivate on reattachment.");
        host.Dispose();
        Assert(next.Peers.All(p => p.Disposed) && !next.Find("sync-policy").Values.CheckChanged(CheckState.Unchecked), "Primitive listeners are terminal after disposal.");
        Throws<ObjectDisposedException>(() => toggle.Checked = false);
        Throws<ObjectDisposedException>(() => check.ThreeState = true);
        Throws<ObjectDisposedException>(() => progress.Value = 25);
        using var failedHost = new Host(new Dispatcher());
        var failedSettings = new SettingsShowcase(failedHost);
        var failedBackend = new SettingsBackend();
        failedHost.Attach(failedBackend);
        failedBackend.Find("work-progress").FailUpdate = true;
        Throws<ApplicationException>(() => failedSettings.WorkProgress.Range = new NumericRange(10, 20));
        Assert(!failedHost.IsAttached && failedBackend.Peers.All(p => p.Disposed), "Native progress update failure detaches the whole attachment.");
        Assert(failedSettings.WorkProgress.Range == new NumericRange(10, 20) && failedSettings.WorkProgress.Value == 20, "Failed native range update retains the coherent committed range and clamped value.");
        var recovery = new SettingsBackend();
        failedHost.Attach(recovery);
        Assert(recovery.Find("work-progress").NativeProgress == new SettingsProgressSnapshot(10, 20, 20, false), "Reattachment reads the committed progress snapshot.");
    }

    private static void SettingsScenarioChecks()
    {
        using var host = new Host(new Dispatcher());
        _ = new SettingsShowcase(host);
        var backend = new SettingsBackend();
        host.Attach(backend);
        var original = backend.Peers.ToArray();
        var driver = new SettingsDriver(backend);
        string corpus = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "SettingsScenarios.json"));
        int count = SettingsScenarioRunner.Run(corpus, driver);
        Assert(count > 60 && original.SequenceEqual(backend.Peers), "Shared settings scenarios preserve the complete native peer tree.");
        assertions += count;
        Console.WriteLine($"Settings corpus: {count} literal expectations passed.");
        const string start = """{"version":1,"scenarios":[{"name":"invalid","steps":[{"action":"click","id":"reset-settings"},""";
        foreach (string invalid in new[]
        {
            """{"action":"expect","id":"notifications","checked":false}""",
            """{"action":"expect","id":"sync-policy","checkState":"Checked"}""",
            """{"action":"expect","id":"work-progress","value":null}""",
            """{"action":"expect","id":"work-progress","minimum":1}""",
            """{"action":"expect","id":"work-progress","maximum":99}""",
            """{"action":"expect","id":"work-progress","indeterminate":true}""",
            """{"action":"expect","id":"work-progress","ignored":1}""",
            """{"action":"expect","id":"work-progress"}""",
            """{"action":"change","id":"settings-draft","value":null}""",
            """{"action":"unknown","id":"notifications"}"""
        })
        {
            var error = Throws<InvalidOperationException>(() => SettingsScenarioRunner.Run(start + invalid + "]}]}", driver));
            Assert(error.InnerException is not null && error.Message.StartsWith("Settings scenario 'invalid', step 2:", StringComparison.Ordinal), "Settings runner reports exact step failures.");
        }
        Throws<InvalidOperationException>(() => SettingsScenarioRunner.Run("""{"version":2,"scenarios":[]}""", driver));
        Throws<InvalidOperationException>(() => SettingsScenarioRunner.Run("""{"version":1,"scenarios":[]}""", driver));
    }

    private sealed class SettingsDriver(SettingsBackend backend) : ISettingsScenarioDriver
    {
        public void Click(string id)
        {
            var peer = backend.Find(id);
            if (!Enabled(id) || !Visible(id)) return;
            switch (peer.Element.Kind)
            {
                case ElementKind.Button: peer.Events.Click(); break;
                case ElementKind.Toggle:
                    peer.NativeChecked = !peer.NativeChecked;
                    peer.Values.ToggleChanged(peer.NativeChecked);
                    break;
                case ElementKind.CheckBox:
                    peer.NativeCheck = peer.NativeCheck switch
                    {
                        Xui.Experimental.Portable.CheckState.Unchecked => Xui.Experimental.Portable.CheckState.Checked,
                        Xui.Experimental.Portable.CheckState.Checked when peer.NativeThreeState => Xui.Experimental.Portable.CheckState.Indeterminate,
                        _ => Xui.Experimental.Portable.CheckState.Unchecked
                    };
                    peer.Values.CheckChanged(peer.NativeCheck);
                    break;
                default: throw new InvalidOperationException("The native control has no activation.");
            }
        }
        public void Change(string id, string value)
        {
            var peer = backend.Find(id);
            peer.NativeText = value;
            peer.Events.Change(value);
        }
        public string Text(string id) => backend.Find(id).NativeText;
        public bool Enabled(string id)
        {
            for (var peer = backend.Find(id); peer is not null; peer = peer.Parent)
                if (!peer.NativeEnabled) return false;
            return true;
        }
        public bool Visible(string id)
        {
            for (var peer = backend.Find(id); peer is not null; peer = peer.Parent)
                if (!peer.NativeVisible) return false;
            return true;
        }
        public bool Checked(string id) => backend.Find(id).NativeChecked;
        public string CheckState(string id) => backend.Find(id).NativeCheck.ToString();
        public SettingsProgressSnapshot Progress(string id) => backend.Find(id).NativeProgress;
    }

    private sealed class SettingsBackend : IBackend
    {
        public List<SettingsPeer> Peers { get; } = [];
        public SettingsPeer Find(string id) => Peers.Single(p => p.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new SettingsPeer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { }
    }

    private sealed class SettingsPeer : IElementPeer
    {
        public Element Element { get; }
        public IControlEvents Events { get; }
        public IValueControlEvents Values => (IValueControlEvents)Events;
        public string Id { get; private set; } = "";
        public bool Disposed { get; private set; }
        public bool FailUpdate { get; set; }
        public SettingsPeer? Parent { get; private set; }
        public List<ElementProperty> Updates { get; } = [];
        public string NativeText = "";
        public bool NativeEnabled = true, NativeVisible = true, NativeChecked, NativeThreeState;
        public CheckState NativeCheck;
        public SettingsProgressSnapshot NativeProgress;
        public SettingsPeer(Element element, IControlEvents events)
        {
            Element = element;
            Events = events;
            Capture();
        }
        public void AddChild(IElementPeer child) => ((SettingsPeer)child).Parent = this;
        private void Capture()
        {
            if (Element is Control control)
            {
                Id = control.AutomationId;
                NativeEnabled = control.Enabled;
                NativeVisible = control.Visible;
                NativeText = control is TextInput input ? input.Text : control.Name;
            }
            if (Element is Toggle toggle) NativeChecked = toggle.Checked;
            if (Element is CheckBox check) { NativeCheck = check.State; NativeThreeState = check.ThreeState; }
            if (Element is Xui.Experimental.Portable.Progress progress)
                NativeProgress = new(progress.Range.Minimum, progress.Range.Maximum,
                    progress.State == ProgressState.Indeterminate ? null : progress.Value, progress.State == ProgressState.Indeterminate);
        }
        public void Update(ElementProperty property)
        {
            if (FailUpdate) throw new ApplicationException("native value update");
            Updates.Add(property);
            Capture();
            if (Element is Toggle && property == ElementProperty.Checked)
                Assert(!Values.ToggleChanged(NativeChecked), "Programmatic binary echoes are blocked.");
            if (Element is CheckBox && property is ElementProperty.CheckState or ElementProperty.ThreeState)
                Assert(!Values.CheckChanged(NativeCheck), "Programmatic mixed-state echoes are blocked.");
            if (Element is TextInput && property == ElementProperty.Text)
                Assert(!Events.Change(NativeText), "Programmatic draft echoes are blocked.");
        }
        public void Dispose() { Disposed = true; }
    }
}

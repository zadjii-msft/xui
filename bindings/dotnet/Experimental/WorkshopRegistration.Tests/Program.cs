using System.Globalization;
using System.Text.Json;
using PortableDemo;
using Xui.Experimental.Portable;

internal static class Program
{
    private static int assertions;
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { assertions++; return; }
        throw new InvalidOperationException("Expected " + typeof(T).Name);
    }
    private static void Main()
    {
        ModelChecks();
        GeneratedChecks();
        Console.WriteLine($"Workshop registration: {assertions} assertions passed.");
    }
    private static void ModelChecks()
    {
        var state = new WorkshopRegistrationState();
        Assert(!state.CanReview && state.SeatCount == 1 && state.Total == 20m && !state.Consent && !state.InPerson, "Initial local workshop draft.");
        var valid = state with { Name = "  Ada  ", Email = " ada@example.test ", Seats = "3", Goals = "Native input\r\nForms\rAccessibility", Consent = true };
        Assert(valid.CanReview && valid.Goals == "Native input\nForms\nAccessibility" && valid.Total == 60m, "Multiline LF normalization and exact quote.");
        Assert((valid with { InPerson = true }).Total == 135m && (valid with { Seats = "6", InPerson = true }).Total == 270m, "Literal in-person and maximum totals.");
        Assert(valid.Review().Reviewing && !valid.Reviewing && state.Name == "", "Review preserves immutable drafts.");
        foreach (string invalid in new[] { "", " ", "0", "7", "2e1", "-1", "+2", "1.5", "999999999999999999999999" })
        {
            var bad = valid with { Seats = invalid };
            Assert(!bad.CanReview && bad.Total is null && bad.TotalText == "Local estimate: --" && bad.Seats == invalid, "Number purpose is not coercion or validation.");
        }
        foreach (string invalid in new[] { "", "ada", "ada@", "a@local", "a@@example.test", "a@b..test", ".a@example.test" })
            Assert(!(valid with { Email = invalid }).CanReview, "Required email follows existing order validation.");
        Assert((valid with { Goals = "\n \n" }).Validation == "Describe at least one learning goal.", "Whitespace-only multiline goals are invalid.");
        Assert((valid with { Seats = " 02 " }).Total == 40m, "Valid number text remains as entered.");
        Assert((valid with { Goals = new string('a', 500) }).Goals.Length == 500, "Exact 500 UTF-16 boundary accepted.");
        Assert((valid with { Goals = string.Concat(Enumerable.Repeat("\r\n", 500)) }).Goals.Length == 500, "LF normalization precedes the text limit.");
        Assert((valid with { Goals = new string('a', 498) + "\U0001F642" }).Goals.Length == 500, "Supplementary scalar consumes two UTF-16 units.");
        Throws<ArgumentException>(() => _ = valid with { Goals = new string('a', 501) });
        Throws<ArgumentException>(() => _ = valid with { Goals = "\ud800" });
        Throws<ArgumentException>(() => _ = valid with { Goals = "\udc00" });
        Throws<ArgumentException>(() => _ = valid with { Goals = "a\0b" });
        Throws<ArgumentNullException>(() => _ = valid with { Goals = null! });
        Assert(!JsonSerializer.IsReflectionEnabledByDefault, "Workshop uses generated JSON metadata.");
        var draft = valid with { Email = "invalid", Seats = "", Reviewing = false };
        string json = WorkshopRegistrationCodec.Serialize(draft);
        Assert(WorkshopRegistrationCodec.Restore(json) == draft && !json.Contains("Total") && !json.Contains("Validation"), "Restorable invalid form draft contains data only.");
        Throws<JsonException>(() => WorkshopRegistrationCodec.Restore("null"));
        Throws<JsonException>(() => WorkshopRegistrationCodec.Restore("{}"));
        Throws<JsonException>(() => WorkshopRegistrationCodec.Restore(json.Replace("\"Name\":", "\"unexpected\":1,\"Name\":")));
        var culture = CultureInfo.CurrentCulture;
        try { CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR"); Assert(valid.TotalText == "Local estimate: $60.00", "Invariant USD."); }
        finally { CultureInfo.CurrentCulture = culture; }
    }
    private static void GeneratedChecks()
    {
        using var host = new Host(new Dispatcher());
        var app = new WorkshopRegistration(host);
        var backend = new Backend();
        host.Attach(backend);
        Assert(app.Root.Children.Count == 2 && app.Root.Children[1] is ScrollView { Flex: 1 }, "Only title remains outside scroll.");
        int count = WorkshopScenarioRunner.Run(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "WorkshopScenarios.json")), new Driver(backend));
        assertions += count;
        Assert(count >= 40, "Literal native form corpus.");
        Console.WriteLine($"Workshop corpus: {count} literal expectations.");
        Assert(app.State.Name == "Ada Lovelace" && app.State.Total == 40m && !app.State.Reviewing, "Corpus ends at deterministic editable seed.");
        var peers = backend.Peers.ToArray();
        var inputs = new[] { app.NameInput, app.EmailInput, app.SeatsInput };
        foreach (var input in inputs)
            Assert(input.CaptionVisible && input.FixedSize is null && input.PreferredSize is null && input.Help.Length > 0, "Single-line captions remain naturally sized.");
        Assert(app.EmailInput.Purpose == InputPurpose.Email && app.SeatsInput.Purpose == InputPurpose.Number &&
            app.GoalsInput.MaximumLength == 500, "Generated purpose and multiline contracts.");
        var controls = backend.Peers.Select(peer => peer.Element).OfType<Control>().ToArray();
        Assert(controls.All(control => control.AutomationId.Length > 0) &&
            controls.Select(control => control.AutomationId).Distinct().Count() == controls.Length,
            "Every form control has a stable unique host-scoped automation identity.");
        foreach (var peer in backend.Peers) peer.Updates.Clear();
        var goals = backend.Find("workshop-goals");
        int edits = 0;
        app.GoalsInput.Changed += _ => edits++;
        Assert(goals.Events.Change("First\r\nSecond\rThird") && app.State.Goals == "First\nSecond\nThird", "Native multiline edit normalizes LF before app state.");
        Assert(goals.Updates.Count == 0 && edits == 1, "Native multiline does not echo programmatic text.");
        Assert(goals.Events.Change("First\nSecond\nThird") && edits == 1, "Equivalent normalized edit is silent.");
        var driver = new Driver(backend);
        driver.Click("workshop-in-person");
        driver.Click("workshop-review");
        Assert(app.GoalsInput.ReadOnly && !goals.Events.Change("read-only edit") && edits == 1, "Review denies native multiline edits.");
        Throws<InvalidOperationException>(() => goals.Events.Submit());
        Assert(goals.Updates.SequenceEqual([ElementProperty.ReadOnly]), "Unrelated option/review changes do not reset multiline selection by rewriting text.");
        driver.Click("workshop-edit");
        string before = app.State.Goals;
        Throws<ArgumentException>(() => goals.Events.Change(new string('x', 501)));
        Throws<ArgumentException>(() => goals.Events.Change("\ud800"));
        Assert(app.State.Goals == before && app.GoalsInput.Text == before, "Invalid native payload leaves existing draft intact.");
        Assert(peers.SequenceEqual(backend.Peers), "All form edits retain native peer identity.");
        string saved = WorkshopRegistrationCodec.Serialize(app.State);
        driver.Click("workshop-reset");
        app.State = WorkshopRegistrationCodec.Restore(saved);
        Assert(edits == 1 && app.State.Goals == before, "Programmatic reset/restoration is silent.");
        app.AttendanceToggle.Enabled = false;
        Assert(!((IValueControlEvents)backend.Find("workshop-in-person").Events).ToggleChanged(false),
            "Disabled native attendance action is rejected.");
        app.AttendanceToggle.Enabled = true;
        var invalidFixture = """{"version":1,"scenarios":[{"name":"bad","steps":[{"action":"click","id":"workshop-reset"},{"action":"expect","id":"workshop-total","text":"wrong"}]}]}""";
        Throws<InvalidOperationException>(() => WorkshopScenarioRunner.Run(invalidFixture, driver));
        Throws<InvalidOperationException>(() => WorkshopScenarioRunner.Run(invalidFixture.Replace("\"version\":1", "\"version\":2"), driver));
        Throws<InvalidOperationException>(() => WorkshopScenarioRunner.Run(invalidFixture.Replace("\"text\":\"wrong\"", "\"unknown\":1"), driver));
        Throws<InvalidOperationException>(() => WorkshopScenarioRunner.Run(invalidFixture.Replace("\"text\":\"wrong\"", "\"text\":\"wrong\",\"text\":\"wrong\""), driver));
        Throws<InvalidOperationException>(() => WorkshopScenarioRunner.Run("""{"version":1,"scenarios":[]}""", driver));
        app.State = WorkshopRegistrationCodec.Restore(saved);
        host.Detach();
        Assert(backend.Disposed && backend.Peers.All(p => p.Disposed) && !goals.Events.Change("stale"), "Detach releases form peers and invalidates events.");
        var next = new Backend();
        host.Attach(next);
        Assert(((MultilineText)next.Find("workshop-goals").Element).Text == before, "Reattachment restores the multiline document.");
        host.Dispose();
        Assert(next.Peers.All(p => p.Disposed), "Terminal cleanup owns every form peer.");
        Throws<ObjectDisposedException>(() => app.State = new());
        using var unsupportedHost = new Host(new Dispatcher());
        _ = new WorkshopRegistration(unsupportedHost);
        Throws<NotSupportedException>(() => unsupportedHost.Attach(new UnsupportedBackend()));
    }
    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new NotSupportedException("Workshop has no asynchronous work.");
    }
    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public Peer Find(string id) => Peers.Single(peer => peer.Element is Control control && control.AutomationId == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(element, events); Peers.Add(peer); return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() => Disposed = true;
    }
    private sealed class Peer(Element element, IControlEvents events) : IInputPurposeElementPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public List<ElementProperty> Updates { get; } = [];
        public bool Disposed { get; private set; }
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property)
        {
            Updates.Add(property);
            if (property == ElementProperty.Text)
            {
                string text = Element switch { TextInput input => input.Text, MultilineText input => input.Text, _ => throw new InvalidOperationException() };
                Assert(!Events.Change(text), "Programmatic text suppresses event echoes.");
            }
        }
        public void Dispose() => Disposed = true;
    }
    private sealed class UnsupportedBackend : IBackend
    {
        public IElementPeer Create(Element element, IControlEvents events) => new UnsupportedPeer();
        public void Mount(IElementPeer root) => throw new InvalidOperationException("Missing input-purpose support must reject before mount.");
        public void Dispose() { }
    }
    private sealed class UnsupportedPeer : IElementPeer
    {
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property) { }
        public void Dispose() { }
    }
    private sealed class Driver(Backend backend) : IWorkshopScenarioDriver
    {
        public void Click(string id)
        {
            var peer = backend.Find(id);
            bool accepted = peer.Element switch
            {
                Toggle toggle => ((IValueControlEvents)peer.Events).ToggleChanged(!toggle.Checked),
                CheckBox check => ((IValueControlEvents)peer.Events).CheckChanged(check.State == CheckState.Checked ? CheckState.Unchecked : CheckState.Checked),
                _ => peer.Events.Click()
            };
            Assert(accepted, "Native activation accepted: " + id);
        }
        public void Change(string id, string value) => Assert(backend.Find(id).Events.Change(value), "Native text accepted: " + id);
        public string Text(string id) => backend.Find(id).Element switch
        {
            TextInput input => input.Text,
            MultilineText input => input.Text,
            Control control => control.Name,
            _ => throw new InvalidOperationException("Not a text control.")
        };
        public bool Enabled(string id) => ((Control)backend.Find(id).Element).Enabled;
        public bool Visible(string id) => ((Control)backend.Find(id).Element).Visible;
        public bool Checked(string id) => backend.Find(id).Element switch
        {
            Toggle toggle => toggle.Checked,
            CheckBox check => check.State == CheckState.Checked,
            _ => throw new InvalidOperationException("Not a checked control.")
        };
        public bool ReadOnly(string id) => ((MultilineText)backend.Find(id).Element).ReadOnly;
        public int MaximumLength(string id) => ((MultilineText)backend.Find(id).Element).MaximumLength;
        public string Purpose(string id) => ((TextInput)backend.Find(id).Element).Purpose.ToString();
    }
}

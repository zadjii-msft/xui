using PortableDemo;
using PortableMutation;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void FormsChecks()
    {
        Assert(FormValues.NormalizeMultiline("  a\r\nb\rc\n  ", 10) == "  a\nb\nc\n  ", "Multiline canonicalization preserves all non-newline characters.");
        Assert(FormValues.NormalizeMultiline("a\r\nb", 3) == "a\nb", "Normalize CRLF before counting the UTF-16 limit.");
        Assert(FormValues.NormalizeMultiline("\ud83d\ude80", 2).Length == 2, "Valid UTF-16 pairs remain intact.");
        Throws<ArgumentException>(() => FormValues.NormalizeMultiline("\ud800"));
        Throws<ArgumentException>(() => FormValues.NormalizeMultiline("\udc00"));
        Throws<ArgumentException>(() => FormValues.NormalizeMultiline("nul\0"));
        Throws<ArgumentException>(() => FormValues.NormalizeMultiline("abcd", 3));
        foreach (string invalid in new[] { "\0", "\r", "\n", "\ud800", "\udc00" })
            Throws<ArgumentException>(() => FormValues.ValidatePassword(invalid));
        FormValues.ValidatePassword("\ud83d\ude80", 2);
        Throws<ArgumentException>(() => FormValues.ValidatePassword("\ud83d\ude80", 1));
        Throws<ArgumentOutOfRangeException>(() => FormValues.ValidatePassword("", 4097));
        Throws<ArgumentOutOfRangeException>(() => FormValues.NormalizeMultiline("", 1048577));

        using var host = new Host(new Dispatcher());
        var app = new FormsWorkbench(host);
        Throws<InvalidOperationException>(() => _ = app.Secret.Length);
        Throws<InvalidOperationException>(() => app.Secret.SetPassword("unattached"));
        var unsupported = new Backend();
        Throws<NotSupportedException>(() => host.Attach(unsupported));
        Assert(unsupported.Disposed && !host.IsAttached, "New purposes and secret peers fail explicitly on old backends.");
        var backend = new FormsBackend();
        host.Attach(backend);
        var inputPeers = backend.Peers.ToArray();
        int literalChecks = FormsScenarioRunner.Run(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "FormsScenarios.json")), new FormsDriver(backend));
        assertions += literalChecks;
        Console.WriteLine($"Forms corpus: {literalChecks} literal metadata/text expectations passed.");
        foreach (var peer in backend.Peers) peer.Updates.Clear();
        var email = backend.Find("forms-email");
        var number = backend.Find("forms-number");
        var document = backend.Find("forms-notes");
        var secret = backend.Find("forms-password");
        secret.ReportedLength = -1;
        Throws<InvalidOperationException>(() => _ = app.Secret.Length);
        secret.ReportedLength = 33;
        Throws<InvalidOperationException>(() => _ = app.Secret.Length);
        secret.ReportedLength = 32;
        Assert(app.Secret.Length == app.Secret.MaximumLength, "A native password at the exact configured limit remains valid.");
        secret.ReportedLength = null;
        Assert(app.EmailInput.Purpose == InputPurpose.Email && app.NumberInput.Purpose == InputPurpose.Number, "Purpose is immutable constructor metadata.");
        email.Events.Change(" Not an email \u674e ");
        number.Events.Change(" -12,5 xyz ");
        Assert(app.Email == " Not an email \u674e " && app.Number == " -12,5 xyz ", "Purpose never validates, trims, or coerces typed text.");
        document.Events.Change("first\r\nsecond");
        Assert(app.Notes == "first\nsecond" && app.NotesInput.Text == app.Notes, "Native document changes publish canonical LF before authored handlers.");
        Assert(document.Updates.Count == 0, "User document changes do not write back into the editor.");
        app.Locked = true;
        Assert(!document.Events.Change("rejected") && app.Notes == "first\nsecond", "Read-only rejects native edits without losing text.");
        app.Notes = "programmatic\nupdate";
        Assert(app.NotesInput.Text == app.Notes, "Read-only does not prohibit silent programmatic setters.");
        Throws<InvalidOperationException>(() => document.Events.Submit());
        Assert(inputPeers.SequenceEqual(backend.Peers), "Editor kinds and widgets remain retained across unrelated changes.");

        char[] password = ['f', 'i', 'x', 't', 'u', 'r', 'e', '\ud83d', '\ude80'];
        app.Secret.SetPassword(password);
        Assert(app.Secret.Length == 9 && app.Notices == 0, "Programmatic password writes are silent and length counts UTF-16 units.");
        int readLength = 0;
        app.Secret.WithPassword(value => readLength = value.Length);
        Assert(readLength == 9, "Scoped reads receive the actual native value.");
        var saved = secret.Secret;
        app.Secret.SetPassword("next");
        Assert(saved.All(value => value == '\0'), "The peer erases its previous temporary fixture buffer on replacement.");
        secret.NativePassword(password);
        Assert(app.Notices == 1 && app.SecretLength == 9, "Password notices carry no plaintext; handlers explicitly query length.");
        backend.Find("forms-inspect").Events.Click();
        Assert(app.Status == "Inspected 9 UTF-16 units; no value displayed.", "Authored password inspection displays metadata only.");
        Throws<InvalidOperationException>(() => secret.Events.Change("forbidden generic text"));
        Throws<InvalidOperationException>(() => secret.Events.Submit());
        Throws<InvalidOperationException>(() => ((IPasswordControlEvents)email.Events).PasswordChanged());
        Throws<InvalidOperationException>(() => app.Secret.WithPassword(_ => app.Email = "reentrant"));
        Throws<ApplicationException>(() => app.Secret.WithPassword(_ => throw new ApplicationException("receiver")));
        Assert(host.IsAttached && app.Secret.Length == 9, "Receiver errors propagate without mutating or detaching the secret.");
        secret.ReadCalls = 0;
        Throws<InvalidOperationException>(() => app.Secret.WithPassword(_ => { }));
        secret.ReadCalls = 2;
        int received = 0;
        Throws<InvalidOperationException>(() => app.Secret.WithPassword(_ => received++));
        Assert(received == 1, "A broken native reader cannot invoke an authored receiver twice.");
        secret.ReadCalls = 1;
        app.Secret.Visible = false;
        app.Secret.Enabled = false;
        Assert(app.Secret.Length == 9 && secret.ClearCalls == 0, "Hide and disable do not silently clear an attached secret.");
        Assert(!((IPasswordControlEvents)secret.Events).PasswordChanged(), "Hidden/disabled password events are inert.");
        app.Secret.Visible = true;
        app.Secret.Enabled = true;
        backend.Find("forms-reset").Events.Click();
        Assert(app.Notices == 0 && app.Secret.Length == 0 && app.Notes == "" && app.Email == "", "Reset clears through explicit native write without a password notice.");
        app.Secret.SetPassword(password);
        var prior = secret.Secret;
        host.Detach();
        Assert(secret.ClearCalls == 1 && prior.All(value => value == '\0') &&
            backend.Trace.IndexOf("clear") < backend.Trace.IndexOf("unmount"), "Detach clears every secret before native unmount.");
        Throws<InvalidOperationException>(() => _ = app.Secret.Length);
        Throws<InvalidOperationException>(() => app.Secret.WithPassword(_ => { }));
        Assert(!((IPasswordControlEvents)secret.Events).PasswordChanged(), "Queued secret notices remain stale.");
        var replacement = new FormsBackend();
        host.Attach(replacement);
        Assert(app.Secret.Length == 0, "Reattachment never restores a secret from a managed model.");
        var replacementSecret = replacement.Find("forms-password");
        replacementSecret.FailClear = true;
        var cleanup = Throws<AggregateException>(() => host.Detach());
        Assert(cleanup.InnerExceptions.Any(error => error.Message == "secret clear") &&
            replacement.Disposed && replacement.Peers.All(peer => peer.Disposed), "Secret clear failure cannot skip arena and peer release.");
        password.AsSpan().Clear();
        FormsSubtreeCleanup();
    }

    private static void FormsSubtreeCleanup()
    {
        using var host = new Host(new Dispatcher());
        var board = new MutationBoard(host);
        SecretRow? row = null;
        board.Rows = [KeyedItem.Create("secret", h => row = new SecretRow(h))];
        var backend = new FormsBackend();
        host.Attach(backend);
        row!.Secret.SetPassword("fixture");
        var peer = backend.Find("owned-secret");
        char[] previous = peer.Secret;
        board.Rows = [];
        Assert(previous.All(value => value == '\0') && backend.Trace.IndexOf("clear") < backend.Trace.IndexOf("remove"),
            "A retired secret subtree is cleared before its native parent removes it.");
        Assert(!((IPasswordControlEvents)peer.Events).PasswordChanged(), "Removed secret notices stay stale.");
        Throws<ObjectDisposedException>(() => _ = row.Secret.Length);
    }

    private sealed class FormsBackend : IBackend
    {
        public List<FormsPeer> Peers { get; } = [];
        public List<string> Trace { get; } = [];
        public bool Disposed { get; private set; }
        public FormsPeer Find(string id) => Peers.Single(peer => peer.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new FormsPeer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { Disposed = true; Trace.Add("unmount"); }
    }

    private sealed class FormsDriver(FormsBackend backend) : IFormsScenarioDriver
    {
        public void Click(string id) => backend.Find(id).Events.Click();
        public void Change(string id, string value)
        {
            var peer = backend.Find(id);
            peer.NativeText = value;
            peer.Events.Change(value);
        }
        public void SeedPassword(string id, int codeUnits)
        {
            char[] temporary = Enumerable.Repeat('x', codeUnits).ToArray();
            try { backend.Find(id).NativePassword(temporary); }
            finally { temporary.AsSpan().Clear(); }
        }
        public string Text(string id)
        {
            var peer = backend.Find(id);
            if (peer.IsSecret) throw new InvalidOperationException("Generic text reads cannot inspect a password.");
            return peer.NativeText;
        }
        public string Purpose(string id) => backend.Find(id).NativePurpose.ToString();
        public bool ReadOnly(string id) => backend.Find(id).NativeReadOnly;
        public int PasswordLength(string id) => backend.Find(id).PasswordLength;
    }

    private sealed class FormsPeer(FormsBackend backend, Element element, IControlEvents events) : IInputPurposeElementPeer, IPasswordElementPeer, IMutableElementPeer
    {
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public IControlEvents Events { get; } = events;
        public List<ElementProperty> Updates { get; } = [];
        public char[] Secret { get; private set; } = [];
        public int? ReportedLength { get; set; }
        public int PasswordLength => ReportedLength ?? Secret.Length;
        public int ClearCalls { get; private set; }
        public int ReadCalls { get; set; } = 1;
        public bool FailClear { get; set; }
        public bool Disposed { get; private set; }
        public bool IsSecret { get; } = element is PasswordInput;
        public string NativeText = element switch { TextInput input => input.Text, MultilineText document => document.Text, Control control => control.Name, _ => "" };
        public InputPurpose NativePurpose = element is TextInput single ? single.Purpose : InputPurpose.Normal;
        public bool NativeReadOnly = element is MultilineText multiline && multiline.ReadOnly;
        public void AddChild(IElementPeer child) { }
        public void InsertChild(int index, IElementPeer child) { }
        public void RemoveChild(IElementPeer child) => backend.Trace.Add("remove");
        public void ValidateMove(IElementPeer child, int index) { }
        public void MoveChild(IElementPeer child, int index) { }
        public void Update(ElementProperty property)
        {
            Updates.Add(property);
            if (element is TextInput single) NativeText = single.Text;
            else if (element is MultilineText multiline) { NativeText = multiline.Text; NativeReadOnly = multiline.ReadOnly; }
            else if (element is Control control && element is not PasswordInput) NativeText = control.Name;
            if (element is MultilineText document && property == ElementProperty.Text)
                Assert(!Events.Change(document.Text), "Native document setter echoes are suppressed.");
        }
        public void SetPassword(ReadOnlySpan<char> password)
        {
            Secret.AsSpan().Clear();
            Secret = password.ToArray();
            Assert(!((IPasswordControlEvents)Events).PasswordChanged(), "Native secret setter echoes are suppressed.");
        }
        public void NativePassword(ReadOnlySpan<char> password)
        {
            Secret.AsSpan().Clear();
            Secret = password.ToArray();
            ((IPasswordControlEvents)Events).PasswordChanged();
        }
        public void WithPassword(PasswordReceiver receiver)
        {
            char[] temporary = Secret.ToArray();
            try { for (int i = 0; i < ReadCalls; i++) receiver(temporary); }
            finally { temporary.AsSpan().Clear(); }
        }
        public void ClearPassword()
        {
            ClearCalls++;
            Secret.AsSpan().Clear();
            Secret = [];
            backend.Trace.Add("clear");
            Assert(!((IPasswordControlEvents)Events).PasswordChanged(), "Secret clear notices are suppressed before unmount.");
            if (FailClear) throw new ApplicationException("secret clear");
        }
        public void Dispose() { Disposed = true; backend.Trace.Add("peer"); }
    }
}

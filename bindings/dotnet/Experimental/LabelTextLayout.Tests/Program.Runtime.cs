using PortableDemo;
using TextLayoutFixtures;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void RuntimeChecks()
    {
        using var host = new Host(new Dispatcher());
        Label label;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            label = host.Label("literal\nlines");
            root.Add(label);
            host.SetContent(root);
            build.Complete();
        }
        var legacy = new LegacyBackend();
        host.Attach(legacy);
        Assert(label.TextLayout is null, "A default label inherits native wrapping and trimming.");
        Throws<ArgumentException>(() => label.TextLayout = LabelTextLayout.SingleLine());
        Assert(label.TextLayout is null && label.Text == "literal\nlines" && host.IsAttached,
            "Single-line validation rejects existing hard breaks before the model or peer changes.");
        Throws<NotSupportedException>(() => label.TextLayout = LabelTextLayout.Wrap(2));
        Assert(label.TextLayout is null && host.IsAttached,
            "An old backend cannot silently ignore explicit wrapping.");
        label.TextLayout = null;
        label.Text = "single line";
        host.Detach();
        label.TextLayout = LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis);
        var rejected = new LegacyBackend();
        Throws<NotSupportedException>(() => host.Attach(rejected));
        Assert(rejected.Disposed && !host.IsAttached && label.TextLayout == LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis),
            "Missing text-layout capability during peer creation cleans up the owned attachment without erasing managed state.");
        var backend = new Backend();
        host.Attach(backend);
        var peer = backend.Find(label);
        Assert(peer.Layout == label.TextLayout && peer.Text == label.Text,
            "A new attachment reads the retained label descriptor and complete text.");
        int validations = peer.Validations;
        label.TextLayout = LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis);
        Assert(peer.Validations == validations && peer.Updates.Count == 0,
            "An equal descriptor does not invoke native validation or apply again.");
        foreach (string separator in new[] { "\r", "\n", "\u0085", "\u2028", "\u2029" })
        {
            string invalid = "before" + separator + "after";
            Throws<ArgumentException>(() => label.Text = invalid);
            Throws<ArgumentException>(() => ((Control)label).Name = invalid);
            Assert(label.Text == "single line" && peer.Text == "single line" && peer.Updates.Count == 0,
                "Both Label.Text and its inherited Control.Name reject hard breaks before mutation or native update.");
        }
        label.Text = "  spaces\tand e\u0301 stay  ";
        Assert(peer.Text == "  spaces\tand e\u0301 stay  ", "Single-line layout never normalizes other whitespace or combining text.");
        peer.Updates.Clear();
        peer.Reject = true;
        Throws<NotSupportedException>(() => label.TextLayout = LabelTextLayout.Wrap());
        Throws<NotSupportedException>(() => label.TextLayout = null);
        Assert(label.TextLayout == LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis) &&
            peer.Updates.Count == 0 && host.IsAttached,
            "Backend-specific layout and null-reset rejection leave the old descriptor and attachment intact.");
        peer.Reject = false;
        peer.Validating = () => Throws<InvalidOperationException>(() => label.Text = "reentrant");
        label.TextLayout = LabelTextLayout.Wrap(1);
        peer.Validating = null;
        string multiline = "first\r\nsecond\u0085third\u2028fourth\u2029fifth";
        label.Text = multiline;
        Assert(peer.Layout == LabelTextLayout.Wrap(1) && peer.Text == multiline,
            "A wrapped line cap clips native presentation without truncating the complete source or accessible name.");
        Throws<ArgumentException>(() => label.SetTextLayout(LabelTextLayout.SingleLine()));
        Assert(label.TextLayout == LabelTextLayout.Wrap(1) && label.Text == multiline,
            "The fluent setter validates existing text before changing its descriptor.");
        label.TextLayout = null;
        label.Text = "legacy\nhard breaks";
        Assert(peer.Layout is null && peer.Text == label.Text,
            "Clearing layout restores native inheritance and the prior unrestricted hard-break contract.");
        Task.Run(() => Throws<InvalidOperationException>(() => label.TextLayout = null)).GetAwaiter().GetResult();
        peer.FailUpdate = true;
        var committed = LabelTextLayout.Wrap(2);
        Throws<ApplicationException>(() => label.TextLayout = committed);
        Assert(label.TextLayout == committed && !host.IsAttached && backend.Disposed && backend.Peers.All(value => value.Disposed),
            "A native apply failure detaches while retaining the committed descriptor for recovery.");
        var recovery = new Backend();
        host.Attach(recovery);
        Assert(recovery.Find(label).Layout == committed && recovery.Find(label).Text == "legacy\nhard breaks",
            "Reattachment restores the complete committed text and layout.");
        host.Dispose();
        Throws<ObjectDisposedException>(() => label.TextLayout = null);
    }

    private static void GeneratedChecks()
    {
        using var host = new Host(new Dispatcher());
        var fixture = new LayoutFixture(host);
        var backend = new Backend();
        host.Attach(backend);
        var peer = backend.Find(fixture.Target);
        fixture.Layout = LabelTextLayout.Wrap(2);
        fixture.Caption = "First\nSecond\nThird";
        Assert(peer.Layout == LabelTextLayout.Wrap(2) && peer.Text == "First\nSecond\nThird",
            "The generated atomic textLayout binding and later text binding preserve full multiline text.");
        fixture.Caption = "Single paragraph";
        fixture.Layout = LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis);
        Assert(peer.Layout == LabelTextLayout.SingleLine(TextOverflow.CharacterEllipsis),
            "State can select single-line ellipsis through the same generated label, not another tree.");
        Throws<ArgumentException>(() => fixture.Caption = "Invalid\nsingle-line");
        Assert(fixture.Target.Text == "Single paragraph" && peer.Text == "Single paragraph" && host.IsAttached,
            "An invalid generated text update cannot commit the Label.Name or native text.");
        fixture.Layout = LabelTextLayout.Wrap();
        fixture.Caption = "Recovered\nparagraph";
        Assert(ReferenceEquals(peer, backend.Find(fixture.Target)) && peer.Text == fixture.Caption,
            "A later valid generated update reuses the retained native label.");
        WorkbenchChecks();
    }

    private static void WorkbenchChecks()
    {
        using var host = new Host(new Dispatcher());
        var workbench = new LabelLayoutWorkbench(host);
        var backend = new Backend();
        host.Attach(backend);
        var peers = backend.Peers.ToArray();
        var paragraph = backend.Find(workbench.ParagraphLabel);
        var headline = backend.Find(workbench.HeadlineLabel);
        var input = backend.Peers.Single(peer => ReferenceEquals(peer.Element, workbench.DraftInput));
        string completeText = workbench.Paragraph;
        input.Events.Change("retain draft during label reflow");
        foreach (string action in new[] { "uncap", "cap", "clip", "ellipsis", "inherit" })
        {
            var button = backend.Peers.Single(peer => peer.Element is Control control && control.AutomationId == "label-layout-" + action);
            Assert(button.Events.Click(), "The shared label workbench executes compiled native-button actions.");
            Assert(paragraph.Text == completeText && workbench.Draft == "retain draft during label reflow" &&
                !input.Updates.Contains(ElementProperty.Text) && peers.SequenceEqual(backend.Peers),
                "Label reflow changes keep the full text, unrelated draft, and complete peer identity.");
        }
        Assert(paragraph.Layout is null && headline.Layout is null,
            "The shared workbench clears both label overrides without replacing its controls.");
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new NotSupportedException("Synchronous label fixtures do not post UI work.");
    }

    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public Peer Find(Label label) => Peers.Single(peer => ReferenceEquals(peer.Element, label));
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() => Disposed = true;
    }

    private sealed class Peer(Element element, IControlEvents events) : ITextLayoutPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public LabelTextLayout? Layout { get; private set; } = (element as Label)?.TextLayout;
        public string Text { get; private set; } = (element as Label)?.Text ?? "";
        public List<ElementProperty> Updates { get; } = [];
        public int Validations { get; private set; }
        public bool Disposed { get; private set; }
        public bool Reject { get; set; }
        public bool FailUpdate { get; set; }
        public Action? Validating { get; set; }
        public void AddChild(IElementPeer child) { }
        public void ValidateTextLayout(LabelTextLayout? layout)
        {
            Validations++;
            Validating?.Invoke();
            if (Reject) throw new NotSupportedException("Requested native text layout is unsupported.");
        }
        public void Update(ElementProperty property)
        {
            if (FailUpdate) throw new ApplicationException("Native label layout failed.");
            Updates.Add(property);
            if (property == ElementProperty.TextLayout) Layout = ((Label)Element).TextLayout;
            if (property == ElementProperty.Name) Text = ((Label)Element).Text;
        }
        public void Dispose() => Disposed = true;
    }

    private sealed class LegacyBackend : IBackend
    {
        public bool Disposed { get; private set; }
        public IElementPeer Create(Element element, IControlEvents events) => new LegacyPeer();
        public void Mount(IElementPeer root) { }
        public void Dispose() => Disposed = true;
    }

    private sealed class LegacyPeer : IElementPeer
    {
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property) { }
        public void Dispose() { }
    }
}

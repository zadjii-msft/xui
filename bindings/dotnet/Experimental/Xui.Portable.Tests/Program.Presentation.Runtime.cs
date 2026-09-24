using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void PresentationRuntimeChecks()
    {
        using var host = new Host(new Dispatcher());
        var (input, label) = PresentationTree(host);
        Assert(host.Theme is null && input.Typography is null && label.Typography is null,
            "Untouched presentation inherits native typography and host appearance.");
        var legacy = new Backend();
        host.Attach(legacy);
        Throws<NotSupportedException>(() => host.Theme = ThemeSettings.System);
        Throws<NotSupportedException>(() => input.Typography = new Typography());
        Assert(host.Theme is null && input.Typography is null && host.IsAttached && !legacy.Disposed,
            "Legacy backends reject explicit system theme and body typography before model mutation.");
        host.Theme = null;
        input.Typography = null;
        Assert(legacy.Peers.All(peer => peer.Updates.Count == 0),
            "Inherited no-op assignments do not write native presentation.");
        host.Detach();

        host.Theme = ThemeSettings.System;
        var rejected = new Backend();
        Throws<NotSupportedException>(() => host.Attach(rejected));
        Assert(!host.IsAttached && !rejected.Disposed && rejected.Peers.Count == 0,
            "Unsupported theme attach preflight leaves the rejected backend caller-owned.");
        host.Theme = null;
        input.Typography = new Typography(TextRole.Body, 18, 700);
        var rejectedTypography = new Backend();
        Throws<NotSupportedException>(() => host.Attach(rejectedTypography));
        Assert(!host.IsAttached && rejectedTypography.Disposed && rejectedTypography.Peers.All(peer => peer.Disposed),
            "A peer capability failure cleans up already-created owned peers.");
        Assert(input.Typography == new Typography(TextRole.Body, 18, 700),
            "Unsupported attachment does not erase authored typography.");

        host.Theme = new ThemeSettings(ThemeMode.Dark,
            new ThemeResources(new ThemeColor(0xffffff), new ThemeColor(0x101010), new ThemeColor(0x60cdff)));
        var backend = new PresentationBackend();
        host.Attach(backend);
        var inputPeer = backend.Find(input);
        var originalPeers = backend.Peers.ToArray();
        Assert(backend.Mounted && backend.Theme == host.Theme && backend.ThemeApplications == 1 &&
            inputPeer.NativeTypography == input.Typography,
            "Attachment projects committed theme and typography onto the new native peers.");
        Assert(inputPeer.Events.Change("retained draft") && input.Text == "retained draft",
            "Native input reaches the model before presentation changes.");
        inputPeer.NativeText = "retained draft";
        int changes = 0;
        input.Changed += _ => changes++;
        inputPeer.Updates.Clear();
        backend.DuringApply = () => Assert(!inputPeer.Events.Change("synchronous echo"),
            "Theme application suppresses synchronous native input echoes.");
        host.Theme = new ThemeSettings(ThemeMode.Light);
        label.Typography = new Typography(TextRole.Title);
        input.Typography = new Typography(TextRole.Caption, 20, 400);
        Assert(inputPeer.Updates.SequenceEqual([ElementProperty.Typography]) &&
            inputPeer.NativeText == "retained draft" && input.Text == "retained draft" && changes == 0,
            "Typography and theme updates never echo text or invoke input handlers.");
        Assert(originalPeers.SequenceEqual(backend.Peers),
            "Presentation updates retain the complete peer tree.");
        int applications = backend.ThemeApplications;
        int validations = backend.ThemeValidations;
        host.Theme = new ThemeSettings(ThemeMode.Light);
        input.Typography = new Typography(TextRole.Caption, 20, 400);
        Assert(backend.ThemeApplications == applications && backend.ThemeValidations == validations &&
            inputPeer.Updates.Count == 1,
            "Equal immutable presentation values skip native theme and typography updates.");
        host.Theme = null;
        input.Typography = null;
        Assert(backend.Theme is null && inputPeer.NativeTypography is null &&
            inputPeer.Updates.SequenceEqual([ElementProperty.Typography, ElementProperty.Typography]),
            "Clearing presentation explicitly restores the backend's inherited baseline.");

        using var unrelated = new Host(new Dispatcher());
        PresentationTree(unrelated);
        var unrelatedBackend = new PresentationBackend();
        unrelated.Attach(unrelatedBackend);
        host.Theme = ThemeSettings.System;
        Assert(unrelated.Theme is null && unrelatedBackend.ThemeApplications == 0,
            "Host theme changes never broadcast to unrelated attachments.");
        Task.Run(() => Throws<InvalidOperationException>(() => host.Theme = null)).GetAwaiter().GetResult();
        Task.Run(() => Throws<InvalidOperationException>(() => input.Typography = null)).GetAwaiter().GetResult();
        host.Dispose();
        Throws<ObjectDisposedException>(() => host.Theme = null);
        Throws<ObjectDisposedException>(() => input.Typography = null);
        Assert(backend.Disposed && backend.Peers.All(peer => peer.Disposed) && !inputPeer.Events.Change("late"),
            "Presentation-capable peers obey terminal attachment lifetime.");
        PresentationTargetChecks();
        PresentationFailureChecks();
        PresentationGeneratedChecks();
    }

    private static void PresentationTargetChecks()
    {
        using var host = new Host(new Dispatcher());
        Control[] supported;
        Control[] unsupported;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            supported = [host.Label("Label"), host.Button("Button"), host.TextInput("Input"),
                host.Toggle("Toggle"), host.CheckBox("Check box")];
            unsupported = [host.Progress("Accessible progress name"),
                host.ScrollView(host.Stack(Axis.Vertical), "Accessible scroll name")];
            foreach (var control in supported.Concat(unsupported)) root.Add(control);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new PresentationBackend();
        host.Attach(backend);
        foreach (var control in supported)
        {
            var peer = backend.Find(control);
            var typography = new Typography(TextRole.Caption, 28, 700);
            Assert(ReferenceEquals(control.SetTypography(typography), control),
                "The typography helper retains the control identity.");
            Assert(control.Typography == typography && peer.NativeTypography == typography &&
                peer.TypographyValidations == 1 && peer.Updates.SequenceEqual([ElementProperty.Typography]),
                "Every supported text-bearing target applies one validated atomic typography update.");
            peer.RejectTypography = true;
            control.Typography = new Typography(TextRole.Caption, 28, 700);
            Assert(peer.TypographyValidations == 1 && peer.Updates.Count == 1,
                "Equal typography bypasses peer preflight and cannot fail spuriously.");
            Throws<NotSupportedException>(() => control.Typography = null);
            Assert(control.Typography == typography && peer.NativeTypography == typography &&
                peer.Updates.Count == 1 && host.IsAttached,
                "A rejected null reset preserves the prior typography and native attachment.");
            peer.RejectTypography = false;
            control.Typography = null;
            Assert(control.Typography is null && peer.NativeTypography is null &&
                peer.Updates.SequenceEqual([ElementProperty.Typography, ElementProperty.Typography]),
                "Every supported target forwards null to restore its captured native baseline.");
            int validations = peer.TypographyValidations;
            control.Typography = null;
            Assert(peer.TypographyValidations == validations && peer.Updates.Count == 2,
                "An already-inherited target remains untouched.");
        }
        foreach (var control in unsupported)
        {
            var peer = backend.Find(control);
            control.Typography = null;
            Throws<NotSupportedException>(() => control.Typography = new Typography());
            Assert(control.Typography is null && peer.TypographyValidations == 0 && peer.Updates.Count == 0,
                "Accessible-only control names cannot silently consume visible text styling.");
        }
        host.Theme = new ThemeSettings(ThemeMode.Dark);
        backend.RejectTheme = true;
        int themeApplications = backend.ThemeApplications;
        Throws<NotSupportedException>(() => host.Theme = null);
        Assert(host.Theme == new ThemeSettings(ThemeMode.Dark) && backend.Theme == host.Theme &&
            backend.ThemeApplications == themeApplications && host.IsAttached,
            "A rejected inherited-theme reset preserves the committed scheme and attachment.");
        backend.RejectTheme = false;
        host.Theme = null;
        Assert(backend.Theme is null && backend.ThemeApplications == themeApplications + 1,
            "A validated inherited-theme reset is applied exactly once.");
    }

    private static void PresentationFailureChecks()
    {
        using var buildingHost = new Host(new Dispatcher());
        using (var build = buildingHost.BeginBuild())
        {
            var root = buildingHost.Stack(Axis.Vertical);
            Throws<InvalidOperationException>(() => buildingHost.Theme = ThemeSettings.System);
            Assert(buildingHost.Theme is null, "A component build cannot restyle the whole host.");
            buildingHost.SetContent(root);
            build.Complete();
        }

        using var host = new Host(new Dispatcher());
        var (input, _) = PresentationTree(host);
        host.Theme = ThemeSettings.System;
        var rejected = new PresentationBackend { RejectTheme = true };
        Throws<NotSupportedException>(() => host.Attach(rejected));
        Assert(!rejected.Disposed && rejected.Peers.Count == 0 && !host.IsAttached,
            "Backend-specific theme preflight rejection also preserves caller ownership.");
        var failedAttach = new PresentationBackend { FailApply = true };
        Throws<ApplicationException>(() => host.Attach(failedAttach));
        Assert(failedAttach.Disposed && failedAttach.Peers.All(peer => peer.Disposed) && !host.IsAttached,
            "A theme application failure after mounting releases the owned attachment.");
        var backend = new PresentationBackend();
        host.Attach(backend);
        var peer = backend.Find(input);
        backend.RejectTheme = true;
        Throws<NotSupportedException>(() => host.Theme = new ThemeSettings(ThemeMode.Dark));
        Assert(host.Theme == ThemeSettings.System && host.IsAttached && !backend.Disposed,
            "Live theme capability rejection preserves model and attachment.");
        backend.RejectTheme = false;
        peer.RejectTypography = true;
        Throws<NotSupportedException>(() => input.Typography = new Typography(fontSize: 32));
        Assert(input.Typography is null && peer.Updates.Count == 0 && host.IsAttached,
            "Backend-specific typography rejection precedes model commit.");
        peer.RejectTypography = false;
        backend.DuringValidate = () => Throws<InvalidOperationException>(() => input.Text = "reentrant");
        host.Theme = new ThemeSettings(ThemeMode.Light);
        Assert(input.Text == "", "Read-only presentation validation forbids reentrant tree mutation.");
        backend.DuringValidate = null;
        peer.DuringValidate = () => Throws<InvalidOperationException>(() => host.Theme = null);
        input.Typography = new Typography(fontSize: 18);
        Assert(host.Theme == new ThemeSettings(ThemeMode.Light), "Typography validation cannot reenter host theming.");
        peer.DuringValidate = null;
        backend.FailApply = true;
        var committed = new ThemeSettings(ThemeMode.Dark);
        Throws<ApplicationException>(() => host.Theme = committed);
        Assert(host.Theme == committed && !host.IsAttached && backend.Disposed && backend.Peers.All(value => value.Disposed),
            "Theme application failure retains the committed model and detaches the failed native tree.");
        Assert(!peer.Events.Change("stale"), "Failed theme updates invalidate native input callbacks.");

        var recovered = new PresentationBackend();
        host.Attach(recovered);
        var recoveredPeer = recovered.Find(input);
        Assert(recovered.Theme == committed && recoveredPeer.NativeTypography == input.Typography,
            "Reattachment projects the retained theme and typography after an apply failure.");
        recoveredPeer.FailUpdate = true;
        var committedTypography = new Typography(TextRole.Title, 28, 700);
        Throws<ApplicationException>(() => input.Typography = committedTypography);
        Assert(input.Typography == committedTypography && !host.IsAttached && recovered.Disposed,
            "Typography uses the same detach-and-retain failure contract as other element properties.");
        var final = new PresentationBackend();
        host.Attach(final);
        Assert(final.Find(input).NativeTypography == committedTypography,
            "A new native editor receives the committed typography after recovery.");
    }

    private static (TextInput Input, Label Label) PresentationTree(Host host)
    {
        using var build = host.BeginBuild();
        var root = host.Stack(Axis.Vertical);
        var input = host.TextInput("Retained editor");
        var label = host.Label("Presentation");
        root.Add(label).Add(input);
        host.SetContent(root);
        build.Complete();
        return (input, label);
    }

    private sealed class PresentationBackend : IThemeBackend
    {
        public List<PresentationPeer> Peers { get; } = [];
        public ThemeSettings? Theme { get; private set; }
        public int ThemeValidations { get; private set; }
        public int ThemeApplications { get; private set; }
        public bool Mounted { get; private set; }
        public bool Disposed { get; private set; }
        public bool RejectTheme { get; set; }
        public bool FailApply { get; set; }
        public Action? DuringValidate { get; set; }
        public Action? DuringApply { get; set; }
        public PresentationPeer Find(Element element) => Peers.Single(peer => ReferenceEquals(peer.Element, element));
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new PresentationPeer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) => Mounted = true;
        public void ValidateTheme(ThemeSettings? theme)
        {
            ThemeValidations++;
            DuringValidate?.Invoke();
            if (RejectTheme) throw new NotSupportedException("This attachment does not own the requested theme scope.");
        }
        public void ApplyTheme(ThemeSettings? theme)
        {
            Assert(Mounted, "Theme applies only after a host surface exists.");
            ThemeApplications++;
            Theme = theme;
            DuringApply?.Invoke();
            if (FailApply) throw new ApplicationException("Native theme application failed.");
        }
        public void Dispose() { Disposed = true; Mounted = false; }
    }

    private sealed class PresentationPeer(Element element, IControlEvents events) : IPresentationPeer
    {
        public Element Element { get; } = element;
        public IControlEvents Events { get; } = events;
        public Typography? NativeTypography { get; private set; } = (element as Control)?.Typography;
        public string NativeName { get; private set; } = (element as Control)?.Name ?? "";
        public string NativeText { get; set; } = (element as TextInput)?.Text ?? "";
        public List<ElementProperty> Updates { get; } = [];
        public int TypographyValidations { get; private set; }
        public bool Disposed { get; private set; }
        public bool RejectTypography { get; set; }
        public bool FailUpdate { get; set; }
        public Action? DuringValidate { get; set; }
        public void AddChild(IElementPeer child) { }
        public void ValidateTypography(Typography? typography)
        {
            TypographyValidations++;
            DuringValidate?.Invoke();
            if (RejectTypography) throw new NotSupportedException("This peer cannot render the requested typography.");
        }
        public void Update(ElementProperty property)
        {
            if (FailUpdate) throw new ApplicationException("Native typography update failed.");
            Updates.Add(property);
            if (property == ElementProperty.Typography) NativeTypography = ((Control)Element).Typography;
            if (property == ElementProperty.Name) NativeName = ((Control)Element).Name;
            if (property == ElementProperty.Text) NativeText = ((TextInput)Element).Text;
        }
        public void Dispose() => Disposed = true;
    }
}

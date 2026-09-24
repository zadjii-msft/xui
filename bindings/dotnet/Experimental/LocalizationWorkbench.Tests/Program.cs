using System.Globalization;
using System.Resources;
using PortableDemo.Localization;
using Xui.Experimental.Portable;

internal static class Program
{
    private static int assertions;

    private static void Main(string[] args)
    {
        if (args is ["--expect-missing-bundle", "en" or "de" or "ar"])
        {
            var error = Throws<MissingManifestResourceException>(() => LocalizationCatalog.Load(args[1]));
            Assert(error.Message.Contains($"'{args[1]}' localization bundle", StringComparison.Ordinal),
                "A missing bundle error identifies the absent language, not a fallback success.");
            Console.WriteLine($"Missing '{args[1]}' bundle rejected explicitly.");
            return;
        }
        if (args.Length != 0) throw new ArgumentException("Unknown localization test arguments.");
        ResourceChecks();
        WorkbenchChecks();
        Console.WriteLine($"Localization workbench: {assertions} assertions passed.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void ResourceChecks()
    {
        var currentCulture = CultureInfo.CurrentCulture;
        var currentUiCulture = CultureInfo.CurrentUICulture;
        var english = LocalizationCatalog.Load("en");
        var german = LocalizationCatalog.Load("de-DE");
        var arabic = LocalizationCatalog.Load("ar-EG");
        Assert(english.Title == "Language workbench", "The explicit English resource bundle loads.");
        Assert(german.Title == "Sprachwerkstatt" && german.DraftCaption == "Ihr unveränderter Textentwurf",
            "A regional German request selects the German main-assembly bundle.");
        Assert(arabic.Title == "ورشة اللغات" && arabic.DraftCaption == "مسودتك",
            "A regional Arabic request selects the Arabic main-assembly bundle.");
        Assert(english.Culture.Name == "en" && german.Culture.Name == "de-DE" && arabic.Culture.Name == "ar-EG",
            "Formatting retains the explicitly requested culture, not the resource fallback language.");
        Assert(english.FormatCount(1234) == "Preview count: 1,234" &&
            german.FormatCount(1234) == "Vorschauanzahl: 1.234",
            "The same count formats using explicit application-selected cultures.");
        Assert(english.FallbackNote == "This note intentionally falls back to the neutral English resource." &&
            german.FallbackNote == english.FallbackNote && arabic.FallbackNote == english.FallbackNote,
            "An intentionally untranslated key uses the explicit English-bundle fallback.");
        Assert(LocalizationCatalog.Load("de-AT").Title == german.Title &&
            LocalizationCatalog.Load("en-GB").Title == english.Title,
            "Supported regional variants select their language bundle.");
        Assert(english.Culture.IsReadOnly && german.Culture.IsReadOnly,
            "Catalog snapshots cannot mutate the shared formatting culture.");
        Assert(ReferenceEquals(CultureInfo.CurrentCulture, currentCulture) &&
            ReferenceEquals(CultureInfo.CurrentUICulture, currentUiCulture),
            "Resolving and formatting localized strings leaves ambient application cultures untouched.");

        var assembly = typeof(LocalizationCatalog).Assembly;
        var manifests = assembly.GetManifestResourceNames();
        Assert(manifests.Where(name => name.StartsWith("PortableDemo.Localization.WorkbenchStrings", StringComparison.Ordinal))
            .Order(StringComparer.Ordinal).SequenceEqual(new[]
            {
                "PortableDemo.Localization.WorkbenchStrings.ar.resources",
                "PortableDemo.Localization.WorkbenchStrings.de.resources",
                "PortableDemo.Localization.WorkbenchStrings.en.resources"
            }), "The main assembly contains exactly the three explicit language bundles.");
        var englishResources = new ResourceManager("PortableDemo.Localization.WorkbenchStrings.en", assembly);
        foreach (string language in new[] { "en", "de", "ar" })
        {
            string resourceBase = $"PortableDemo.Localization.WorkbenchStrings.{language}";
            Assert(manifests.Contains(resourceBase + ".resources", StringComparer.Ordinal),
                "Each language is embedded in the main assembly without satellite loading.");
            var resources = new ResourceManager(resourceBase, assembly);
            var exact = resources.GetResourceSet(CultureInfo.InvariantCulture, true, false);
            Assert(exact is not null && exact.GetString("Title") == LocalizationCatalog.Load(language).Title,
                "The selected language bundle contains a real translation, not English fallback masquerading as translation.");
            if (language != "en")
                Assert(exact!.GetString("FallbackNote") is null,
                    "The fallback demonstration is intentionally absent from translated bundles.");
        }
        Throws<MissingManifestResourceException>(() =>
            LocalizationCatalog.RequiredString(new ResourceManager("PortableDemo.Localization.WorkbenchStrings.de", assembly),
                englishResources, "MissingRequiredKey", "de"));
        Throws<ArgumentNullException>(() => LocalizationCatalog.Load(null!));
        foreach (string invalid in new[] { "", " ", " de", "de " })
            Throws<ArgumentException>(() => LocalizationCatalog.Load(invalid));
        Throws<CultureNotFoundException>(() => LocalizationCatalog.Load("not a culture!"));
        foreach (string unavailable in new[] { "fr", "ja-JP", "he" })
            Throws<NotSupportedException>(() => LocalizationCatalog.Load(unavailable));
    }

    private static void WorkbenchChecks()
    {
        using var host = new Host(new Dispatcher());
        var workbench = new LocalizationWorkbench(host);
        var backend = new Backend();
        host.Attach(backend);
        var peers = backend.Peers.ToArray();
        var ids = peers.Select(peer => peer.Id).ToArray();
        var input = backend.Find("localization-draft");
        int changes = 0;
        workbench.DraftInput.Changed += _ => changes++;
        const string draft = "Draft e\u0301 \u05e9\u05dc\u05d5\u05dd \u0645\u0631\u062d\u0628\u0627 \u4e2d\u6587 \U0001f469\u200d\U0001f4bb";
        input.NativeText = draft;
        input.Selection = new(6, draft.Length);
        input.Focused = true;
        Assert(input.Events.Change(draft), "Native editing reaches the authored state.");
        Assert(workbench.Draft == draft && changes == 1 && input.Updates.Count == 0,
            "A user edit is retained verbatim without setter echo or Unicode normalization.");

        foreach (var expected in new[]
        {
            (Id: "localization-german", Culture: "de", Title: "Sprachwerkstatt", Caption: "Ihr unveränderter Textentwurf"),
            (Id: "localization-arabic", Culture: "ar", Title: "ورشة اللغات", Caption: "مسودتك"),
            (Id: "localization-english", Culture: "en", Title: "Language workbench", Caption: "Your draft")
        })
        {
            Assert(backend.Find(expected.Id).Events.Click(), "A language action invokes compiled shared C#.");
            Assert(workbench.Copy.Culture.Name == expected.Culture &&
                backend.Find("localization-title").Name == expected.Title && input.Name == expected.Caption &&
                backend.Find("localization-content").Name == expected.Title,
                "Translated captions and accessible control names update in place.");
            Assert(input.Placeholder == workbench.Copy.DraftPlaceholder && input.Help == workbench.Copy.DraftHelp,
                "Placeholder and help text use the same complete localized snapshot.");
            Assert(workbench.Draft == draft && input.NativeText == draft && changes == 1 &&
                !input.Updates.Contains(ElementProperty.Text) && input.Focused && input.Selection == new TextSelection(6, draft.Length),
                "Language changes never write the draft, request focus, or replace the recorded selection.");
            Assert(peers.SequenceEqual(backend.Peers) && ids.SequenceEqual(backend.Peers.Select(peer => peer.Id)),
                "The shared tree, peer identities, and literal automation IDs survive localization.");
        }
        workbench.SelectLanguage("de-DE");
        Assert(backend.Find("localization-count").Name == "Vorschauanzahl: 1.234",
            "Generated text binds explicit localized number formatting.");
        Assert(backend.Find("localization-increment").Events.Click() && workbench.Count == 1235 &&
            backend.Find("localization-count").Name == "Vorschauanzahl: 1.235",
            "Counter state updates use the currently selected format culture.");
        var before = workbench.Copy;
        int updates = backend.Peers.Sum(peer => peer.Updates.Count);
        Throws<NotSupportedException>(() => workbench.SelectLanguage("fr-FR"));
        Assert(ReferenceEquals(workbench.Copy, before) && backend.Peers.Sum(peer => peer.Updates.Count) == updates &&
            host.IsAttached && workbench.Draft == draft,
            "Unsupported localization fails before publishing a snapshot or a false-success UI.");
        Task.Run(() => Throws<InvalidOperationException>(() => workbench.SelectLanguage("en"))).GetAwaiter().GetResult();
        host.Detach();
        Assert(backend.Disposed && peers.All(peer => peer.Disposed) && !input.Events.Change("stale"),
            "Localized components preserve normal attachment ownership and stale-event rejection.");
        var restored = new Backend();
        host.Attach(restored);
        Assert(restored.Find("localization-draft").NativeText == draft &&
            restored.Find("localization-title").Name == "Sprachwerkstatt",
            "Reattachment projects the retained draft and selected localized snapshot.");
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new NotSupportedException("These synchronous fixtures do not post work.");
    }

    private sealed class Backend : IBackend
    {
        public List<Peer> Peers { get; } = [];
        public bool Disposed { get; private set; }
        public Peer Find(string id) => Peers.Single(peer => peer.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new Peer(element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) => Assert(Peers.Contains((Peer)root), "The host mounts an owned peer.");
        public void Dispose() => Disposed = true;
    }

    private sealed class Peer(Element element, IControlEvents events) : IElementPeer
    {
        public IControlEvents Events { get; } = events;
        public string Id { get; } = (element as Control)?.AutomationId ?? "";
        public string Name { get; private set; } = (element as Control)?.Name ?? "";
        public string Help { get; private set; } = (element as Control)?.Help ?? "";
        public string Placeholder { get; private set; } = (element as TextInput)?.Placeholder ?? "";
        public string NativeText { get; set; } = (element as TextInput)?.Text ?? "";
        public bool Focused { get; set; }
        public TextSelection Selection { get; set; }
        public bool Disposed { get; private set; }
        public List<ElementProperty> Updates { get; } = [];
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property)
        {
            Updates.Add(property);
            switch (property)
            {
                case ElementProperty.Name: Name = ((Control)element).Name; break;
                case ElementProperty.Help: Help = ((Control)element).Help; break;
                case ElementProperty.Placeholder: Placeholder = ((TextInput)element).Placeholder; break;
                case ElementProperty.Text: NativeText = ((TextInput)element).Text; break;
            }
        }
        public void Dispose() => Disposed = true;
    }
}

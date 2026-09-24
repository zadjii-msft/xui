using System.Globalization;
using System.Resources;
using PortableDemo.Localization;
using Xui.Experimental.Portable;

namespace LocalizationConsumer;

public static class LocalizationProof
{
    public static void Verify()
    {
        var before = CultureInfo.CurrentCulture;
        var beforeUi = CultureInfo.CurrentUICulture;
        var english = LocalizationCatalog.Load("en");
        var german = LocalizationCatalog.Load("de-DE");
        var arabic = LocalizationCatalog.Load("ar-EG");
        Require(english.Title == "Language workbench", "Neutral English resource.");
        Require(german.Title == "Sprachwerkstatt" && german.DraftCaption == "Ihr unver\u00e4nderter Textentwurf",
            "Real German language bundle.");
        Require(arabic.Title == "\u0648\u0631\u0634\u0629 \u0627\u0644\u0644\u063a\u0627\u062a" &&
            arabic.DraftCaption == "\u0645\u0633\u0648\u062f\u062a\u0643", "Real Arabic language bundle.");
        Require(german.FormatCount(1234) == "Vorschauanzahl: 1.234" &&
            english.FormatCount(1234) == "Preview count: 1,234", "Explicit culture formatting.");
        Require(german.Culture.Name == "de-DE" && arabic.Culture.Name == "ar-EG", "Requested culture survives resource fallback.");
        Require(german.FallbackNote == english.FallbackNote && arabic.FallbackNote == english.FallbackNote &&
            english.FallbackNote == "This note intentionally falls back to the neutral English resource.",
            "Only the intentionally absent key falls back to neutral English.");
        string[] names = typeof(LocalizationCatalog).Assembly.GetManifestResourceNames();
        foreach (string culture in new[] { "en", "de", "ar" })
        {
            string baseName = $"PortableDemo.Localization.WorkbenchStrings.{culture}";
            Require(names.Contains(baseName + ".resources"), "All three language bundles survive in the main shared assembly.");
            var manager = new ResourceManager(baseName, typeof(LocalizationCatalog).Assembly);
            var resourceSet = manager.GetResourceSet(CultureInfo.InvariantCulture, true, false);
            Require(resourceSet?.GetString("Title") == LocalizationCatalog.Load(culture).Title, "No English fallback masking a missing bundle.");
            if (culture != "en") Require(resourceSet!.GetString("FallbackNote") is null, "Fallback fixture key stays absent in translated bundles.");
        }
        try { LocalizationCatalog.Load("fr"); throw new InvalidDataException("Unsupported language was accepted."); }
        catch (NotSupportedException) { }
        using var host = new Host(new Dispatcher());
        var workbench = new LocalizationWorkbench(host);
        var input = workbench.DraftInput;
        const string draft = "Owned draft e\u0301 \u0645\u0631\u062d\u0628\u0627";
        workbench.Draft = draft;
        foreach (string culture in new[] { "de-DE", "ar-EG", "en-GB" })
        {
            workbench.SelectLanguage(culture);
            Require(ReferenceEquals(input, workbench.DraftInput) && input.Text == draft && workbench.Draft == draft,
                "Compiled shared component retains draft and input identity across language changes.");
        }
        Require(ReferenceEquals(before, CultureInfo.CurrentCulture) && ReferenceEquals(beforeUi, CultureInfo.CurrentUICulture),
            "Application language selection leaves ambient cultures unchanged.");
    }

    public static void VerifyMissingBundle(string culture)
    {
        if (culture is not ("en" or "de" or "ar")) throw new ArgumentException("Select en, de, or ar.", nameof(culture));
        try { LocalizationCatalog.Load(culture); }
        catch (MissingManifestResourceException) { return; }
        throw new InvalidDataException($"Missing '{culture}' bundle was hidden by an English fallback.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidDataException(message);
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new NotSupportedException("This package fixture has no asynchronous UI queue.");
    }
}

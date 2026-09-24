using System.Globalization;
using System.Resources;

namespace PortableDemo.Localization;

public sealed record WorkbenchText(
    CultureInfo Culture,
    string Title,
    string Description,
    string DraftCaption,
    string DraftPlaceholder,
    string DraftHelp,
    string CountCaption,
    string AddOne,
    string English,
    string German,
    string Arabic,
    string FallbackNote)
{
    public string FormatCount(int count) => $"{CountCaption}: {count.ToString("N0", Culture)}";
}

public static class LocalizationCatalog
{
    private static readonly ResourceManager English = CreateManager("en");
    private static readonly ResourceManager German = CreateManager("de");
    private static readonly ResourceManager Arabic = CreateManager("ar");

    private static ResourceManager CreateManager(string language) => new(
        $"PortableDemo.Localization.WorkbenchStrings.{language}", typeof(LocalizationCatalog).Assembly);

    public static WorkbenchText Load(string cultureName)
    {
        ArgumentNullException.ThrowIfNull(cultureName);
        if (string.IsNullOrWhiteSpace(cultureName) || cultureName != cultureName.Trim())
            throw new ArgumentException("An explicit culture name is required.", nameof(cultureName));
        var culture = CultureInfo.GetCultureInfo(cultureName);
        string language = culture.TwoLetterISOLanguageName;
        var resources = language switch
        {
            "en" => English,
            "de" => German,
            "ar" => Arabic,
            _ => throw new NotSupportedException($"The localization workbench does not provide language '{culture.Name}'.")
        };
        RequireBundle(resources, language);
        if (language != "en") RequireBundle(English, "en");
        string Get(string key) => RequiredString(resources, English, key, language);
        return new(culture, Get("Title"), Get("Description"), Get("DraftCaption"), Get("DraftPlaceholder"),
            Get("DraftHelp"), Get("CountCaption"), Get("AddOne"), Get("English"), Get("German"),
            Get("Arabic"), Get("FallbackNote"));
    }

    private static void RequireBundle(ResourceManager resources, string language)
    {
        try
        {
            if (resources.GetResourceSet(CultureInfo.InvariantCulture, true, false) is null)
                throw new MissingManifestResourceException("The resource bundle is absent.");
        }
        catch (MissingManifestResourceException error)
        {
            throw new MissingManifestResourceException($"Required '{language}' localization bundle is not deployed.", error);
        }
    }

    internal static string RequiredString(ResourceManager resources, ResourceManager english, string key, string language)
    {
        var value = resources.GetString(key, CultureInfo.InvariantCulture) ??
            english.GetString(key, CultureInfo.InvariantCulture);
        if (string.IsNullOrWhiteSpace(value))
            throw new MissingManifestResourceException($"Required localized text '{key}' is missing for '{language}'.");
        return value;
    }
}

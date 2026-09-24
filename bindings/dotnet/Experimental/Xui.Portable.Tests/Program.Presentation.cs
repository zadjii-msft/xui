using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void PresentationChecks()
    {
        var body = new Typography();
        var caption = new Typography(TextRole.Caption);
        var title = new Typography(TextRole.Title);
        Assert(body.Role == TextRole.Body && body.FontSize is null && body.FontWeight is null,
            "Absent typography overrides remain distinguishable from role defaults.");
        Assert(body.ResolvedFontSize == 14 && body.ResolvedFontWeight == 400 &&
            caption.ResolvedFontSize == 12 && caption.ResolvedFontWeight == 400 &&
            title.ResolvedFontSize == 24 && title.ResolvedFontWeight == 700,
            "Semantic typography resolves consistent logical sizes and weights before native scaling.");
        Assert(new Typography(TextRole.Title, 20).ResolvedFontWeight == 700 &&
            new Typography(TextRole.Caption, fontWeight: 700).ResolvedFontSize == 12,
            "Explicit typography fields override only their own role defaults.");
        Assert(new Typography(TextRole.Title, 18, 700) == new Typography(TextRole.Title, 18, 700) &&
            new Typography(TextRole.Title, 18, 700) != new Typography(TextRole.Body, 18, 700),
            "Atomic typography descriptors have value equality and retain their semantic role.");
        foreach (float size in new[] { 8f, 12.5f, 32f })
            Assert(new Typography(fontSize: size).ResolvedFontSize == size, "Valid finite font sizes retain exact logical units.");
        foreach (uint weight in new[] { 400u, 700u })
            Assert(new Typography(fontWeight: weight).ResolvedFontWeight == weight, "Valid font weights retain authored values.");
        foreach (float size in new[] { float.NaN, float.PositiveInfinity, float.NegativeInfinity, -1f, 0f, 7.99f, 32.01f })
            Throws<ArgumentOutOfRangeException>(() => new Typography(fontSize: size));
        foreach (uint weight in new[] { 0u, 1u, 399u, 401u, 600u, 699u, 701u, 999u, 1000u, uint.MaxValue })
            Throws<ArgumentOutOfRangeException>(() => new Typography(fontWeight: weight));
        Throws<ArgumentOutOfRangeException>(() => new Typography((TextRole)(-1)));
        Throws<ArgumentOutOfRangeException>(() => new Typography((TextRole)3));

        Assert(new ThemeColor(0) == default(ThemeColor) && new ThemeColor(0xffffff).Dark == 0xffffff,
            "Black and white are valid explicit opaque colors, including the default struct value.");
        var paired = new ThemeColor(0x102030, 0xabcdef);
        Assert(paired.Light == 0x102030 && paired.Dark == 0xabcdef && new ThemeColor(0x123456).Light == new ThemeColor(0x123456).Dark,
            "Paired resources preserve both scheme values, while a single color serves both schemes.");
        foreach (uint invalid in new[] { 0x1000000u, 0xff000000u, uint.MaxValue })
        {
            Throws<ArgumentOutOfRangeException>(() => new ThemeColor(invalid));
            Throws<ArgumentOutOfRangeException>(() => new ThemeColor(invalid, 0));
            Throws<ArgumentOutOfRangeException>(() => new ThemeColor(0, invalid));
        }
        Assert(ThemeResources.Default.IsDefault && ThemeResources.Default == new ThemeResources(),
            "An empty resource scope preserves every native semantic color.");
        var black = new ThemeResources(foreground: default(ThemeColor));
        Assert(!black.IsDefault && black.Foreground == new ThemeColor(0) && black.Background is null && black.Accent is null,
            "Explicit black is not confused with an absent resource override.");
        var palette = new ThemeResources(paired, new ThemeColor(0xffffff, 0x101010), new ThemeColor(0x005fb8, 0x60cdff));
        Assert(palette == new ThemeResources(paired, new ThemeColor(0xffffff, 0x101010), new ThemeColor(0x005fb8, 0x60cdff)),
            "Color scopes have immutable structural equality.");
        Assert(ThemeSettings.System == new ThemeSettings() &&
            ThemeSettings.System == new ThemeSettings(resources: new ThemeResources()),
            "Explicit system settings preserve value equality; they are not an inherited null host theme.");
        foreach (ThemeMode mode in Enum.GetValues<ThemeMode>())
        {
            var settings = new ThemeSettings(mode, palette);
            Assert(settings.Mode == mode && settings.Resources == palette,
                "Every explicit theme mode retains its immutable semantic resource scope.");
        }
        Assert(new ThemeSettings(ThemeMode.Light) != ThemeSettings.System && new ThemeSettings(ThemeMode.Dark) != ThemeSettings.System,
            "Explicit light, dark, and system requests remain distinct.");
        Throws<ArgumentOutOfRangeException>(() => new ThemeSettings((ThemeMode)(-1)));
        Throws<ArgumentOutOfRangeException>(() => new ThemeSettings((ThemeMode)3));
    }
}

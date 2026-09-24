using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void PresentationGeneratedChecks()
    {
        using var host = new Host(new Dispatcher());
        var workbench = PresentationWorkbench.Create(host);
        var backend = new PresentationBackend();
        host.Attach(backend);
        PresentationPeer Find(string id) => backend.Peers.Single(peer => peer.Element is Control control && control.AutomationId == id);
        void Click(string id) => Assert(Find(id).Events.Click(), $"Workbench action '{id}' reaches compiled C#.");
        var input = Find("presentation-draft");
        var original = backend.Peers.ToArray();
        Assert(backend.ThemeApplications == 0 && host.Theme is null,
            "Building the workbench does not implicitly replace an inherited host theme.");
        Assert(Find("presentation-title").NativeTypography == new Typography(TextRole.Title) &&
            Find("presentation-caption").NativeTypography == new Typography(TextRole.Caption) &&
            input.NativeTypography == new Typography(TextRole.Body, 14, 400),
            "Generated role and combined explicit font bindings project complete immutable descriptors.");
        input.NativeText = "Do not replace this editor";
        Assert(input.Events.Change(input.NativeText) && workbench.Draft == input.NativeText,
            "Workbench draft is ordinary compiled C# state.");
        int before = input.Updates.Count;
        Click("presentation-larger");
        Click("presentation-bold");
        Assert(workbench.EditorSize == 28 && workbench.EditorWeight == 700 &&
            input.NativeTypography == new Typography(TextRole.Body, 28, 700),
            "Independent font state dependencies recompute the complete atomic typography value.");
        Assert(Find("presentation-toggle").NativeTypography == new Typography(TextRole.Body, 28, 700) &&
            Find("presentation-check").NativeTypography == new Typography(TextRole.Body, 28, 700) &&
            Find("presentation-bold").NativeTypography == new Typography(TextRole.Body, fontWeight: 700),
            "Every supported native text-bearing control participates in the shared workbench typography.");
        Assert(input.Updates.Skip(before).SequenceEqual([ElementProperty.Typography, ElementProperty.Typography]),
            "Changing each typography field makes one complete presentation update, not individual native writes.");
        Click("presentation-next-role");
        Assert(Find("presentation-preview").NativeTypography == new Typography(TextRole.Caption),
            "A state-driven role compiles as the shared enum scalar.");
        Click("presentation-next-role");
        Assert(Find("presentation-preview").NativeTypography == new Typography(TextRole.Title),
            "Role changes select the title semantics without replacing the label.");
        Click("presentation-next-role");
        Assert(Find("presentation-preview").NativeTypography == new Typography(TextRole.Body),
            "The role preview returns to explicit body typography.");

        Click("presentation-dark");
        Assert(backend.Theme == new ThemeSettings(ThemeMode.Dark) &&
            Find("presentation-appearance").NativeName == "Appearance: Dark / native colors",
            "Authored theme actions apply before publishing the visible state.");
        Click("presentation-colors");
        Assert(backend.Theme?.Resources == new ThemeResources(
            new ThemeColor(0x1a1a1a, 0xffffff), new ThemeColor(0xffffff, 0x202020), new ThemeColor(0x005fb8, 0x60cdff)),
            "The shared workbench supplies typed foreground, background, and accent resources.");
        Click("presentation-light");
        Assert(backend.Theme?.Mode == ThemeMode.Light && workbench.CustomColors,
            "Changing the scheme preserves the immutable authored resource pair.");
        Click("presentation-system");
        Assert(backend.Theme?.Mode == ThemeMode.System && host.Theme is not null,
            "Follow system is explicit presentation, not inherited null.");
        Click("presentation-inherit");
        Assert(backend.Theme is null && host.Theme is null && !workbench.CustomColors &&
            Find("presentation-appearance").NativeName == "Appearance: Inherited",
            "Restore inherited clears the host override without recreating the workbench.");

        backend.RejectTheme = true;
        Throws<NotSupportedException>(() => Find("presentation-dark").Events.Click());
        Assert(host.Theme is null && workbench.Appearance == "Inherited" && host.IsAttached,
            "Unsupported host theming is surfaced; the workbench does not display a false success.");
        backend.RejectTheme = false;
        Click("presentation-normal");
        Click("presentation-bold");
        Assert(input.NativeTypography == new Typography(TextRole.Body, 14, 400),
            "Normal and bold actions restore the initial explicit editor typography.");
        Assert(input.NativeText == "Do not replace this editor" && workbench.Draft == input.NativeText &&
            !input.Updates.Contains(ElementProperty.Text) && original.SequenceEqual(backend.Peers),
            "Every generated presentation action retains draft text and peer identity without editor writeback.");
    }
}

using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void PageChecks()
    {
        using var host = new Host(new Dispatcher());
        var state = new ProfileSnapshot();
        int previewCount = 0;
        ProfileEditPage? page = null;
        page = new ProfileEditPage(host,
            value => page!.Draft = state = state.WithDisplayName(value),
            value => page!.Draft = state = state.WithRole(value),
            value => page!.Draft = state = state.WithLocation(value),
            value => page!.Draft = state = state.WithFocus(value),
            () => previewCount++);
        var backend = new Backend();
        host.Attach(backend);
        var peers = backend.Peers.ToArray();
        var inputs = backend.Peers.Where(p => p.Element is TextInput).ToArray();
        Assert(inputs.Length == 4, "Profile authoring has four native editors.");
        foreach (var peer in inputs)
        {
            var input = (TextInput)peer.Element;
            Assert(input.FixedSize is null && input.PreferredSize is null && input.CaptionVisible &&
                input.Placeholder.Length > 0 && input.Help.Length > 0, "Captioned profile editor leaves native height unconstrained.");
        }
        Assert(!backend.Find("profile-preview").Events.Click() && previewCount == 0, "Invalid profile cannot preview.");
        backend.Find("profile-name").Events.Change("  Ada  ");
        backend.Find("profile-role").Events.Change("Engineer");
        backend.Find("profile-location").Events.Change("London");
        backend.Find("profile-focus").Events.Change("Native input");
        Assert(state == new ProfileSnapshot("  Ada  ", "Engineer", "London", "Native input"), "Generated native handlers update exact draft values.");
        Assert(inputs.All(p => p.Updates.Count == 0) && peers.SequenceEqual(backend.Peers), "Typing retains peers and does not rewrite editor text.");
        Assert(backend.Find("profile-preview").Events.Click() && previewCount == 1, "Valid preview invokes typed navigation callback.");
        page.Busy = true;
        Assert(!backend.Find("profile-name").Events.Change("blocked") &&
            !backend.Find("profile-preview").Events.Click(), "Busy page disables edits and navigation.");
        page.Busy = false;
        Assert(backend.Find("profile-name").Events.Change("Grace"), "Editors recover after busy state.");
        page.Draft = new ProfileSnapshot("Restored", "", "", "");
        Assert(page.NameInput.Text == "Restored" && state.DisplayName == "Grace", "Programmatic refresh is silent.");
        var namePeer = backend.Find("profile-name");
        host.Dispose();
        Assert(!namePeer.Events.Change("late"), "Disposed page ignores native callbacks.");

        using var previewHost = new Host(new Dispatcher());
        int backCount = 0;
        var preview = new ProfilePreviewPage(previewHost, () => backCount++) { Draft = state };
        var previewBackend = new Backend();
        previewHost.Attach(previewBackend);
        Assert(preview.NameLabel.Text == "Grace" && preview.EditButton.AutomationId == "profile-back", "Preview uses the actual unsaved snapshot.");
        Assert(previewBackend.Find("profile-back").Events.Click() && backCount == 1, "Preview back uses typed navigation callback.");
    }
}

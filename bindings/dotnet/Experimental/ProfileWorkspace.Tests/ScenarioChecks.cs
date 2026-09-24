using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void ScenarioChecks()
    {
        var storage = new MemoryStorage();
        using var h = new Harness(storage);
        string json = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "ProfileWorkspaceScenarios.json"));
        int count = ApplicationScenarioRunner.Run(json, "profile-workspace", new Driver(h));
        assertions += count;
        Assert(count >= 40, "Shared profile corpus has at least forty literal UI expectations.");
        Assert(h.Controller.State.Page == ProfilePage.Preview && h.Controller.State.Draft ==
            new ProfileSnapshot("Ada Lovelace", "Developer", "London", "Accessible tools") &&
            h.Controller.State.Status == "Edits are not saved." && storage.Data is null,
            "Corpus ends in the literal unsaved-preview screenshot state without retaining stored test data.");
        Assert(h.Unhandled.Count == 0, "Corpus leaves no unobserved async failure.");
        Console.WriteLine($"Profile corpus: {count} literal fixture expectations.");
    }

    private sealed class Driver(Harness h) : IApplicationScenarioDriver
    {
        public void Change(string id, string value) =>
            Assert(h.Backend.Find(id).Events.Change(value), $"Profile input accepted: {id}.");
        public void Click(string id)
        {
            Assert(h.Backend.Find(id).Events.Click(), $"Profile action accepted: {id}.");
            h.Finish();
        }
        public void Submit(string id)
        {
            Assert(h.Backend.Find(id).Events.Submit(), $"Profile submit accepted: {id}.");
            h.Finish();
        }
        public string Text(string id) => h.Backend.Find(id).Element switch
        {
            TextInput input => input.Text,
            Label label => label.Text,
            Button button => button.Text,
            _ => throw new InvalidOperationException($"'{id}' has no text.")
        };
        public bool Enabled(string id) => ((Control)h.Backend.Find(id).Element).Enabled;
        public bool Visible(string id) => h.Backend.Contains(id) && ((Control)h.Backend.Find(id).Element).Visible;
    }
}

using System.Diagnostics;
using Xui;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try { Run(args); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
    private static void Run(string[] args)
    {
        if (args.Contains("--features")) { FeatureDemo.Run(args.Contains("--callback-fail")); return; }
        using var window = new Window();
        var root = window.Stack();
        root.Padding(20); root.Spacing(10);
        var label = window.Label("Ready — 日本語 😀 — a long Unicode label with native retained layout");
        label.AutomationId = "status";
        var input = window.TextInput("Workspace name"); input.AutomationId = "input"; input.Text = "Alpha 😀";
        var button = window.Button("Apply"); button.AutomationId = "apply";
        var toggle = window.Toggle("Enable previews"); toggle.AutomationId = "toggle";
        var form = window.Stack(); form.Spacing(8);
        for (int i = 0; i < 8; ++i) form.Add(window.Label($"Preference {i} — Unicode 日本語"));
        var scroll = window.ScrollView(form, "Preferences"); scroll.AutomationId = "scroll"; scroll.FixedSize(560, 120);
        var image = window.Image("Preview"); image.AutomationId = "image"; image.FixedSize(192, 96);
        var list = window.FileList("Files"); list.AutomationId = "files";
        list.SetItems(Enumerable.Range(0, 60).Select(i => new FileItem((ulong)i + 1, $"Entry-{i:D3}.txt", "")).ToArray());
        root.Add(label); root.Add(input); root.Add(button); root.Add(toggle); root.Add(scroll); root.Add(image); root.Add(list, 1);
        window.SetContent(root);
        button.Click += () =>
        {
            if (args.Contains("--callback-fail")) throw new InvalidOperationException("GUI callback sentinel");
            label.Text = "Applied";
        };
        toggle.Changed += value => label.Text = value ? "Enabled" : "Disabled";
        input.Changed += _ => label.Text = "Edited";
        input.Submitted += () => label.Text = "Submitted";
        window.Key += e =>
        {
            if ((e.Value & 0xffff) == 0x75) label.Text = "Keyboard";
            if ((e.Value & 0xffff) == 0x76) label.Text = image.Status == ImageStatus.Ready ? "Image ready" : "Image pending";
            if ((e.Value & 0xffff) == 0x77 && args.Length > 0) { image.Unload(); image.Source(args[0], 193, 145); }
            if ((e.Value & 0xffff) == 0x7b) window.Close();
        };
        if (args.Length > 0 && !args[0].StartsWith("--")) image.Source(args[0]);
        if (args.Contains("--throughput"))
        {
            var updates = Enumerable.Range(0, 64).Select(i => new Property(label, PropertyKind.Text, $"Update {i}")).ToArray();
            var clock = Stopwatch.StartNew();
            for (int j = 0; j < 1000; ++j) foreach (var p in updates) window.Update(p);
            double single = clock.Elapsed.TotalMilliseconds;
            clock.Restart();
            for (int j = 0; j < 1000; ++j) window.Update(updates);
            Console.WriteLine($"{{\"mutations\":64000,\"single_ms\":{single:F3},\"batch_ms\":{clock.Elapsed.TotalMilliseconds:F3}}}");
            return;
        }
        window.Run();
    }
}

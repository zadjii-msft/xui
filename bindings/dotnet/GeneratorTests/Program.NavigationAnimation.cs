using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;

internal static partial class Program
{
    private static void TestNavigationAnimation()
    {
        const string source = """
            component AnimatedNavigation {
                state uint Motion = 180;
                view { VStack() {
                    NavigationView("Workspace", duration: Motion, ref: Navigation, searchId: "query");
                    NavigationView("Default", ref: Default);
                } }
            }
            """;
        var (_, compilation) = Generate(new File(@"C:\fixture\AnimatedNavigation.xui", source));
        using var bytes = new MemoryStream();
        var emitted = compilation.Emit(bytes);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        bytes.Position = 0;
        var context = new AssemblyLoadContext("animated-navigation", isCollectible: true);
        var type = context.LoadFromStream(bytes).GetType("AnimatedNavigation")!;
        var instance = Activator.CreateInstance(type, new Xui.Window(), true)!;
        var navigation = (Xui.NavigationView)type.GetProperty("Navigation")!.GetValue(instance)!;
        var defaults = (Xui.NavigationView)type.GetProperty("Default")!.GetValue(instance)!;
        var search = navigation.Search;
        search.Text = "retained query";
        Assert(navigation.Duration == 180 && search.AutomationId == "query",
            "Navigation markup configures opt-in duration and its native search child.");
        Assert(defaults.Duration == 0 && defaults.DurationSets == 0, "Omission preserves the immediate native default.");
        type.GetProperty("Motion")!.SetValue(instance, 240u);
        type.GetProperty("Motion")!.SetValue(instance, 240u);
        type.GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(instance, null);
        Assert(navigation.Duration == 240 && navigation.DurationSets == 2 &&
            ReferenceEquals(navigation.Search, search) && search.Text == "retained query",
            "Reactive duration preserves search identity and avoids redundant setters.");
        try
        {
            type.GetProperty("Motion")!.SetValue(instance, 10001u);
            throw new Exception("Expected navigation duration rejection.");
        }
        catch (TargetInvocationException error) when (error.InnerException is ArgumentOutOfRangeException) { count++; }
        Assert(navigation.Duration == 240, "Rejected duration leaves the native value unchanged.");
        context.Unload();
        Invalid("""component Bad { view { NavigationView("x", animating: true); } }""");
        foreach (string value in new[] { "-1", "1.5", "\"180\"" })
        {
            var (_, bad) = Generate(new File(@"C:\fixture\BadNavigation.xui",
                "component Bad { view { NavigationView(\"x\", duration: " + value + "); } }"));
            Assert(bad.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error),
                "Navigation duration requires a uint expression.");
        }
    }
}

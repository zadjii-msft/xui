extern alias RuntimeXui;
using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;

internal static partial class Program
{
    private static void TestParityControls()
    {
        const string source = """
            component Parity {
                state global::Xui.CheckState Current = global::Xui.CheckState.Unchecked;
                state uint Count = 3;
                state (global::Xui.Choice[] Items, ulong? Selected) Options = (new global::Xui.Choice[] { new(1, "First"), new(2, "Second") }, 1);
                state global::Xui.Command[] Commands = new global::Xui.Command[] { new(10, "File", Kind: global::Xui.CommandKind.Submenu), new(11, "Open", Parent: 10, PinLabel: "Pin") };
                state int Changes = 0;
                state ulong LastCommand = 0;
                style LinkStyle for HyperlinkButton { background: 0x123456; }
                style MenuStyle for MenuBar { background: 0x234567; }
                style CheckStyle for CheckBox { part mark { foreground: 0x345678; } }
                style SelectorStyle for SelectorBar { part item { background: 0x456789; } }
                style BadgeStyle for InfoBadge { part message { foreground: 0x56789A; } }
                view { VStack() {
                    CheckBox("Check", checkState: Current, threeState: true, change: Checked, ref: Check, style: CheckStyle);
                    HyperlinkButton("Open", click: Replace, style: LinkStyle, icon: global::Xui.ButtonIcon.Search, ref: Link);
                    SelectorBar("Pages", items: Options.Items, selected: Options.Selected, change: OnSelected, ref: Selector, style: SelectorStyle);
                    InfoBadge("Count", count: Count, ref: Badge, style: BadgeStyle);
                    InfoBadge("Dot", ref: Dot);
                    InfoBadge("Icon", icon: global::Xui.ButtonIcon.Search, ref: Icon);
                    MenuBar("Main", commands: Commands, invoke: Invoked, pin: Pinned, style: MenuStyle, ref: Menu);
                } }
                code csharp {
                    void Checked(global::Xui.CheckState value) { Current = value; Changes++; }
                    void OnSelected(ulong value) { Options = (Options.Items, value); Changes++; }
                    void Replace() {
                        Options = (new global::Xui.Choice[] { new(3, "Third") }, 3);
                        Count++;
                    }
                    void Invoked(ulong id) { LastCommand = id; Changes++; }
                    void Pinned(ulong id) { LastCommand = id + 100; Changes++; }
                }
            }
            """;
        var (_, compilation) = Generate(new File(@"C:\fixture\Parity.xui", source));
        using var bytes = new MemoryStream();
        var emitted = compilation.Emit(bytes);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        var real = compilation.WithReferences(References
            .Where(reference => reference.Display != typeof(Xui.Window).Assembly.Location)
            .Append(MetadataReference.CreateFromFile(typeof(RuntimeXui::Xui.Window).Assembly.Location)));
        using var realBytes = new MemoryStream();
        var realEmitted = real.Emit(realBytes);
        Assert(realEmitted.Success, string.Join("\n", realEmitted.Diagnostics));
        bytes.Position = 0;
        var context = new AssemblyLoadContext("parity-controls", isCollectible: true);
        var type = context.LoadFromStream(bytes).GetType("Parity")!;
        var instance = Activator.CreateInstance(type, new Xui.Window(), true)!;
        T Control<T>(string name) => (T)type.GetProperty(name)!.GetValue(instance)!;
        var check = Control<Xui.CheckBox>("Check");
        var link = Control<Xui.HyperlinkButton>("Link");
        var selector = Control<Xui.SelectorBar>("Selector");
        var badge = Control<Xui.InfoBadge>("Badge");
        var menu = Control<Xui.MenuBar>("Menu");
        Assert(check.Text == "Check" && link.Text == "Open" && selector.Name == "Pages", "Labels and control names bind correctly.");
        Assert(check.ThreeState && check.State == Xui.CheckState.Unchecked && selector.Selected == 1, "Typed initial values are applied.");
        Assert(badge.Kind == Xui.InfoBadgeKind.Count && badge.Count == 3 &&
            Control<Xui.InfoBadge>("Dot").Kind == Xui.InfoBadgeKind.Dot &&
            Control<Xui.InfoBadge>("Icon").Kind == Xui.InfoBadgeKind.Icon, "Badge arguments select the requested presentation.");
        Assert(link.Style is not null && menu.ControlStyle?.Target == Xui.StyleTarget.CommandBar, "Style aliases preserve native targets.");
        Assert(check.ControlStyle?.Target == Xui.StyleTarget.Toggle && selector.ControlStyle?.Target == Xui.StyleTarget.ChoiceList &&
            badge.ControlStyle?.Target == Xui.StyleTarget.InlineStatus, "Parity aliases preserve existing style schemas and parts.");
        check.Invoke(Xui.CheckState.Indeterminate);
        Assert((Xui.CheckState)type.GetProperty("Current")!.GetValue(instance)! == Xui.CheckState.Indeterminate, "Change carries CheckState without reducing it to bool.");
        selector.Select(2);
        Assert(((Xui.Choice[], ulong? Selected))type.GetProperty("Options")!.GetValue(instance)! is { Selected: 2 }, "Selection change carries a stable ID.");
        link.Invoke();
        Assert(selector.Items.Length == 1 && selector.Items[0].Id == 3 && selector.Selected == 3 && badge.Count == 4,
            "Items and selected state refresh atomically after an event.");
        menu.Invoke(11); menu.Invoke(11, true);
        Assert((ulong)type.GetProperty("LastCommand")!.GetValue(instance)! == 111 && (int)type.GetProperty("Changes")!.GetValue(instance)! == 4,
            "Menu invoke and pin handlers remain distinct and typed.");
        var itemSets = selector.ItemSets;
        var refresh = type.GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!;
        refresh.Invoke(instance, null); refresh.Invoke(instance, null);
        Assert(selector.ItemSets == itemSets && menu.CommandSets == 1, "Unchanged arrays do not replace snapshots.");
        context.Unload();
        foreach (var node in new[] {
            """CheckBox("Check", checked: true);""",
            """HyperlinkButton("Link", uri: "https://example.invalid");""",
            """InfoBadge("Badge", count: 1, icon: global::Xui.ButtonIcon.Search);""",
            """MenuBar("Menu", click: Clicked);"""
        })
        {
            Invalid($"component InvalidParity {{ view {{ VStack() {{ {node} }} }} }}");
        }
        foreach (var node in new[] {
            """CheckBox("Check", checkState: true);""",
            """SelectorBar("Pages", items: new string[] { "First" });""",
            """InfoBadge("Badge", count: -1);""",
            """MenuBar("Menu", commands: new global::Xui.Choice[] { new(1, "First") });"""
        })
        {
            var (_, invalid) = Generate(new File(@"C:\fixture\TypedParity.xui", $"component TypedParity {{ view {{ VStack() {{ {node} }} }} }}"));
            Assert(invalid.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error), "Parity arguments enforce their public types.");
        }
    }
}

#pragma once
#include <array>

namespace gallery {
struct Reference {
    const wchar_t* id;
    const wchar_t* usage;
    const wchar_t* exercise;
    const wchar_t* notes;
    const wchar_t* docs;
    const wchar_t* xui;
    const wchar_t* csharp;
    const wchar_t* rust;
};

// Each snippet is independent. C# uses Xui; Rust uses xui::* and a Result-returning function.
// Host code supplies the window, mounts ordinary controls once, and retains callback dependencies.
inline constexpr std::array references{
    Reference{L"forms",
        L"Combine a TextInput, Button, and Toggle for a small form. Keep validation separate from the save action.",
        L"Enter a name. Save the greeting. Clear Allow greeting updates and try Save again.",
        L"TextInput change reports committed text, not IME composition. Property setters do not simulate user edits. The window owns the controls. The excerpts show event connections, not persistent storage.",
        L"docs/specs/controls/basic.md",
        LR"(component Greeting {
    state string Name = "";
    state bool Allowed = true;
    state string Message = "Enter a name";
    view {
        VStack(spacing: 8) {
            TextInput("Your name", text: Name, change: Edit);
            Toggle("Allow greeting updates", checked: Allowed, change: Allow);
            Button("Save greeting", enabled: Allowed && Name.Length > 0, click: Save);
            Text(Message);
        }
    }
    code csharp {
        void Edit(string value) => Name = value;
        void Allow(bool value) => Allowed = value;
        void Save() => Message = "Hello, " + Name;
    }
})",
        LR"(var name = window.TextInput("Your name");
var output = window.Label("Enter a name");
var save = window.Button("Save greeting").SetEnabled(false);
name.Changed += value => save.Enabled = value.Length > 0;
save.Click += () => output.Text = "Hello, " + name.Text;)",
        LR"(let name = window.text_input("Your name")?;
let output = window.label("Enter a name")?;
let input = name.downgrade();
let result = output.downgrade();
name.on_event(move |event| {
    if event.kind == 3 {
        if let (Some(input), Some(result)) = (input.upgrade(), result.upgrade()) {
            result.set_text(&format!("Hello, {}", input.text()?))?;
        }
    }
    Ok(())
})?;)"},
    Reference{L"buttons",
        L"Use Button for an immediate action. Give icon buttons names that describe their actions.",
        L"Invoke Run action with Enter. Clear Enable action. Try the disabled button and the Refresh icon.",
        L"Click runs after a valid activation. Disabled buttons do not invoke their action. Rust supports the action but has no button-icon setter.",
        L"docs/specs/controls/basic.md",
        LR"(component RefreshAction {
    view {
        VStack() {
            Button("Refresh", icon: global::Xui.ButtonIcon.Refresh, click: OnRefresh);
        }
    }
    code csharp {
        void OnRefresh() => System.Console.WriteLine("Refresh requested");
    }
})",
        LR"(var action = window.Button("Refresh").SetIcon(ButtonIcon.Refresh);
action.Click += () => System.Console.WriteLine("Refresh requested");
action.Enabled = true;)",
        LR"(let action = window.button("Refresh")?;
action.on_event(|event| {
    if event.kind == 1 { println!("Refresh requested"); }
    Ok(())
})?;
action.enabled(true)?;)"},
    Reference{L"toggles",
        L"Use Toggle for independent boolean choices. Use RadioGroup when exactly one option must be selected.",
        L"Select both available choices with Space. Try the disabled Unavailable choice.",
        L"SetChecked sets the initial value without a user event. Changed carries the new boolean value. The independent choices do not clear each other.",
        L"docs/specs/controls/basic.md",
        LR"(component UpdateChoice {
    state bool Allowed = true;
    view {
        VStack() {
            Toggle("Allow updates", checked: Allowed, change: Change);
        }
    }
    code csharp { void Change(bool value) => Allowed = value; }
})",
        LR"(var choice = window.Toggle("Allow updates").SetChecked(true);
choice.Changed += value => System.Console.WriteLine(value);)",
        LR"(let choice = window.toggle("Allow updates")?;
choice.checked(true)?;
choice.on_event(|event| {
    if event.kind == 2 { println!("Allowed: {}", event.value != 0); }
    Ok(())
})?;)"},
    Reference{L"text",
        L"Use TextInput for one line of ordinary text. Keep its accessible field name separate from its value.",
        L"Enter Unicode text in Project name. Press Enter. Toggle Show caption and try the 40-unit limit.",
        L"The C++ limit counts UTF-16 units. Native editing preserves IME, selection, clipboard, and undo. C# and Rust lack this maximum-length setter. Rust also lacks placeholder and caption setters.",
        L"docs/specs/controls/basic.md",
        LR"(component ProjectField {
    view {
        VStack() {
            TextInput("Project name", placeholder: "Enter a project", submit: Submit);
        }
    }
    code csharp { void Submit() => System.Console.WriteLine("Submitted"); }
})",
        LR"(var input = window.TextInput("Project name")
    .SetPlaceholder("Enter a project");
input.Submitted += () => System.Console.WriteLine(input.Text);
input.SetCaptionVisible(true);)",
        LR"(let input = window.text_input("Project name")?;
input.on_event(|event| {
    if event.kind == 3 { println!("Submitted"); }
    Ok(())
})?;)"},
    Reference{L"suggestions",
        L"Attach a SuggestionSource to TextInput for bounded completion results. Keep native text input as the editor.",
        L"Type D in Suggested location. Use Down and Enter to accept a suggestion. Try Escape to cancel.",
        L"The demo provider returns four in-memory locations and never reads the filesystem. Providers must honor cancellation. C#, Rust, and .xui expose no SuggestionSource attachment or search-style setter.",
        L"docs/specs/menus-and-input.md",
        L"", L"", L""},
    Reference{L"labels",
        L"Use Label for noneditable headings, captions, and status text. Use semantic tones for meaning, not interaction.",
        L"Select Change heading. Compare the heading, secondary caption, accent text, and error text.",
        L"SetText updates the accessible name and measurement. Labels add no keyboard focus stop. C# and Rust use explicit typography styles, not C++ heading or tone setters.",
        L"docs/specs/controls/basic.md",
        LR"(component Heading {
    style Title for Label {
        part label { fontSize: 24; fontWeight: 600; }
    }
    view { VStack() { Text("Measured heading", style: Title); } }
})",
        LR"(var heading = window.Label("Measured heading");
heading.SetControlStyleValues(StylePart.Label, new PartStyleValues {
    FontSize = 24, FontWeight = 600
});)",
        LR"(let heading = window.label("Measured heading")?;
heading.set_control_style_values(StylePart::Label, PartStyleValues {
    font_size: Some(24.), font_weight: Some(600), ..Default::default()
})?;)"},
    Reference{L"layout",
        L"Use Stack for rows and columns. Assign flex to the child that must receive the remaining space.",
        L"Toggle Use wider spacing. Resize the window and compare the fixed label with the flexible column.",
        L"Each child has one layout parent. Spacing and padding use DIPs. C# and Rust lack the C++ surface flag. Style backgrounds provide explicit colors instead.",
        L"docs/specs/controls/layout.md",
        LR"(component WorkspaceRow {
    view {
        HStack(spacing: 8, padding: 12) {
            Text("Fixed label");
            VStack(flex: 1) { Text("Flexible column"); Text("Second row"); }
        }
    }
})",
        LR"(var row = window.Stack(Axis.Horizontal).Spacing(8).Padding(12);
row.Add(window.Label("Fixed label"));
var column = window.Stack().Add(window.Label("Flexible column"));
row.Add(column, flex: 1);)",
        LR"(let row = window.stack(Axis::Horizontal)?;
row.spacing(8.)?;
row.padding(12.)?;
let column = window.stack(Axis::Vertical)?;
column.add(&*window.label("Flexible column")?, 0.)?;
row.add(&*window.label("Fixed label")?, 0.)?;
row.add(&column, 1.)?;)"},
    Reference{L"scroll",
        L"Use ScrollView when a form exceeds the available height. It owns one content subtree.",
        L"Tab through Workspace preferences until Apply preferences appears. Change a field, then select Reset.",
        L"Keyboard focus reveals hidden fields inside the viewport. The content cannot also belong to another parent. Preferred size is a request, not an absolute layout bound.",
        L"docs/specs/controls/layout.md",
        LR"(component ScrollForm {
    view {
        VStack() {
            ScrollView("Workspace preferences", preferredSize: (480, 160)) {
                VStack(spacing: 12) {
                    TextInput("Workspace name");
                    TextInput("Description");
                    Button("Apply preferences");
                }
            }
        }
    }
})",
        LR"(var form = window.Stack().Spacing(12);
form.Add(window.TextInput("Workspace name"));
form.Add(window.TextInput("Description"));
var scroll = window.ScrollView(form, "Workspace preferences")
    .PreferredSize(480, 160);)",
        LR"(let form = window.stack(Axis::Vertical)?;
form.add(&*window.text_input("Workspace name")?, 0.)?;
form.add(&*window.text_input("Description")?, 0.)?;
let scroll = window.scroll_view(&form, "Workspace preferences")?;
scroll.preferred_size(480., 160.)?;)"},
    Reference{L"files",
        L"Use FileList for a bounded array of file-like rows with stable IDs. Use ItemsView for a custom virtual source.",
        L"Filter the 200 fixture names. Select a row and activate it. No file operation occurs.",
        L"The bindings copy the FileItem array and expose selection events, but not the C++ activation callback. Large arrays can block the UI thread. For .xui, create files with the C# excerpt and pass it to FileRows.",
        L"docs/specs/controls/collections.md",
        LR"(component FileRows {
    param global::Xui.FileList Files;
    view { VStack() { Content(Files); } }
})",
        LR"(var files = window.FileList("Sample files").SetItems([
    new(1, "Notes.txt", ""), new(2, "Archive", "", Directory: true)
]);
files.Filter("Notes");
files.Event += e => {
    if (e.Kind == EventKind.Selection) System.Console.WriteLine("Selection changed");
};)",
        LR"(let files = window.file_list("Sample files")?;
files.set_items(&[
    FileItem { id: 1, name: "Notes.txt", path: "", directory: false },
    FileItem { id: 2, name: "Archive", path: "", directory: true },
])?;
files.filter("Notes")?;)"},
    Reference{L"grid",
        L"Use DataGrid for virtual rows and logical columns. Replace the source to apply an application-defined sort.",
        L"Sort the 100,000 rows. Drag a header boundary. Select Reverse columns and inspect the selected row.",
        L"Source snapshots need stable keys and fast UI-thread callbacks. The excerpts require a same-window ImmutableSource named source. Sort state alone does not sort data. The .xui host calls view.Rows.SetSource(source) after construction.",
        L"docs/specs/controls/collections.md",
        LR"(component Table {
    param global::Xui.GridColumn[] Columns;
    view {
        VStack() {
            DataGrid("Synthetic data", ref: Rows, columns: Columns);
        }
    }
})",
        LR"(var grid = window.DataGrid("Synthetic data");
grid.SetColumns([new("Item", 240), new("Size", 150, Numeric: true)]);
grid.SetSource(source);
grid.SetColumnOrder([1, 0]);)",
        LR"(let grid = window.data_grid("Synthetic data")?;
grid.set_columns(&[
    GridColumn { name: "Item".into(), width: 240., numeric: false, filterable: false, checkable: false },
    GridColumn { name: "Size".into(), width: 150., numeric: true, filterable: false, checkable: false },
])?;
grid.set_source(source)?;
grid.set_column_order(&[1, 0])?;)"},
    Reference{L"tabs",
        L"Use TabStrip for document identities. Handle close requests in application code before replacing the tab list.",
        L"Select Preview. Add a document, close it, and toggle Custom tab colors.",
        L"Selection uses stable IDs, not array positions. The gallery limits its sample to 12 tabs. TabStrip does not create document content. For .xui, create tabs in C# and pass it to Documents.",
        L"docs/specs/controls/layout.md",
        LR"(component Documents {
    param global::Xui.TabStrip Tabs;
    view { VStack() { Content(Tabs); } }
})",
        LR"(var tabs = window.TabStrip("Sample documents");
tabs.SetTabs([new(1, "Notes"), new(2, "Preview")], 1);
tabs.Event += e => {
    if (e.Kind == EventKind.Selection) System.Console.WriteLine(e.Value);
};)",
        LR"(let tabs = window.tab_strip("Sample documents")?;
tabs.set_items(&[
    Choice { id: 1, text: "Notes".into(), enabled: true, version: 0 },
    Choice { id: 2, text: "Preview".into(), enabled: true, version: 0 },
], Some(1))?;)"},
    Reference{L"split",
        L"Use SplitView for two resizable panes. Keep both pane contents alive across compact layouts.",
        L"Drag the divider. Hide the secondary pane. Narrow the window and inspect both native text fields.",
        L"Each pane must have no existing parent. A narrow layout can collapse the secondary pane even when its visibility preference is true. Hidden editors stop accepting input.",
        L"docs/specs/controls/layout.md",
        LR"(component TwoPanes {
    view {
        VStack() {
            SplitView("Sample divider", secondVisible: true) {
                VStack() { TextInput("Primary note"); }
                VStack() { TextInput("Secondary note"); }
            }
        }
    }
})",
        LR"(var first = window.Stack().Add(window.TextInput("Primary note"));
var second = window.Stack().Add(window.TextInput("Secondary note"));
var split = window.SplitView("Sample divider", first, second)
    .SetRatio(0.5).SetSecondVisible(true);)",
        LR"(let first = window.stack(Axis::Vertical)?;
let second = window.stack(Axis::Vertical)?;
first.add(&*window.text_input("Primary note")?, 0.)?;
second.add(&*window.text_input("Secondary note")?, 0.)?;
let split = window.split_view("Sample divider", &first, &second)?;
split.set_ratio(0.5)?;
split.set_second_visible(true)?;)"},
    Reference{L"pages",
        L"Use PageView to switch between existing content trees without reconstructing their controls.",
        L"Enter text in the first field. Select Switch content page twice. Inspect the original field value.",
        L"Page indices are zero-based. Only the active page participates in layout and input. The window retains inactive controls. For .xui, create pages in C# and pass it to ContentPages.",
        L"docs/specs/controls/layout.md",
        LR"(component ContentPages {
    param global::Xui.PageView Pages;
    view { VStack() { Content(Pages); } }
})",
        LR"(var pages = window.PageView("Content pages");
pages.Add(window.TextInput("First page field"));
pages.Add(window.TextInput("Second page field"));
pages.SelectedPage = 1;)",
        LR"(let pages = window.page_view("Content pages")?;
pages.add(&*window.text_input("First page field")?)?;
pages.add(&*window.text_input("Second page field")?)?;
pages.set_selected_page(1)?;)"},
    Reference{L"images",
        L"Use Image for a bounded preview of an application-selected local image.",
        L"Enter a local PNG or JPEG path. Select Load image, then Unload image. Try an invalid path.",
        L"The excerpts require an existing local path. Decode dimensions range from 1 to 1,024 pixels. Bindings expose status, but not reload or detailed decode errors. For .xui, pass the C# image to Preview.",
        L"docs/specs/controls/media.md",
        LR"(component Preview {
    param global::Xui.Image Image;
    view { VStack() { Content(Image, preferredSize: (320, 144)); } }
})",
        LR"(var image = window.Image("Workspace preview");
image.Source(path, 320, 144);
// Call from a later Unload action:
// image.Unload();)",
        LR"(let image = window.image("Workspace preview")?;
image.source(path, 320, 144)?;
// Call from a later Unload action:
// image.unload()?;)"},
    Reference{L"chart",
        L"Use HistoryChart for a short numeric history. Supply samples from application code.",
        L"Select Append sample repeatedly. Select Append gap and inspect the break in the line.",
        L"The chart retains at most 60 samples. It creates no sampler or timer. C# and Rust can append numbers, but lack scale, gap, and history-read APIs. For .xui, pass the C# chart to History.",
        L"docs/specs/controls/collections.md",
        LR"(component History {
    param global::Xui.HistoryChart Chart;
    view { VStack() { Content(Chart); } }
})",
        LR"(var chart = window.HistoryChart("History");
chart.Append(42);
chart.Append(65);)",
        LR"(let chart = window.history_chart("History")?;
chart.append(42.)?;
chart.append(65.)?;)"},
    Reference{L"menus",
        L"Use a native context menu for local actions. Use Window.confirm for a native confirmation instead of a content dialog.",
        L"Focus Context menu target and press Shift+F10. Toggle Show details. Open confirmation and select Cancel.",
        L"Menu shortcut labels do not register shortcuts. The menu closes before its chosen action runs. C# and Rust lack Window.confirm and this Button context-menu provider. .xui cannot add these missing bindings. Collection menu APIs are separate.",
        L"docs/specs/controls/commands.md",
        L"", L"", L""},
    Reference{L"themes",
        L"Use the window theme to keep controls consistent. Use high contrast to inspect focus and accessible content.",
        L"Select each theme. Use Tab, Space, and Enter without a mouse. Compare the native editors and retained controls.",
        L"Theme changes preserve control identities and text. High contrast uses system colors. .xui requires a same-window Host parameter because theme selection belongs to Window, not a control node.",
        L"docs/specs/controls/commands.md",
        LR"(component ThemeActions {
    param global::Xui.Window Host;
    view {
        VStack() { Button("High contrast", click: Contrast); }
    }
    code csharp { void Contrast() => Host.SetTheme(global::Xui.Theme.HighContrast); }
})",
        LR"(window.SetTheme(Theme.Dark);
// Choose another mode from an application action:
window.SetTheme(Theme.HighContrast);)",
        LR"(window.set_theme(Theme::Dark)?;
// Choose another mode from an application action:
window.set_theme(Theme::HighContrast)?;)"},
    Reference{L"radio",
        L"Use RadioGroup for one visible choice from a small set. Give each option a stable ID.",
        L"Use arrow keys to switch between List and Details. Try to select Unavailable view.",
        L"Disabled choices cannot be selected. SetItems establishes the initial selection. Semantic selection produces a selection event. For .xui, create choices in C# and pass it to ViewMode.",
        L"docs/specs/controls/choices.md",
        LR"(component ViewMode {
    param global::Xui.RadioGroup Choices;
    view { VStack() { Content(Choices); } }
})",
        LR"(var choices = window.RadioGroup("View mode");
choices.SetItems([new(10, "List"), new(20, "Details"), new(30, "Unavailable", false)], 10);
choices.Event += e => {
    if (e.Kind == EventKind.Selection) System.Console.WriteLine(e.Value);
};)",
        LR"(let choices = window.radio_group("View mode")?;
choices.set_items(&[
    Choice { id: 10, text: "List".into(), enabled: true, version: 0 },
    Choice { id: 20, text: "Details".into(), enabled: true, version: 0 },
], Some(10))?;)"},
    Reference{L"combo",
        L"Use ComboBox for a compact committed choice. Enable its editor only when free text is also useful.",
        L"Open Output format. Preview Markdown, then press Escape. Repeat with Enter. Type in Editable format.",
        L"Escape keeps the committed ID. Editor text does not invent a selected ID. Bindings report edits through the retained Editor, not a ComboBox edit callback. For .xui, pass the C# format to FormatField.",
        L"docs/specs/controls/choices.md",
        LR"(component FormatField {
    param global::Xui.ComboBox Format;
    view { VStack() { Content(Format); } }
})",
        LR"(var format = window.ComboBox("Editable format", true);
format.SetItems([new(11, "Text"), new(22, "Markdown")], 11);
format.Editor!.Changed += text => System.Console.WriteLine(text);)",
        LR"(let format = window.combo_box("Editable format", true)?;
format.set_items(&[
    Choice { id: 11, text: "Text".into(), enabled: true, version: 0 },
    Choice { id: 22, text: "Markdown".into(), enabled: true, version: 0 },
], Some(11))?;)"},
    Reference{L"popup",
        L"Use Popup for anchored interactive content. Use a tooltip only for noninteractive help.",
        L"Open the retained popup. Edit its native input. Open Nested popup, then dismiss both with Escape.",
        L"Popup content has one owner. Dismissal restores focus to the anchor when possible. Construct the .xui component with attach: false. Host code calls view.Panel.Show(anchor) from a visible anchor action.",
        L"docs/specs/controls/choices.md",
        LR"(component PopupPanel {
    view {
        Popup("Details", ref: Panel) {
            VStack() { TextInput("Popup native input"); }
        }
    }
})",
        LR"(var content = window.Stack().Add(window.TextInput("Popup native input"));
var popup = window.Popup("Details", content);
anchor.Click += () => popup.Show(anchor);)",
        LR"(let content = window.stack(Axis::Vertical)?;
content.add(&*window.text_input("Popup native input")?, 0.)?;
let popup = window.popup("Details", &content)?;
let panel = popup.weak();
let target = anchor.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1 {
        if let (Some(panel), Some(target)) = (panel.upgrade(), target.upgrade()) {
            panel.show(&target)?;
        }
    }
    Ok(())
})?;)"},
    Reference{L"tooltip",
        L"Use help text for a short explanation of an action. Keep required instructions visible instead of hiding them in help.",
        L"Hover over Hover or focus for help. Wait 600 milliseconds. Move away, then focus the action with Tab.",
        L"A tooltip adds no focus stop. Its delay applies to hover and focus help. There is no Tooltip constructor. The .xui host can set a different delay through the Action reference.",
        L"docs/specs/controls/commands.md",
        LR"(component HelpAction {
    view {
        VStack() {
            Button("Refresh", ref: Action, help: "Refresh the local preview.");
        }
    }
})",
        LR"(var action = window.Button("Refresh")
    .Help("Refresh the local preview.")
    .TooltipDelay(600);)",
        LR"(let action = window.button("Refresh")?;
action.help("Refresh the local preview.")?;
action.tooltip_delay(600)?;)"},
    Reference{L"actions",
        L"Use repeat behavior for held actions. Use SplitButton when primary activation and the options menu must remain separate.",
        L"Hold Hold to repeat. Toggle Toggle action. Compare Run primary with Run options.",
        L"Repeat timing uses milliseconds. Release or capture cancellation stops repetition. Bindings lack a silent Button checked-state setter. For .xui, host code configures Repeat.Behavior and RepeatTiming after construction.",
        L"docs/specs/controls/basic.md",
        LR"(component RepeatedAction {
    view { VStack() { Button("Hold to repeat", ref: Repeat); } }
})",
        LR"(var repeat = window.Button("Hold to repeat")
    .Behavior(ButtonBehavior.Repeat).RepeatTiming(400, 80);
var split = window.SplitButton("Run");
split.Primary.Click += () => System.Console.WriteLine("Primary only");
split.Secondary.Click += () => System.Console.WriteLine("Options requested");)",
        LR"(let repeat = window.button("Hold to repeat")?;
repeat.behavior(ButtonBehavior::Repeat)?;
repeat.repeat_timing(400, 80)?;
let split = window.split_button("Run")?;
split.primary()?.on_event(|event| {
    if event.kind == 1 { println!("Primary only"); }
    Ok(())
})?;)"},
    Reference{L"number",
        L"Use NumericInput for exact numeric text and spin actions. Set finite bounds and explicit small and large steps.",
        L"Enter invalid text in Copies. Use Up and Down after a valid value. Try the bounds of 1 and 99.",
        L"Invalid text remains visible and does not replace the committed value. The native editor parses the Windows locale. Bindings lack spin-placement configuration. For .xui, pass the C# number to CopyCount.",
        L"docs/specs/controls/choices.md",
        LR"(component CopyCount {
    param global::Xui.NumericInput Number;
    view { VStack() { Content(Number); } }
})",
        LR"(var number = window.NumericInput("Copies")
    .SetRange(new(1, 99, 1, 10)).SetValue(3);
number.OnChange(value => System.Console.WriteLine(value));)",
        LR"(let number = window.numeric_input("Copies")?;
number.set_range(NumericRange {
    minimum: 1., maximum: 99., small_step: 1., large_step: 10.,
})?;
number.set_value(3.)?;
number.on_change(|value| { println!("{value}"); Ok(()) })?;)"},
    Reference{L"range",
        L"Use RangeInput for spatial adjustment. Use NumericInput when the user must enter an exact value.",
        L"Drag each thumb and compare preview with committed events. Start another drag and press Escape.",
        L"SetValue is silent. OnChange reports committed values. Preview and cancellation are separate event kinds. Values and steps must be finite. For .xui, pass the C# range to ScaleField.",
        L"docs/specs/controls/choices.md",
        LR"(component ScaleField {
    param global::Xui.RangeInput Range;
    view { VStack() { Content(Range); } }
})",
        LR"(var range = window.RangeInput("Scale")
    .SetRange(new(0, 100, 5, 20)).SetValue(40)
    .SetOrientation(Axis.Vertical);
range.OnChange(value => System.Console.WriteLine(value));)",
        LR"(let range = window.range_input("Scale")?;
range.set_range(NumericRange {
    minimum: 0., maximum: 100., small_step: 5., large_step: 20.,
})?;
range.set_value(40.)?;
range.set_orientation(Axis::Vertical)?;
range.on_change(|value| { println!("{value}"); Ok(()) })?;)"},
    Reference{L"disclosure",
        L"Use Expander for optional detail fields. Keep important validation messages outside collapsed content.",
        L"Expand Details. Enter a note and select Keep detail selection. Collapse and expand Details again.",
        L"Collapse retains values, repairs focus, and prevents input in the hidden subtree. The content must have no existing parent. For .xui, pass the C# group to DetailsGroup.",
        L"docs/specs/controls/choices.md",
        LR"(component DetailsGroup {
    param global::Xui.Expander Group;
    view { VStack() { Content(Group); } }
})",
        LR"(var content = window.Stack().Add(window.TextInput("Detail note"));
var group = window.Expander("Details", content).SetExpanded(false);)",
        LR"(let content = window.stack(Axis::Vertical)?;
content.add(&*window.text_input("Detail note")?, 0.)?;
let group = window.expander("Details", &content)?;
group.set_expanded(false)?;)"},
    Reference{L"progress",
        L"Use Progress for read-only completion or capacity. Represent unknown work with an explicit state.",
        L"Select Advance, Indeterminate, and Pause. Compare Sample task with Storage capacity and Unknown capacity.",
        L"Visible, enabled indeterminate indicators animate when Windows permits client-area animation. Hidden indicators stop animation. Capacity remains static; C# and Rust expose SetCapacity/set_capacity with explicit units. For .xui, pass the C# progress to TaskProgress.",
        L"docs/specs/controls/choices.md",
        LR"(component TaskProgress {
    param global::Xui.Progress Progress;
    view { VStack() { Content(Progress); } }
})",
        LR"(var progress = window.Progress("Sample task").SetValue(40);
progress.State = ProgressState.Indeterminate;)",
        LR"(let progress = window.progress("Sample task")?;
progress.set_value(40.)?;
progress.set_state(ProgressState::Indeterminate)?;)"},
    Reference{L"items",
        L"Use ItemsView for a large immutable source. Reuse stable item identities across list, tile, and grouped presentations.",
        L"Choose Tiles and drag a selection rectangle. Enable Show even IDs. Compare Select filtered with Select full source.",
        L"The excerpts require a same-window ImmutableSource named source. Bindings lack full-source selection-scope and group-metadata APIs. Source callbacks run on the UI thread. The .xui host calls view.Items.SetSource(source).SetPresentation(ItemsPresentation.Tiles) after construction.",
        L"docs/specs/controls/collections.md",
        LR"(component ItemTiles {
    view {
        VStack() {
            ItemsView("Items", ref: Items);
        }
    }
})",
        LR"(var items = window.ItemsView("Items").SetSource(source)
    .SetPresentation(ItemsPresentation.Tiles);
items.SelectAll();)",
        LR"(let items = window.items_view("Items")?;
items.set_source(source)?;
items.set_presentation(ItemsPresentation::Tiles)?;
items.select_all()?;)"},
    Reference{L"tree",
        L"Use TreeView for hierarchical data with deferred children. Complete each request with an immutable child snapshot.",
        L"Select Expand first branch. Navigate its 100,000 virtual children. Collapse the branch with Left or the button.",
        L"The excerpts require same-window roots and children snapshots. Roots must report HasChildren. Complete requests on the UI thread. Disposal or drop cancels an unfinished request. For .xui, pass the C# tree to FolderTree.",
        L"docs/specs/controls/collections.md",
        LR"(component FolderTree {
    param global::Xui.TreeView Tree;
    view { VStack() { Content(Tree); } }
})",
        LR"(var tree = window.TreeView("Tree").SetSource(roots);
tree.OnRequest(request => {
    using (request) { request.Complete(children); }
});)",
        LR"(let tree = window.tree_view("Tree")?;
tree.set_source(roots)?;
// Inside the application's UI-thread completion handler:
// request.complete(children, "")?;)"},
    Reference{L"adaptive",
        L"Use Grid for aligned tracks, Wrap for repeated actions, and AdaptiveLayout for width-dependent navigation.",
        L"Enable Compact recipe. Toggle Overlay navigation. Select a wrapped action and resize the window.",
        L"AdaptiveLayout retains the same two children across breakpoints. Hidden navigation cannot accept input. Bindings expose tracks, but set grid gaps through styles. For .xui, pass the C# adaptive element to AdaptivePanels.",
        L"docs/specs/controls/layout.md",
        LR"(component AdaptivePanels {
    param global::Xui.AdaptiveLayout Panels;
    view { VStack() { Content(Panels); } }
})",
        LR"(var nav = window.Stack().Add(window.Button("Home"));
var detail = window.Wrap("Actions").SetItemWidth(110);
detail.Add(window.Button("Action 1")).Add(window.Button("Action 2"));
var adaptive = window.AdaptiveLayout("Workspace", nav, detail)
    .SetBreakpoint(520).SetNavigationExtent(170)
    .SetCompactNavigation(CompactNavigation.Overlay);)",
        LR"(let nav = window.stack(Axis::Vertical)?;
nav.add(&*window.button("Home")?, 0.)?;
let detail = window.wrap("Actions")?;
detail.set_item_width(110.)?;
detail.add(&*window.button("Action 1")?)?;
let adaptive = window.adaptive_layout("Workspace", &nav, &detail)?;
adaptive.set_breakpoint(520.)?;
adaptive.set_compact_navigation(CompactNavigation::Overlay)?;)"},
    Reference{L"grid-extensions",
        L"Use DataGrid filters and checkboxes when row selection must survive column movement and source replacement.",
        L"Select Even IDs, then Select all rows. Reorder columns. Open the Item header filter and clear its text.",
        L"Column indices refer to source columns, not display positions. Bindings store filter state but lack filter-worker completion. The excerpts require a prepared evenRows snapshot. For .xui, pass the C# grid to FilterTable.",
        L"docs/specs/controls/collections.md",
        LR"(component FilterTable {
    param global::Xui.DataGrid Grid;
    view { VStack() { Content(Grid); } }
})",
        LR"(var grid = window.DataGrid("Filterable data");
grid.SetColumns([new("Item", 290, Filterable: true, Checkable: true)]);
grid.SetFilter(0, "even");
grid.SetSource(evenRows);
grid.SelectAll();)",
        LR"(let grid = window.data_grid("Filterable data")?;
grid.set_columns(&[GridColumn {
    name: "Item".into(), width: 290., numeric: false, filterable: true, checkable: true,
}])?;
grid.set_filter(0, "even")?;
grid.set_source(even_rows)?;
grid.select_all()?;)"},
    Reference{L"commands",
        L"Use CommandSurface for searchable actions and CommandBar for frequent actions. Keep command IDs stable across presentations.",
        L"Open the palette. Search for Open sample. Use F2 for its independent pin action. Inspect the toolbar overflow.",
        L"Shortcut hints do not register key bindings. Bindings lack command-query completion and section records. The excerpts show a fixed command set. For .xui, construct CommandPopup with the C# surface and attach: false, then call surface.Show(anchor).",
        L"docs/specs/controls/commands.md",
        LR"(component CommandPopup {
    param global::Xui.CommandSurface Surface;
    view { Content(Surface); }
})",
        LR"(var surface = window.CommandSurface("Commands");
surface.SetCommands([new(1, "Open sample", PinLabel: "Pin")]);
surface.OnCommand((id, pin) => System.Console.WriteLine($"{id}: pin={pin}"));
anchor.Click += () => surface.Show(anchor);)",
        LR"(let surface = window.command_surface("Commands")?;
surface.set_commands(&[Command {
    id: 1, parent: 0, label: "Open sample".into(), kind: CommandKind::Action,
    enabled: true, checked: None, shortcut_hint: String::new(), pin_label: "Pin".into(),
}])?;
surface.on_event(|event| {
    if event.kind == 1 || event.kind == 9 { println!("Command {}", event.value); }
    Ok(())
})?;
// A visible anchor's callback calls surface.show(&anchor).;)"},
    Reference{L"breadcrumb",
        L"Use Breadcrumb for the current location path. Handle navigation requests without treating a canceled picker as a location change.",
        L"Select an earlier segment. Open the overflow. Open Choose location and press Escape.",
        L"Segment identities remain stable when the path changes. Bindings use Choice records for segments. They lack navigation-query completion for custom location providers. For .xui, pass the C# path to LocationPath.",
        L"docs/specs/controls/navigation.md",
        LR"(component LocationPath {
    param global::Xui.Breadcrumb Path;
    view { VStack() { Content(Path); } }
})",
        LR"(var path = window.Breadcrumb("Current location");
path.SetSegments([new(1, "Home", Version: 1), new(2, "Projects", Version: 1)]);
path.Event += e => {
    if (e.Kind == EventKind.Selection) System.Console.WriteLine(e.Value);
};)",
        LR"(let path = window.breadcrumb("Current location")?;
path.set_segments(&[
    Choice { id: 1, version: 1, text: "Home".into(), enabled: true },
    Choice { id: 2, version: 1, text: "Projects".into(), enabled: true },
])?;)"},
    Reference{L"navigation",
        L"Use NavigationPane for grouped quick access. Use LocationPicker for an anchored chooser and ViewPicker for ItemsView presentation settings.",
        L"Select a quick-access row. Open quick access and select another row. Open View settings and change the presentation.",
        L"The excerpts require a same-window cachedLocations snapshot. Bindings support static sources but lack navigation-query completion. Retained children cannot acquire another parent. For .xui, pass the C# pane to QuickAccess.",
        L"docs/specs/controls/navigation.md",
        LR"(component QuickAccess {
    param global::Xui.NavigationPane Pane;
    view { VStack() { Content(Pane); } }
})",
        LR"(var pane = window.NavigationPane("Quick access").SetSource(cachedLocations);
var picker = window.LocationPicker("Choose location");
picker.Navigation.SetSource(cachedLocations);
anchor.Click += () => picker.Show(anchor);)",
        LR"(let pane = window.navigation_pane("Quick access")?;
pane.set_source(cached_locations)?;
let picker = window.location_picker("Choose location")?;
picker.navigation()?.set_source(cached_locations)?;
// A visible anchor's callback calls picker.show(&anchor).;)"},
    Reference{L"shell",
        L"Use native Shell menus when Windows or third-party extensions must own the available file actions.",
        L"Discover synthetic Shell commands. Invoke synthetic inspect. If you intend to inspect a native menu, enter a real path.",
        L"Discovery alone does not invoke a verb. A selected native verb can change files. Third-party calls cannot always cancel. Bindings lack the abstract ShellCommandSession provider. The excerpts require an existing local path and an explicit user action.",
        L"docs/specs/controls/commands.md",
        LR"(component ShellAction {
    param global::Xui.Window Host;
    param string Path;
    view { VStack() { Button("Shell commands", ref: Open, click: Show); } }
    code csharp { void Show() => Host.ShowShellCommands(Open, new[] { Path }); }
})",
        LR"(anchor.Click += () => window.ShowShellCommands(anchor, [path]);)",
        LR"(let owner = window.downgrade();
let target = anchor.downgrade();
let path = path.to_owned();
anchor.on_event(move |event| {
    if event.kind == 1 {
        if let (Some(owner), Some(target)) = (owner.upgrade(), target.upgrade()) {
            owner.show_shell_commands(&target, &[path.as_str()])?;
        }
    }
    Ok(())
})?;)"},
    Reference{L"titlebar",
        L"Choose a custom title bar when document tabs belong in the window caption. Keep native drag and system commands intact.",
        L"Select Preview in the real caption. Drag empty caption space. Open its system menu with a right-click.",
        L"Custom chrome is a window-construction option. Titlebar children belong to the window and cannot be reparented. For .xui, the host creates a custom-titlebar window before it constructs CaptionContent.",
        L"docs/specs/controls/commands.md",
        LR"(component CaptionContent {
    view { VStack() { Text("The host window owns the caption tabs."); } }
})",
        LR"(using var captionWindow = new Window("Workspace", customTitlebar: true);
captionWindow.TitlebarTabs.SetTabs([new(1, "Gallery"), new(2, "Preview")], 1);
// Set content and run this window before leaving its using scope.)",
        LR"(let caption_window = Window::with_titlebar("Workspace", 800., 600.)?;
caption_window.titlebar_tabs()?.set_items(&[
    Choice { id: 1, version: 0, text: "Gallery".into(), enabled: true },
    Choice { id: 2, version: 0, text: "Preview".into(), enabled: true },
], Some(1))?;
// Set content and run this window while its owner remains alive.;)"},
    Reference{L"dialog",
        L"Use ContentDialog for modal form content that needs validation. Use native confirmation for a simple yes-or-no decision.",
        L"Open the dialog. Submit an empty title. Enter a title and save. Reopen the dialog and cancel.",
        L"An invalid title remains visible while focus stays inside the dialog. Bindings use a stored validation message, not a validation callback. For .xui, pass the C# dialog with attach: false, then call dialog.Show(anchor).",
        L"docs/specs/controls/documents.md",
        LR"(component SaveDialog {
    param global::Xui.ContentDialog Dialog;
    view { Content(Dialog); }
})",
        LR"(var editor = window.TextInput("Document title");
var dialog = window.ContentDialog("Save document", editor);
dialog.SetValidationMessage("Enter a document title.");
editor.Changed += text => dialog.SetValidationMessage(
    text.Length == 0 ? "Enter a document title." : "");
anchor.Click += () => dialog.Show(anchor);)",
        LR"(let editor = window.text_input("Document title")?;
let dialog = window.content_dialog("Save document", &editor)?;
dialog.set_validation_message("Enter a document title.")?;
let target = dialog.weak();
let input = editor.downgrade();
editor.on_event(move |event| {
    if event.kind == 2 {
        if let (Some(target), Some(input)) = (target.upgrade(), input.upgrade()) {
            target.set_validation_message(if input.text()?.is_empty() { "Enter a document title." } else { "" })?;
        }
    }
    Ok(())
})?;
// A visible anchor's callback calls dialog.show(&anchor).;)"},
    Reference{L"status",
        L"Use InlineStatus for local feedback with severity, an optional action, and optional dismissal.",
        L"Select Show warning, then Inspect. Dismiss the message. Select Show error to show it again.",
        L"Dismissal does not delete the control. Show restores it. Messages support accessible live announcements. Bindings expose retained action buttons, but lack the C++ SetAction convenience. For .xui, pass the C# status to DocumentStatus.",
        L"docs/specs/controls/documents.md",
        LR"(component DocumentStatus {
    param global::Xui.InlineStatus Status;
    view { VStack() { Content(Status); } }
})",
        LR"(var status = window.InlineStatus("Document status")
    .SetMessage("The document has unsaved changes.", StatusSeverity.Warning)
    .SetDismissible(true);
status.Show();)",
        LR"(let status = window.inline_status("Document status")?;
status.set_message("The document has unsaved changes.", StatusSeverity::Warning)?;
status.set_dismissible(true)?;
status.show()?;)"},
    Reference{L"multiline",
        L"Use MultilineText for plain Unicode documents with paragraphs, native selection, and undo.",
        L"Edit the second paragraph. Use Undo document edit and Redo document edit. Enable Read-only document.",
        L"Windows RichEdit owns editing and composition. MaximumLength bounds document storage. Read-only content still supports selection and copy. Bindings lack the C++ monospace convenience. For .xui, pass the C# notes to NotesEditor.",
        L"docs/specs/controls/documents.md",
        LR"(component NotesEditor {
    param global::Xui.MultilineText Notes;
    view { VStack() { Content(Notes); } }
})",
        LR"(var notes = window.MultilineText("Notes")
    .SetMaximumLength(65536).SetReadOnly(false);
notes.Text = "First paragraph\rSecond paragraph";
// From an Undo action:
// notes.Command(TextCommand.Undo);)",
        LR"(let notes = window.multiline_text("Notes")?;
notes.set_maximum_length(65536)?;
notes.set_document("First paragraph\rSecond paragraph")?;
notes.set_read_only(false)?;
// From an Undo action:
// notes.command(TextCommand::Undo)?;)"},
    Reference{L"password",
        L"Use PasswordInput for secrets. Read the value only inside an explicit authentication boundary.",
        L"Enter a test password only. Toggle Reveal test password. Select Clear password and inspect the event message.",
        L"Never log or publish the secret as an accessible name. WithPassword borrows UTF-8 bytes in bindings. The buffer cannot escape its callback. Bindings expose reveal permission, not the C++ revealed-state setter. For .xui, pass the C# password to SecretField.",
        L"docs/specs/controls/documents.md",
        LR"(component SecretField {
    param global::Xui.PasswordInput Password;
    view { VStack() { Content(Password); } }
})",
        LR"(var password = window.PasswordInput("Test password")
    .SetMaximumLength(256).SetRevealAllowed(false);
password.OnChange(() => System.Console.WriteLine("Password changed; value hidden"));
// Inside authentication: password.WithPassword(AuthenticateUtf8);)",
        LR"(let password = window.password_input("Test password")?;
password.set_maximum_length(256)?;
password.set_reveal_allowed(false)?;
password.on_change(|| { println!("Password changed; value hidden"); Ok(()) })?;
// Inside authentication: password.with_password(authenticate_utf8)?;)"},
    Reference{L"rich-text",
        L"Use RichText for authored styled runs and explicit links. Use MultilineText when plain text is sufficient.",
        L"Select a link and press Ctrl+Enter. Enable Edit rich document. Paste plain Unicode text.",
        L"RichEdit owns selection and editing. Links request an application action rather than automatically opening a browser. Bindings can set runs, but lack the C++ link-target callback. For .xui, pass the C# rich control to StyledDocument.",
        L"docs/specs/controls/documents.md",
        LR"(component StyledDocument {
    param global::Xui.RichText Rich;
    view { VStack() { Content(Rich); } }
})",
        LR"(var rich = window.RichText("Rich document").SetReadOnly(true);
rich.SetRuns([new("Heading\r", Bold: true), new("Native italic text", Italic: true)]);)",
        LR"(let rich = window.rich_text("Rich document")?;
rich.set_read_only(true)?;
rich.set_runs(&[TextRun {
    text: "Heading".into(), bold: true, italic: false,
    underline: false, link: String::new(),
}])?;)"},
    Reference{L"date-time",
        L"Use DateTimePicker for native date, time, or calendar input. Store local Gregorian fields without an implied time zone.",
        L"Change Document date, Document time, and Document calendar. Compare their event messages.",
        L"Windows owns locale formatting and calendar interaction. C# requires DateTimeKind.Unspecified. Bindings lack the C++ minimum and maximum date setters. For .xui, pass the C# date to DocumentDate.",
        L"docs/specs/controls/documents.md",
        LR"(component DocumentDate {
    param global::Xui.DateTimePicker Date;
    view { VStack() { Content(Date); } }
})",
        LR"(var date = window.DateTimePicker("Document date");
date.Value = new System.DateTime(2026, 9, 13, 0, 0, 0, System.DateTimeKind.Unspecified);
var time = window.DateTimePicker("Document time", DateTimePresentation.Time);)",
        LR"(let date = window.date_time_picker("Document date", DateTimePresentation::Date)?;
date.set_value(LocalDateTime {
    year: 2026, month: 9, day: 13, hour: 0, minute: 0, second: 0,
})?;
let time = window.date_time_picker("Document time", DateTimePresentation::Time)?;)"},
    Reference{L"color",
        L"Use ColorPicker for an sRGB color with an independent alpha channel. Keep channel values separate from premultiplied renderer colors.",
        L"Change alpha and inspect the checkerboard preview. Use arrow keys in a channel. Select a swatch.",
        L"Channels range from 0 to 255. The change event carries RGBA values. Bindings expose channel children and default swatches, but lack custom swatch replacement. For .xui, pass the C# color to DocumentColor.",
        L"docs/specs/controls/documents.md",
        LR"(component DocumentColor {
    param global::Xui.ColorPicker Color;
    view { VStack() { Content(Color); } }
})",
        LR"(var color = window.ColorPicker("Document color")
    .SetValue(new(45, 100, 230, 180));
color.Channel(3).OnChange(alpha => System.Console.WriteLine(alpha));)",
        LR"(let color = window.color_picker("Document color")?;
color.set_value(RgbaColor { red: 45, green: 100, blue: 230, alpha: 180 })?;
color.channel(3)?.on_change(|alpha| { println!("{alpha}"); Ok(()) })?;)"},
    Reference{L"vector-canvas",
        L"Use VectorCanvas for a bounded authored scene. Give interactive shapes stable IDs and useful accessible names.",
        L"Select the transformed rectangle with a pointer. Select the ellipse through its named list entry.",
        L"A scene allows 4,096 shapes, 65,536 points, and 256 interactive shapes. Bindings accept point paths, not the C++ rectangle and ellipse helpers. For .xui, pass the C# canvas to Diagram.",
        L"docs/specs/controls/media.md",
        LR"(component Diagram {
    param global::Xui.VectorCanvas Canvas;
    view { VStack() { Content(Canvas); } }
})",
        LR"(var canvas = window.VectorCanvas("Region");
canvas.SetScene([new VectorShape(11,
    [new(20, 20), new(130, 20), new(130, 100), new(20, 100)],
    Name: "Blue rectangle", Closed: true, Interactive: true,
    Fill: new(0.1f, 0.55f, 0.9f))]);)",
        LR"(let canvas = window.vector_canvas("Region")?;
canvas.set_scene(&[VectorShape {
    id: 11, points: vec![ScenePoint { x: 20., y: 20. },
        ScenePoint { x: 130., y: 20. }, ScenePoint { x: 130., y: 100. }],
    name: "Blue triangle".into(), closed: true, interactive: true,
    fill: SceneColor { red: 0.1, green: 0.55, blue: 0.9, alpha: 1. },
    ..Default::default()
}])?;)"},
    Reference{L"map",
        L"Use MapView for authored coordinates, markers, pan, and zoom. It is not a street-map service.",
        L"Pan across the two markers near the date line. Use Map zoom in, Map zoom out, and Map reset.",
        L"The map uses a Mercator graticule without network requests. Bindings support markers and overlay request tokens, but not C++ overlay polylines. For .xui, pass the C# map to CoordinateMap.",
        L"docs/specs/controls/media.md",
        LR"(component CoordinateMap {
    param global::Xui.MapView Map;
    view { VStack() { Content(Map); } }
})",
        LR"(var map = window.MapView("Authored locations");
map.SetView(new(5, 179), 2);
map.SetMarkers([new(101, new(0, 179), "East marker")]);)",
        LR"(let map = window.map_view("Authored locations")?;
map.set_view(GeoPoint { latitude: 5., longitude: 179. }, 2.)?;
map.set_markers(&[MapMarker {
    id: 101, location: GeoPoint { latitude: 0., longitude: 179. },
    name: "East marker".into(),
}])?;)"},
    Reference{L"media-playback",
        L"Use MediaPlayback for explicit local audio or video. Keep load and play as separate user actions.",
        L"Load the owned tone or video. Select Play, Pause, and Stop. Change volume, then unload the media.",
        L"The excerpts require an existing owned local file. Network URLs and UNC paths are rejected. Hiding unloads the native host. Bindings lack position and duration queries. For .xui, pass the C# media to Player.",
        L"docs/specs/controls/media.md",
        LR"(component Player {
    param global::Xui.MediaPlayback Media;
    view { VStack() { Content(Media); } }
})",
        LR"(var media = window.MediaPlayback("Local media").SetVolume(0.5);
anchor.Click += () => media.LoadLocal(path);
// A separate Play action calls media.Play() after State becomes Ready.)",
        LR"(let media = window.media_playback("Local media")?;
media.set_volume(0.5)?;
let player = media.weak();
let path = path.to_owned();
anchor.on_event(move |event| {
    if event.kind == 1 {
        if let Some(player) = player.upgrade() { player.load_local(&path)?; }
    }
    Ok(())
})?;
// A separate Play action calls play() after state() becomes Ready.;)"},
    Reference{L"web-content",
        L"Use WebContent only when an embedded browser is required. Supply owned HTML and an application-owned profile directory.",
        L"Load owned HTML. Change its DOM, then select Read DOM. Try Focus web, Reload web, and Unload web.",
        L"WebView2 requires the opt-in native build and an installed runtime. The excerpts require an owned profileRoot directory. External origins are denied by default. Hiding unloads the engine. For .xui, pass the C# web control to WebPreview.",
        L"docs/specs/controls/media.md",
        LR"(component WebPreview {
    param global::Xui.WebContent Web;
    view { VStack() { Content(Web); } }
})",
        LR"(var web = window.WebContent("Owned HTML").SetProfileRoot(profileRoot);
anchor.Click += () => web.SetHtml("<h1>Owned preview</h1>");
// Check Window.WebContentEnabled before offering browser actions.)",
        LR"(let web = window.web_content("Owned HTML")?;
web.set_profile_root(profile_root)?;
let preview = web.weak();
anchor.on_event(move |event| {
    if event.kind == 1 {
        if let Some(preview) = preview.upgrade() { preview.set_html("<h1>Owned preview</h1>")?; }
    }
    Ok(())
})?;
// Check Window::web_content_enabled() before offering browser actions.;)"},
    Reference{L"navigation-view",
        L"Use NavigationView for a searchable application hierarchy. Keep navigation selection separate from page activation.",
        L"Select Filter reports. Expand Projects and Reports. Reset the filter, then collapse the pane with its menu button.",
        L"C# uses unversioned IDs and lacks header/footer placement, pane-width setters, and a direct filter setter. Rust cannot supply NavigationView entries. The .xui host calls view.Navigation.SetItems(records) after construction. The native search field filters user input.",
        L"docs/specs/controls/navigation.md",
        LR"(component WorkspaceNavigation {
    view {
        VStack() { NavigationView("Workspace", ref: Navigation); }
    }
})",
        LR"(var nav = window.NavigationView("Workspace").SetItems([
    new(1, "Projects", Selectable: false),
    new(2, "Reports", Parent: 1),
    new(3, "Project notes", Parent: 1)
]);
nav.Select(2);
nav.Event += e => {
    if (e.Kind == EventKind.Selection) System.Console.WriteLine(e.Value);
};)",
        L""},
    Reference{L"miller-columns",
        L"Use MillerColumns to browse a hierarchy while retaining the selected path. Replace the column snapshot after each selection.",
        L"Select folders to add columns. Select a document to remove later columns. Show deep path and try horizontal and vertical scrolling.",
        L"Columns use immutable sources and versioned keys. The C# excerpt requires a same-window roots snapshot. Sources report HasChildren for folder arrows. Rust exposes construction only, without path, source, or event APIs. For .xui, pass the C# columns to FolderColumns.",
        L"docs/specs/controls/collections.md",
        LR"(component FolderColumns {
    param global::Xui.MillerColumns Columns;
    view { VStack() { Content(Columns); } }
})",
        LR"(var columns = window.MillerColumns("Project library").SetColumnWidth(200);
columns.SetColumns([new("Projects", roots)]);
columns.SelectionChanged += item =>
    System.Console.WriteLine($"Column {item.Column}, item {item.Key.Id}");
// Replace the complete path with SetColumns after resolving children.
// The maximum path contains 32 columns.)",
        L""},
    Reference{L"toggle-switch",
        L"Use ToggleSwitch for an immediate on/off preference. Use CheckBox for a choice that can have a mixed state.",
        L"Change Send notifications with Space. Disable the switch and try it again. Compare the unavailable preference.",
        L"Checked is binary. Programmatic setters are silent; user changes report a boolean. The switch shares Toggle styles and accessibility semantics.",
        L"docs/specs/controls/basic.md",
        LR"(component NotificationPreference {
    state bool Enabled = true;
    view { VStack() { ToggleSwitch("Send notifications", checked: Enabled, change: Changed); } }
    code csharp { void Changed(bool value) => Enabled = value; }
})",
        LR"(var notifications = window.ToggleSwitch("Send notifications").SetChecked(true);
notifications.Changed += value => System.Console.WriteLine(value);)",
        LR"(let notifications = window.toggle_switch("Send notifications")?;
notifications.set_checked(true)?;
notifications.on_change(|value| { println!("{value}"); Ok(()) })?;)"},
    Reference{L"toggle-button",
        L"Use ToggleButton for a persistent on/off action with button presentation, such as a pinned preview.",
        L"Toggle Pin preview with Space or Enter. Disable the action and confirm that its checked state does not change.",
        L"ToggleButton reports checked-state changes, not Click. C# exposes Toggled and the Changed alias; .xui uses change. It shares Button styles.",
        L"docs/specs/controls/basic.md",
        LR"(component PinnedPreview {
    state bool Pinned = false;
    view { VStack() { ToggleButton("Pin preview", checked: Pinned, change: Changed); } }
    code csharp { void Changed(bool value) => Pinned = value; }
})",
        LR"(var pin = window.ToggleButton("Pin preview").SetChecked(false);
pin.Toggled += value => System.Console.WriteLine(value);)",
        LR"(let pin = window.toggle_button("Pin preview")?;
pin.set_checked(false)?;
pin.on_toggle(|value| { println!("{value}"); Ok(()) })?;)"},
    Reference{L"progress-ring",
        L"Use ProgressRing for compact circular task progress. Its initial state is indeterminate and it has no visible caption.",
        L"Select Advance, Indeterminate, Pause, and Error. Clear Show indicator to hide the ring and stop its animation.",
        L"Use a separate Label for visible explanatory text. Determinate ranges are read-only. Animation respects visibility, enabled state, and the Windows animation preference.",
        L"docs/specs/controls/choices.md",
        LR"(component PreviewProgress {
    view { VStack() { ProgressRing("Load preview", progressState: global::Xui.ProgressState.Indeterminate); } }
})",
        LR"(var ring = window.ProgressRing("Load preview");
ring.SetState(ProgressState.Determinate).SetValue(40);)",
        LR"(let ring = window.progress_ring("Load preview")?;
ring.set_state(ProgressState::Determinate)?;
ring.set_value(40.)?;)"},
    Reference{L"checkbox",
        L"Use CheckBox for unchecked, checked, or mixed values. Enable three-state input when users must choose all three states.",
        L"Cycle Include attachments from its mixed state. Change the three-state option, disable the checkbox, and set mixed state explicitly.",
        L"Programmatic mixed state is valid even when three-state input is disabled. Setters are silent. Space activates the checkbox; Enter does not. Existing Toggle remains binary.",
        L"docs/specs/controls/basic.md",
        LR"(component AttachmentChoice {
    state global::Xui.CheckState Selection = global::Xui.CheckState.Indeterminate;
    view { VStack() { CheckBox("Include attachments", threeState: true, checkState: Selection, change: Changed); } }
    code csharp { void Changed(global::Xui.CheckState value) => Selection = value; }
})",
        LR"(var check = window.CheckBox("Include attachments")
    .SetThreeState(true).SetState(CheckState.Indeterminate);
check.Changed += value => System.Console.WriteLine(value);)",
        LR"(let check = window.check_box("Include attachments")?;
check.set_three_state(true)?;
check.set_state(CheckState::Indeterminate)?;
check.on_change(|value| { println!("{value:?}"); Ok(()) })?;)"},
    Reference{L"hyperlink-button",
        L"Use HyperlinkButton for an application action that needs link presentation and Hyperlink accessibility semantics.",
        L"Activate Learn about this sample. Disable the help link and try it again. The example does not open a browser.",
        L"Enter, Space, and UIA Invoke run the explicit callback. There is no URI property or automatic navigation. The application owns any external action.",
        L"docs/specs/controls/basic.md",
        LR"(component SampleHelp {
    view { VStack() { HyperlinkButton("Learn more", click: ShowHelp); } }
    code csharp { void ShowHelp() => System.Console.WriteLine("Help requested"); }
})",
        LR"(var help = window.HyperlinkButton("Learn more");
help.Click += () => System.Console.WriteLine("Help requested");)",
        LR"(let help = window.hyperlink_button("Learn more")?;
help.on_click(|| { println!("Help requested"); Ok(()) })?;)"},
    Reference{L"selector-bar",
        L"Use SelectorBar for a compact, horizontal set of exclusive choices with stable IDs and one keyboard focus stop.",
        L"Select All, Active, and Completed. Use the arrow keys and try the disabled Archived choice. Reset the filter.",
        L"SetItems replaces items and selection atomically. SetSelected is silent; Select reports a user change. IDs must name enabled items. Omitted selection preserves an enabled selection or chooses the first enabled item.",
        L"docs/specs/controls/choices.md",
        LR"(component TaskFilter {
    view {
        VStack() {
            SelectorBar("Task filter", items: new global::Xui.Choice[] {
                new(1, "All"), new(2, "Active"), new(3, "Completed")
            }, selected: 1UL, change: Changed);
        }
    }
    code csharp { void Changed(ulong id) => System.Console.WriteLine(id); }
})",
        LR"(var filter = window.SelectorBar("Task filter").SetItems([
    new(1, "All"), new(2, "Active"), new(3, "Completed")
], 1);
filter.Changed += id => System.Console.WriteLine(id);)",
        LR"(let filter = window.selector_bar("Task filter")?;
filter.set_items(&[
    Choice { id: 1, label: "All".into(), enabled: true },
    Choice { id: 2, label: "Active".into(), enabled: true },
    Choice { id: 3, label: "Completed".into(), enabled: true },
], Some(1))?;
filter.on_change(|id| { println!("{id}"); Ok(()) })?;)"},
    Reference{L"info-badge",
        L"Use InfoBadge for a compact dot, notification count, or icon. It describes status without an interactive focus target.",
        L"Add a notification, then choose Show dot or Show icon. Reset the count to return to the initial count presentation.",
        L"Counts above 99 display as 99+ while the stored value and accessible name retain the exact count. The default presentation is a dot. There is no activation event.",
        L"docs/specs/controls/basic.md",
        LR"(component NotificationCount {
    view { VStack() { InfoBadge("Unread notifications", count: 7U); } }
})",
        LR"(var badge = window.InfoBadge("Unread notifications").SetCount(7);
// Later updates can select another presentation:
badge.SetDot();
badge.SetIcon(ButtonIcon.Bookmark);)",
        LR"(let badge = window.info_badge("Unread notifications")?;
badge.set_count(7)?;
badge.set_dot()?;
badge.set_icon(ButtonIcon::Bookmark)?;)"},
    Reference{L"menu-bar",
        L"Use MenuBar for persistent submenu headings backed by an immutable command snapshot. The window manages popup behavior.",
        L"Press F10 or Alt+F. Open Recent samples, navigate between headings, and dismiss with Escape. Publish and Cut are disabled.",
        L"Root commands must be submenu groups. Command labels can contain access-key markers. The framework owns heading activation, nested popups, and focus restoration. The sample actions do not write files or change the clipboard.",
        L"docs/specs/controls/commands.md",
        LR"(component DocumentMenu {
    view {
        VStack() {
            MenuBar("Document menu", commands: new global::Xui.Command[] {
                new(1, "&File", Kind: global::Xui.CommandKind.Submenu),
                new(2, "Show summary", Parent: 1)
            }, invoke: Execute);
        }
    }
    code csharp { void Execute(ulong id) => System.Console.WriteLine(id); }
})",
        LR"(var menu = window.MenuBar("Document menu").SetCommands([
    new(1, "&File", Kind: CommandKind.Submenu),
    new(2, "Show summary", Parent: 1)
]);
menu.Invoked += id => System.Console.WriteLine(id);)",
        LR"(let menu = window.menu_bar("Document menu")?;
menu.set_commands(&[
    Command { id: 1, parent: 0, label: "&File".into(), kind: CommandKind::Submenu,
        enabled: true, checked: None, shortcut_hint: String::new(), pin_label: String::new() },
    Command { id: 2, parent: 1, label: "Show summary".into(), kind: CommandKind::Action,
        enabled: true, checked: None, shortcut_hint: String::new(), pin_label: String::new() },
])?;
menu.on_invoke(|id| { println!("{id}"); Ok(()) })?;)"}
};
}

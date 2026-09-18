namespace Xui;

public enum Axis { Horizontal, Vertical }
public class Window
{
    private readonly int thread = Environment.CurrentManagedThreadId;
    public readonly List<Element> Elements = [];
    public void VerifyAccess()
    {
        if (thread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("UI thread required.");
    }
    public Stack? Content;
    public int ContentSets;
    private T Add<T>(T element) where T : Element { element.Owner = this; Elements.Add(element); return element; }
    public Stack Stack(Axis axis) => Add(new Stack());
    public Label Label(string text) => Add(new Label());
    public Button Button(string text) => Add(new Button());
    public Toggle Toggle(string text) => Add(new Toggle());
    public ToggleSwitch ToggleSwitch(string text) => Add(new ToggleSwitch());
    public ToggleButton ToggleButton(string text) => Add(new ToggleButton());
    public TextInput TextInput(string text) => Add(new TextInput());
    public RangeInput RangeInput(string text) => Add(new RangeInput());
    public Progress Progress(string text) => Add(new Progress());
    public ProgressRing ProgressRing(string text) => Add(new ProgressRing());
    public CheckBox CheckBox(string text) => Add(new CheckBox());
    public HyperlinkButton HyperlinkButton(string text) => Add(new HyperlinkButton());
    public SelectorBar SelectorBar(string text) => Add(new SelectorBar());
    public InfoBadge InfoBadge(string text) => Add(new InfoBadge());
    public MenuBar MenuBar(string text) => Add(new MenuBar());
    public SwapChainPanel SwapChainPanel(string text) => Add(new SwapChainPanel());
    public Grid Grid(string name) => Add(new Grid { Name = name });
    public DataGrid DataGrid(string name) => Add(new DataGrid());
    public ItemsView ItemsView(string name) => Add(new ItemsView());
    public NavigationView NavigationView(string name) => Add(new NavigationView());
    public ScrollView ScrollView(Element content, string name)
    { var result = Add(new ScrollView()); result.AddContent(content); return result; }
    public Popup Popup(string name, Element content)
    { var result = Add(new Popup()); result.AddContent(content); return result; }
    public SplitView SplitView(string name, Element first, Element second)
    { var result = Add(new SplitView()); result.AddContent(first); result.AddContent(second); return result; }
    public void SetContent(Stack root) { root.Claim(this); Content = root; ContentSets++; }
}
public abstract class Element
{
    public ControlStyle? ControlStyle { get; private set; }
    public int ControlStyleSets { get; private set; }
    public readonly Dictionary<StylePart, PartStyleValues> Locals = [];
    public Element SetControlStyle(ControlStyle? value) { ControlStyle = value; ControlStyleSets++; return this; }
    public Element SetControlStyleValues(StylePart part, PartStyleValues values) { Locals[part] = values; return this; }
    public (float Width, float Height) Size;
    public int SizeSets;
    public (float Width, float Height) Preferred;
    public Window Owner = null!;
    private bool owned;
    internal void Claim(Window window)
    {
        if (!ReferenceEquals(window, Owner)) throw new ArgumentException("Elements belong to different windows.");
        if (owned) throw new ArgumentException("Element already has a parent.");
        owned = true;
    }
}
public static class ElementExtensions
{
    public static T FixedSize<T>(T element, float width, float height) where T : Element
    { element.Size = (width, height); element.SizeSets++; return element; }
    public static T PreferredSize<T>(T element, float width, float height) where T : Element
    { element.Preferred = (width, height); return element; }
}
public static class ControlFeatures
{
    public static T Help<T>(T control, string text) where T : Control
    { control.HelpText = text; return control; }
    public static T Visible<T>(T control, bool value) where T : Control
    { control.IsVisible = value; return control; }
}
public class Stack : Element
{
    public int SpacingSets, PaddingSets;
    public float CurrentSpacing, CurrentPadding;
    public void Spacing(float value) { SpacingSets++; CurrentSpacing = value; }
    public void Padding(float value) { PaddingSets++; CurrentPadding = value; }
    public readonly List<(Element Child, float Flex)> Children = [];
    public void Add(Element child, float flex = 0) { child.Claim(Owner); Children.Add((child, flex)); }
}
public abstract class Control : Element
{
    private string text = "";
    public int TextSets;
    public string Text { get => text; set { text = value; TextSets++; } }
    public virtual string Name { get => Text; set => Text = value; }
    public string AutomationId { get; set; } = "";
    public bool Enabled { get; set; }
    public string HelpText { get; set; } = "";
    public bool IsVisible = true;
}
public sealed class Label : Control;
public sealed class SwapChainPanel : Control;
public class Button : Control
{
    private ButtonStyle? style;
    public int StyleSets;
    public ButtonStyle? Style { get => style; set { style = value; StyleSets++; } }
    public ButtonStyleValues StyleValues { get; set; } = new();
    public ButtonIcon Icon;
    public Button SetIcon(ButtonIcon value) { Icon = value; return this; }
    public event Action? Click;
    public void Invoke() => Click?.Invoke();
}
public readonly record struct ThemeColor(uint Light, uint Dark)
{
    public ThemeColor(uint uniform) : this(uniform, uniform) { }
}
public readonly record struct Insets(float Left, float Top, float Right, float Bottom)
{
    public Insets(float uniform) : this(uniform, uniform, uniform, uniform) { }
}
public sealed record ButtonStyleValues
{
    public ThemeColor? Background { get; init; }
    public ThemeColor? Foreground { get; init; }
    public ThemeColor? BorderBrush { get; init; }
    public float? CornerRadius { get; init; }
    public Insets? BorderThickness { get; init; }
    public Insets? Padding { get; init; }
}
public enum ButtonStyleState { Focused, Checked, Hovered, Pressed, Disabled }
public sealed record ButtonStyleRule(ButtonStyleState State, ButtonStyleValues Values);
public sealed class ButtonStyle(ButtonStyleValues values, IReadOnlyList<ButtonStyleRule>? rules = null, ButtonStyle? basedOn = null)
{
    public ButtonStyleValues Values { get; } = values;
    public IReadOnlyList<ButtonStyleRule> Rules { get; } = rules ?? [];
    public ButtonStyle? BasedOn { get; } = basedOn;
}
public class Toggle : Control
{
    public int StyleSets => ControlStyleSets;
    public ControlStyle? Style { get => ControlStyle; set => SetControlStyle(value); }
    public Toggle SetStyleValues(StylePart part, PartStyleValues values) { SetControlStyleValues(part, values); return this; }
    public bool Checked { get; set; }
    public event Action<bool>? Changed;
    public void Invoke(bool value) { Checked = value; Changed?.Invoke(value); }
}
public enum StyleFontStyle : uint { Normal, Italic, Oblique }
public sealed class ToggleSwitch : Toggle;
public sealed class ToggleButton : Button
{
    public bool Checked { get; set; }
    public event Action<bool>? Changed;
    public void Invoke(bool value) { Checked = value; Changed?.Invoke(value); }
}
public enum StyleAlignment : uint { Start, Center, End, Stretch }
public sealed record PartStyleValues
{
    public ThemeColor? Background { get; init; }
    public ThemeColor? Foreground { get; init; }
    public ThemeColor? BorderBrush { get; init; }
    public float? CornerRadius { get; init; }
    public float? Size { get; init; }
    public Insets? BorderThickness { get; init; }
    public Insets? Padding { get; init; }
    public string? FontFamily { get; init; }
    public float? FontSize { get; init; }
    public uint? FontWeight { get; init; }
    public StyleFontStyle? FontStyle { get; init; }
    public StyleAlignment? HorizontalAlignment { get; init; }
    public StyleAlignment? VerticalAlignment { get; init; }
    public float? Spacing { get; init; }
    public float? RowHeight { get; init; }
    public float? HeaderHeight { get; init; }
    public float? Indentation { get; init; }
    public float? Thickness { get; init; }
    public float? Width { get; init; }
    public float? Height { get; init; }
    public float? RowGap { get; init; }
    public float? ColumnGap { get; init; }
    public uint? MaximumLines { get; init; }
    public bool? Wrapping { get; init; }
}
public sealed record PartStyle(StylePart Part, PartStyleValues Values);
public sealed record ControlStyleRule(StylePart Part, StyleState State, PartStyleValues Values);
public sealed class ControlStyle(StyleTarget target, IReadOnlyList<PartStyle> parts,
    IReadOnlyList<ControlStyleRule>? rules = null, ControlStyle? basedOn = null)
{
    public StyleTarget Target { get; } = target;
    public IReadOnlyList<PartStyle> Parts { get; } = parts;
    public IReadOnlyList<ControlStyleRule> Rules { get; } = rules ?? [];
    public ControlStyle? BasedOn { get; } = basedOn;
}
public sealed class TextInput : Control
{
    public bool CaptionVisible = true;
    public string Placeholder = "";
    public TextInput SetCaptionVisible(bool value) { CaptionVisible = value; return this; }
    public TextInput SetPlaceholder(string value) { Placeholder = value; return this; }
    public override string Name { get; set; } = "";
    public event Action<string>? Changed;
    public event Action? Submitted;
    public void Edit(string value) { Text = value; Changed?.Invoke(value); }
    public void Submit() => Submitted?.Invoke();
}
public enum ButtonIcon { None, Back, Forward, Up, Refresh, Search }
public enum PopupPlacement { Below, Above, Right, Left, Center, BelowCenter }
public enum TrackSizing { Fixed, Automatic, Star }
public readonly record struct GridTrack(TrackSizing Sizing = TrackSizing.Star, float Value = 1, float Minimum = 0, float Maximum = float.MaxValue);
public readonly record struct GridColumn(string Name, float Width = 120, bool Numeric = false, bool Filterable = false, bool Checkable = false);
public sealed class Grid : Element
{
    public string Name = "";
    public GridTrack[] Rows = [], Columns = [];
    public int TrackSets;
    public readonly List<(Element Child, uint Row, uint Column, uint RowSpan, uint ColumnSpan)> Children = [];
    public Grid SetTracks(ReadOnlySpan<GridTrack> rows, ReadOnlySpan<GridTrack> columns)
    { Rows = rows.ToArray(); Columns = columns.ToArray(); TrackSets++; return this; }
    public Grid Add(Element child, uint row = 0, uint column = 0, uint rowSpan = 1, uint columnSpan = 1)
    {
        if (rowSpan == 0 || columnSpan == 0 || row + rowSpan > Rows.Length || column + columnSpan > Columns.Length)
            throw new ArgumentException("Grid cell exceeds tracks");
        child.Claim(Owner);
        Children.Add((child, row, column, rowSpan, columnSpan));
        return this;
    }
}
public sealed class DataGrid : Control
{
    public GridColumn[] Columns = [];
    public int ColumnSets;
    public DataGrid SetColumns(ReadOnlySpan<GridColumn> columns) { Columns = columns.ToArray(); ColumnSets++; return this; }
}
public sealed class NavigationView : Control
{
    private TextInput? search;
    public TextInput Search => search ??= Owner.TextInput("Search");
    public bool HeaderVisible = true;
    public NavigationView SetHeaderVisible(bool value) { HeaderVisible = value; return this; }
}
public sealed class ItemsView : Control;
public readonly record struct NumericRange(double Minimum, double Maximum, double SmallStep = 1, double LargeStep = 10);
public enum ProgressState { Determinate, Indeterminate, Paused, Error, Unknown }
public abstract class ValueControl : Control
{
    public NumericRange Range = new(0, 100);
    public double Value;
    public int RangeSets, ValueSets;
    public void SetRange(NumericRange range)
    {
        if (!double.IsFinite(range.Minimum) || !double.IsFinite(range.Maximum) || range.Minimum >= range.Maximum ||
            !double.IsFinite(range.Maximum - range.Minimum) || !double.IsFinite(range.SmallStep) || range.SmallStep <= 0 ||
            !double.IsFinite(range.LargeStep) || range.LargeStep <= 0)
            throw new ArgumentOutOfRangeException(nameof(range));
        Range = range;
        Value = Math.Clamp(Value, range.Minimum, range.Maximum);
        RangeSets++;
    }
    public void SetValue(double value)
    {
        if (!double.IsFinite(value) || value < Range.Minimum || value > Range.Maximum)
            throw new ArgumentOutOfRangeException(nameof(value));
        Value = value;
        ValueSets++;
    }
}
public sealed class RangeInput : ValueControl
{
    private event Action<double>? changed;
    public int Subscriptions;
    public Axis Orientation;
    public bool Reversed;
    public void SetOrientation(Axis value) => Orientation = value;
    public void SetReversed(bool value) => Reversed = value;
    public void OnChange(Action<double> callback) { changed += callback; Subscriptions++; }
    public void ChangeValue(double value) { SetValue(value); changed?.Invoke(value); }
}
public sealed class Progress : ValueControl
{
    public ProgressState State;
    public void SetState(ProgressState value) => State = value;
}
public sealed class ProgressRing : ValueControl
{
    public ProgressState State = ProgressState.Indeterminate;
    public void SetState(ProgressState value) => State = value;
}
public enum CheckState { Unchecked, Checked, Indeterminate }
public enum InfoBadgeKind { Dot, Count, Icon }
public sealed class CheckBox : Control
{
    public CheckState State;
    public bool ThreeState;
    public void SetState(CheckState value) => State = value;
    public void SetThreeState(bool value) => ThreeState = value;
    public event Action<CheckState>? Changed;
    public void Invoke(CheckState value) { State = value; Changed?.Invoke(value); }
}
public sealed class HyperlinkButton : Button;
public readonly record struct Choice(ulong Id, string Text, bool Enabled = true, ulong Version = 0);
public sealed class SelectorBar : Control
{
    public Choice[] Items = [];
    public ulong? Selected;
    public int ItemSets;
    public void SetItems(ReadOnlySpan<Choice> items, ulong? selected = null)
    {
        var snapshot = items.ToArray();
        if (snapshot.Any(item => item.Id == 0) || snapshot.Select(item => item.Id).Distinct().Count() != snapshot.Length)
            throw new ArgumentException("Choice IDs must be nonzero and unique.");
        if (!selected.HasValue)
            selected = snapshot.Any(item => item.Id == Selected && item.Enabled) ? Selected :
                snapshot.Where(item => item.Enabled).Select(item => (ulong?)item.Id).FirstOrDefault();
        if (selected.HasValue && !snapshot.Any(item => item.Id == selected && item.Enabled))
            throw new ArgumentException("Selected item must exist and be enabled.");
        Items = snapshot; Selected = selected; ItemSets++;
    }
    public void SetSelected(ulong selected) => SetItems(Items, selected);
    public event Action<ulong>? Changed;
    public void Select(ulong id) { SetSelected(id); Changed?.Invoke(id); }
}
public sealed class InfoBadge : Control
{
    public InfoBadgeKind Kind;
    public uint Count;
    public ButtonIcon Icon;
    public void SetCount(uint value) { Count = value; Kind = InfoBadgeKind.Count; }
    public void SetIcon(ButtonIcon value) { Icon = value; Kind = InfoBadgeKind.Icon; }
}
public enum CommandKind : uint { Action, Submenu, Separator }
public readonly record struct Command(ulong Id, string Label, ulong Parent = 0, CommandKind Kind = CommandKind.Action,
    bool Enabled = true, bool? Checked = null, string ShortcutHint = "", string PinLabel = "");
public sealed class MenuBar : Control
{
    public Command[] Commands = [];
    public int CommandSets;
    public void SetCommands(ReadOnlySpan<Command> commands) { Commands = commands.ToArray(); CommandSets++; }
    public event Action<ulong>? Invoked;
    public event Action<ulong>? Pinned;
    public void Invoke(ulong id, bool pin = false) { if (pin) Pinned?.Invoke(id); else Invoked?.Invoke(id); }
}
public abstract class ContentControl : Control
{
    public readonly List<Element> Children = [];
    public void AddContent(Element child) { child.Claim(Owner); Children.Add(child); }
}
public sealed class ScrollView : ContentControl;
public sealed class Popup : ContentControl
{
    public PopupPlacement Placement;
    public bool WindowBackground;
    public Popup SetPlacement(PopupPlacement value) { Placement = value; return this; }
    public Popup SetWindowBackground(bool value) { WindowBackground = value; return this; }
}
public sealed class SplitView : ContentControl
{
    public bool SecondVisible = true;
    public SplitView SetSecondVisible(bool value) { SecondVisible = value; return this; }
}

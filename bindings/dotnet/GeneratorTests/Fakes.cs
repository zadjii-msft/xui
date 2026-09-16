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
    public TextInput TextInput(string text) => Add(new TextInput());
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
public sealed class Button : Control
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
public sealed class Toggle : Control
{
    private ControlStyle? style;
    public int StyleSets;
    public ControlStyle? Style { get => style; set { style = value; StyleSets++; } }
    public readonly Dictionary<StylePart, PartStyleValues> Locals = [];
    public Toggle SetStyleValues(StylePart part, PartStyleValues values) { Locals[part] = values; return this; }
    public bool Checked { get; set; }
    public event Action<bool>? Changed;
    public void Invoke(bool value) { Checked = value; Changed?.Invoke(value); }
}
public enum StyleTarget { Toggle }
public enum StylePart { Root, Label, Indicator, Mark }
public enum StyleState : ulong { Focused = 1, Checked = 2, Hovered = 4, Pressed = 8, Disabled = 16 }
public sealed record PartStyleValues
{
    public ThemeColor? Background { get; init; }
    public ThemeColor? Foreground { get; init; }
    public ThemeColor? BorderBrush { get; init; }
    public float? CornerRadius { get; init; }
    public float? Size { get; init; }
    public Insets? BorderThickness { get; init; }
    public Insets? Padding { get; init; }
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
public enum PopupPlacement { Below, Above, Right, Left, Center }
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

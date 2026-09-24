using System.Collections.ObjectModel;

namespace Xui.Experimental.Portable;

public abstract class Element
{
    private readonly List<Element> children = [];
    private readonly ReadOnlyCollection<Element> childView;
    private Element? parent;
    private float flex;
    private Size? fixedSize;
    private Size? preferredSize;
    private (AxisConstraints? Width, AxisConstraints? Height) constraints;
    private GridPlacement? cell;
    internal bool Disposed { get; private set; }
    internal ComponentLifetime? ComponentLifetime { get; set; }
    internal Host Owner { get; }
    public ElementKind Kind { get; }
    public Element? Parent { get { VerifyAccess(); return parent; } }
    public IReadOnlyList<Element> Children { get { VerifyAccess(); return childView; } }
    public float Flex { get { VerifyAccess(); return flex; } }
    public Size? FixedSize { get { VerifyAccess(); return fixedSize; } }
    public Size? PreferredSize { get { VerifyAccess(); return preferredSize; } }
    public AxisConstraints? WidthConstraints => Read(constraints).Width;
    public AxisConstraints? HeightConstraints => Read(constraints).Height;
    public GridPlacement? Cell => Read(cell);

    private protected Element(Host owner, ElementKind kind)
    {
        Owner = owner;
        Kind = kind;
        childView = children.AsReadOnly();
    }

    protected void VerifyAccess()
    {
        Owner.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
    }

    protected T Read<T>(T value) { VerifyAccess(); return value; }

    protected void Set<T>(ref T field, T value, ElementProperty property)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        if (EqualityComparer<T>.Default.Equals(field, value)) return;
        field = value;
        Owner.Update(this, property);
    }

    internal void AddChild(Element child, float weight)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(child);
        child.VerifyAccess();
        Owner.VerifyBuilding();
        Owner.VerifyBuildElement(this);
        Owner.VerifyBuildElement(child);
        Values.Length(weight);
        if (child is Reveal && weight != 0) throw new NotSupportedException("Reveal cannot consume a flex share; size its retained child instead.");
        if (child.Owner != Owner) throw new InvalidOperationException("A child must belong to the same host.");
        if (child.parent is not null) throw new InvalidOperationException("The child already has a parent.");
        for (Element? ancestor = this; ancestor is not null; ancestor = ancestor.parent)
            if (ancestor == child) throw new InvalidOperationException("A tree cannot contain a cycle.");
        children.Add(child);
        child.parent = this;
        child.flex = weight;
    }

    internal void ReplaceChildren(IReadOnlyList<Element> next)
    {
        foreach (var child in children) child.parent = null;
        children.Clear();
        foreach (var child in next)
        {
            child.parent = this;
            children.Add(child);
        }
    }

    internal void SetGridPlacement(GridPlacement placement) => cell = placement;

    internal void SetFixedSize(float width, float height)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        if (this is Reveal) throw new NotSupportedException("Reveal has no outer fixed size; size its retained child instead.");
        Set(ref fixedSize, Values.Size(width, height), ElementProperty.FixedSize);
    }
    internal void SetPreferredSize(float width, float height)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        if (this is Reveal) throw new NotSupportedException("Reveal has no outer preferred size; size its retained child instead.");
        Set(ref preferredSize, Values.Size(width, height), ElementProperty.PreferredSize);
    }

    public Element SetConstraints(AxisConstraints? width, AxisConstraints? height)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        width?.Validate();
        height?.Validate();
        if (this is Reveal && (width.HasValue || height.HasValue))
            throw new NotSupportedException("Reveal has no outer axis constraints; size its retained child instead.");
        if (width.HasValue || height.HasValue) Owner.VerifyConstraintsSupport(this);
        Set(ref constraints, (width, height), ElementProperty.Constraints);
        return this;
    }

    public Element SetWidth(AxisConstraints? width) => SetConstraints(width, HeightConstraints);
    public Element SetHeight(AxisConstraints? height) => SetConstraints(WidthConstraints, height);

    internal bool AcceptsInput()
    {
        for (Element? element = this; element is not null; element = element.parent)
        {
            if (element is Control control && (!control.Enabled || !control.Visible)) return false;
            if (element.parent is PageView pages && !pages.Presents(element)) return false;
            if (element is Reveal reveal && !reveal.Open) return false;
        }
        return true;
    }

    internal virtual void Release() { Disposed = true; }
}

public class Stack : Element
{
    private float spacing;
    private float padding;
    public Axis Axis { get; }
    public float SpacingValue => Read(spacing);
    public float PaddingValue => Read(padding);

    internal Stack(Host owner, Axis axis, ElementKind kind = ElementKind.Stack) : base(owner, kind) { Axis = axis; }
    public Stack Spacing(float value) { Set(ref spacing, Values.Length(value), ElementProperty.Spacing); return this; }
    public Stack Padding(float value) { Set(ref padding, Values.Length(value), ElementProperty.Padding); return this; }
    public Stack Add(Element child, float flex = 0)
    {
        if (this is KeyedStack) throw new InvalidOperationException("Use Reconcile to manage keyed children.");
        AddChild(child, flex);
        return this;
    }
}

public abstract class Control : Element
{
    private string name;
    private string automationId = "";
    private string help = "";
    private bool enabled = true;
    private bool visible = true;
    private Typography? typography;
    public string Name
    {
        get => Read(name);
        set
        {
            VerifyAccess();
            Owner.VerifyElementMutation(this);
            if (this is Label label) label.TextLayout?.ValidateText(value);
            Set(ref name, Values.Text(value), ElementProperty.Name);
        }
    }
    public string AutomationId { get => Read(automationId); set => Set(ref automationId, Values.Text(value), ElementProperty.AutomationId); }
    public string Help { get => Read(help); set => Set(ref help, Values.Text(value), ElementProperty.Help); }
    public bool Enabled
    {
        get => Read(enabled);
        set { VerifyAccess(); Owner.VerifyElementMutation(this); if (!value) Owner.ResetRangePreviews(this); Set(ref enabled, value, ElementProperty.Enabled); }
    }
    public bool Visible
    {
        get => Read(visible);
        set { VerifyAccess(); Owner.VerifyElementMutation(this); if (!value) Owner.ResetRangePreviews(this); Set(ref visible, value, ElementProperty.Visible); }
    }
    public Typography? Typography
    {
        get => Read(typography);
        set
        {
            VerifyAccess();
            Owner.VerifyElementMutation(this);
            if (typography == value) return;
            if (value is not null && Kind is not (ElementKind.Label or ElementKind.Button or ElementKind.TextInput or ElementKind.Toggle or ElementKind.CheckBox))
                throw new NotSupportedException("Typography requires a supported text-bearing control.");
            Owner.VerifyTypographySupport(this, value);
            Set(ref typography, value, ElementProperty.Typography);
        }
    }
    public Control SetTypography(Typography? value) { Typography = value; return this; }

    private protected Control(Host owner, ElementKind kind, string name) : base(owner, kind) { this.name = Values.Text(name); }
}

public sealed class Label : Control
{
    private LabelTextLayout? textLayout;
    public string Text { get => Name; set => Name = value; }
    public LabelTextLayout? TextLayout
    {
        get => Read(textLayout);
        set
        {
            VerifyAccess();
            Owner.VerifyElementMutation(this);
            if (textLayout == value) return;
            value?.ValidateText(Name);
            Owner.VerifyTextLayoutSupport(this, value);
            Set(ref textLayout, value, ElementProperty.TextLayout);
        }
    }
    public Label SetTextLayout(LabelTextLayout? value) { TextLayout = value; return this; }
    internal Label(Host owner, string text) : base(owner, ElementKind.Label, text) { }
}

public sealed class Button : Control
{
    private Action? click;
    public string Text { get => Name; set => Name = value; }
    public event Action Click
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); click += value; }
        remove { Owner.VerifyEventRemoval(this); click -= value; }
    }
    internal Button(Host owner, string text) : base(owner, ElementKind.Button, text) { }
    internal void RaiseClick() => click?.Invoke();
    internal override void Release() { click = null; base.Release(); }
}

public sealed class TextInput : Control
{
    public InputPurpose Purpose { get; }
    private string text = "";
    private string placeholder = "";
    private bool captionVisible = true;
    private Action<string>? changed;
    private Action? submitted;
    private TextInteraction? interaction;
    private Action<TextInteraction>? interactionChanged;
    public string Text { get => Read(text); set => Set(ref text, Values.Text(value), ElementProperty.Text); }
    public string Placeholder => Read(placeholder);
    public bool CaptionVisible => Read(captionVisible);
    public TextInteraction? Interaction { get { VerifyAccess(); return Owner.IsAttached ? interaction : null; } }
    public event Action<TextInteraction> InteractionChanged
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); interactionChanged += value; }
        remove { Owner.VerifyEventRemoval(this); interactionChanged -= value; }
    }
    public event Action<string> Changed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); changed += value; }
        remove { Owner.VerifyEventRemoval(this); changed -= value; }
    }
    public event Action Submitted
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); submitted += value; }
        remove { Owner.VerifyEventRemoval(this); submitted -= value; }
    }
    internal TextInput(Host owner, string name, InputPurpose purpose = InputPurpose.Normal) : base(owner, ElementKind.TextInput, name) { Purpose = purpose; }
    public void SetCaptionVisible(bool value) => Set(ref captionVisible, value, ElementProperty.CaptionVisible);
    public void SetPlaceholder(string value) => Set(ref placeholder, Values.Text(value), ElementProperty.Placeholder);
    internal void RaiseChange(string value)
    {
        Values.Text(value);
        if (text == value) return;
        // The peer already contains this user edit. Do not write it back and disturb selection or IME.
        text = value;
        changed?.Invoke(value);
    }
    internal void RaiseSubmit() => submitted?.Invoke();
    internal bool CaptureInteraction(TextInteraction value)
    {
        if (interaction == value) return false;
        interaction = value;
        return true;
    }
    internal void NotifyInteraction(TextInteraction value) => interactionChanged?.Invoke(value);
    internal void ResetInteraction() => interaction = null;
    internal override void Release() { changed = null; submitted = null; interactionChanged = null; interaction = null; base.Release(); }
}

public sealed class ScrollView : Control
{
    internal ScrollView(Host owner, string name) : base(owner, ElementKind.ScrollView, name) { }
}

public static class ControlFeatures
{
    public static void Visible(Control control, bool value) => control.Visible = value;
    public static void Help(Control control, string value) => control.Help = value;
}

public static class ElementExtensions
{
    public static void FixedSize(Element element, float width, float height) => element.SetFixedSize(width, height);
    public static void PreferredSize(Element element, float width, float height) => element.SetPreferredSize(width, height);
}

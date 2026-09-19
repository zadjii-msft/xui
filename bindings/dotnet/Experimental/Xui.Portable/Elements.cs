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
    internal bool Disposed { get; private set; }
    internal Host Owner { get; }
    public ElementKind Kind { get; }
    public Element? Parent { get { VerifyAccess(); return parent; } }
    public IReadOnlyList<Element> Children { get { VerifyAccess(); return childView; } }
    public float Flex { get { VerifyAccess(); return flex; } }
    public Size? FixedSize { get { VerifyAccess(); return fixedSize; } }
    public Size? PreferredSize { get { VerifyAccess(); return preferredSize; } }

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
        Owner.VerifyMutation();
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
        Values.Length(weight);
        if (child.Owner != Owner) throw new InvalidOperationException("A child must belong to the same host.");
        if (child.parent is not null) throw new InvalidOperationException("The child already has a parent.");
        for (Element? ancestor = this; ancestor is not null; ancestor = ancestor.parent)
            if (ancestor == child) throw new InvalidOperationException("A tree cannot contain a cycle.");
        children.Add(child);
        child.parent = this;
        child.flex = weight;
    }

    internal void SetFixedSize(float width, float height) =>
        Set(ref fixedSize, Values.Size(width, height), ElementProperty.FixedSize);
    internal void SetPreferredSize(float width, float height) =>
        Set(ref preferredSize, Values.Size(width, height), ElementProperty.PreferredSize);

    internal bool AcceptsInput()
    {
        for (Element? element = this; element is not null; element = element.parent)
            if (element is Control control && (!control.Enabled || !control.Visible)) return false;
        return true;
    }

    internal virtual void Release() { Disposed = true; }
}

public sealed class Stack : Element
{
    private float spacing;
    private float padding;
    public Axis Axis { get; }
    public float SpacingValue => Read(spacing);
    public float PaddingValue => Read(padding);

    internal Stack(Host owner, Axis axis) : base(owner, ElementKind.Stack) { Axis = axis; }
    public Stack Spacing(float value) { Set(ref spacing, Values.Length(value), ElementProperty.Spacing); return this; }
    public Stack Padding(float value) { Set(ref padding, Values.Length(value), ElementProperty.Padding); return this; }
    public Stack Add(Element child, float flex = 0) { AddChild(child, flex); return this; }
}

public abstract class Control : Element
{
    private string name;
    private string automationId = "";
    private string help = "";
    private bool enabled = true;
    private bool visible = true;
    public string Name { get => Read(name); set => Set(ref name, Values.Text(value), ElementProperty.Name); }
    public string AutomationId { get => Read(automationId); set => Set(ref automationId, Values.Text(value), ElementProperty.AutomationId); }
    public string Help { get => Read(help); set => Set(ref help, Values.Text(value), ElementProperty.Help); }
    public bool Enabled { get => Read(enabled); set => Set(ref enabled, value, ElementProperty.Enabled); }
    public bool Visible { get => Read(visible); set => Set(ref visible, value, ElementProperty.Visible); }

    private protected Control(Host owner, ElementKind kind, string name) : base(owner, kind) { this.name = Values.Text(name); }
}

public sealed class Label : Control
{
    public string Text { get => Name; set => Name = value; }
    internal Label(Host owner, string text) : base(owner, ElementKind.Label, text) { }
}

public sealed class Button : Control
{
    private Action? click;
    public string Text { get => Name; set => Name = value; }
    public event Action Click
    {
        add { VerifyAccess(); Owner.VerifyMutation(); click += value; }
        remove { VerifyAccess(); Owner.VerifyMutation(); click -= value; }
    }
    internal Button(Host owner, string text) : base(owner, ElementKind.Button, text) { }
    internal void RaiseClick() => click?.Invoke();
    internal override void Release() { click = null; base.Release(); }
}

public sealed class TextInput : Control
{
    private string text = "";
    private string placeholder = "";
    private bool captionVisible = true;
    private Action<string>? changed;
    private Action? submitted;
    public string Text { get => Read(text); set => Set(ref text, Values.Text(value), ElementProperty.Text); }
    public string Placeholder => Read(placeholder);
    public bool CaptionVisible => Read(captionVisible);
    public event Action<string> Changed
    {
        add { VerifyAccess(); Owner.VerifyMutation(); changed += value; }
        remove { VerifyAccess(); Owner.VerifyMutation(); changed -= value; }
    }
    public event Action Submitted
    {
        add { VerifyAccess(); Owner.VerifyMutation(); submitted += value; }
        remove { VerifyAccess(); Owner.VerifyMutation(); submitted -= value; }
    }
    internal TextInput(Host owner, string name) : base(owner, ElementKind.TextInput, name) { }
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
    internal override void Release() { changed = null; submitted = null; base.Release(); }
}

public sealed class ScrollView : Control
{
    internal ScrollView(Host owner, Element content, string name) : base(owner, ElementKind.ScrollView, name) => AddChild(content, 0);
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

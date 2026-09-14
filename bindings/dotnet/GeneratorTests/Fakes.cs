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
    private T Add<T>(T element) where T : Element { Elements.Add(element); return element; }
    public Stack Stack(Axis axis) => Add(new Stack());
    public Label Label(string text) => Add(new Label());
    public Button Button(string text) => Add(new Button());
    public Toggle Toggle(string text) => Add(new Toggle());
    public TextInput TextInput(string text) => Add(new TextInput());
    public void SetContent(Stack root) { }
}
public abstract class Element;
public class Stack : Element
{
    public int SpacingSets, PaddingSets;
    public float CurrentSpacing, CurrentPadding;
    public void Spacing(float value) { SpacingSets++; CurrentSpacing = value; }
    public void Padding(float value) { PaddingSets++; CurrentPadding = value; }
    public void Add(Element child) { }
}
public abstract class Control : Element
{
    private string text = "";
    public int TextSets;
    public string Text { get => text; set { text = value; TextSets++; } }
    public string Name { get; set; } = "";
    public string AutomationId { get; set; } = "";
    public bool Enabled { get; set; }
}
public sealed class Label : Control;
public sealed class Button : Control
{
    public event Action? Click;
    public void Invoke() => Click?.Invoke();
}
public sealed class Toggle : Control
{
    public bool Checked { get; set; }
    public event Action<bool>? Changed;
    public void Invoke(bool value) { Checked = value; Changed?.Invoke(value); }
}
public sealed class TextInput : Control
{
    public event Action<string>? Changed;
    public event Action? Submitted;
    public void Edit(string value) { Text = value; Changed?.Invoke(value); }
    public void Submit() => Submitted?.Invoke();
}

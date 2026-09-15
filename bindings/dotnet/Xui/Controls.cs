namespace Xui;

public abstract class Element
{
    internal Window Window { get; }
    internal ulong Handle { get; }
    private protected Element(Window window, ulong handle) { Window = window; Handle = handle; }
    internal void BelongsTo(Window window)
    {
        Window.Guard();
        if (!ReferenceEquals(window, Window)) throw new ArgumentException("Elements belong to different windows.");
    }
}
public static class ElementExtensions
{
    public static T FixedSize<T>(this T element, float width, float height) where T : Element
    { element.Window.Update(new Property(element, PropertyKind.FixedSize, A: width, B: height)); return element; }
    public static T MinimumSize<T>(this T element, float width, float height) where T : Element
    { element.Window.Update(new Property(element, PropertyKind.MinimumSize, A: width, B: height)); return element; }
    public static T MaximumSize<T>(this T element, float width, float height) where T : Element
    { element.Window.Update(new Property(element, PropertyKind.MaximumSize, A: width, B: height)); return element; }
    public static T PreferredSize<T>(this T element, float width, float height) where T : Element
    { element.Window.Update(new Property(element, PropertyKind.PreferredSize, A: width, B: height)); return element; }
    public static T AutoSize<T>(this T element, bool value) where T : Element
    { element.Window.Update(new Property(element, PropertyKind.AutoSize, Integer: value ? 1u : 0u)); return element; }
}
public static class ControlExtensions
{
    public static T Focus<T>(this T control, bool selectAll = false) where T : Control
    { control.Window.Guard(); control.Window.Check(Native.Focus(control.Handle, selectAll ? 1u : 0u)); return control; }
    public static T SetName<T>(this T control, string value) where T : Control
    { control.Name = value; return control; }
    public static T SetAutomationId<T>(this T control, string value) where T : Control
    { control.AutomationId = value; return control; }
    public static T SetEnabled<T>(this T control, bool value) where T : Control
    { control.Enabled = value; return control; }
    public static T SetText<T>(this T control, string value) where T : Control
    {
        // Document controls hide Control.Text and require their document setter.
        switch (control)
        {
            case MultilineText document: document.Text = value; break;
            case RichText document: document.Text = value; break;
            default: control.Text = value; break;
        }
        return control;
    }
}
public sealed class Stack : Element
{
    internal Stack(Window window, ulong handle) : base(window, handle) { }
    public Stack Add(Element child, float flex = 0)
    {
        Window.Guard(); child.BelongsTo(Window); Window.Check(Native.StackAdd(Handle, child.Handle, flex));
        return this;
    }
    public Stack Spacing(float value) { Window.Update(new Property(this, PropertyKind.Spacing, A: value)); return this; }
    public Stack Padding(float value) { Window.Update(new Property(this, PropertyKind.Padding, A: value, B: value, C: value, D: value)); return this; }
}
public abstract unsafe class Control : Element
{
    private Action<UiEvent>? handlers;
    private Action? focusEntered;
    public ulong Id => Handle;
    public bool Focused => Features.Get(this, 42).First != 0;
    private void OnFocusEvent(UiEvent e) { if (e.Kind == EventKind.FocusEntered) focusEntered?.Invoke(); }
    public event Action FocusEntered
    {
        add { Window.Guard(); if (focusEntered is null) Event += OnFocusEvent; focusEntered += value; }
        remove { Window.Guard(); focusEntered -= value; if (focusEntered is null) Event -= OnFocusEvent; }
    }
    private protected Control(Window window, ulong handle) : base(window, handle) { }
    public string Name { set => Window.Update(new Property(this, PropertyKind.Name, value)); }
    public string AutomationId { set => Window.Update(new Property(this, PropertyKind.AutomationId, value)); }
    public bool Enabled { set => Window.Update(new Property(this, PropertyKind.Enabled, Integer: value ? 1u : 0u)); }
    public string Text
    {
        get
        {
            Window.Guard();
            int status = Native.TextCopy(Handle, null, 0, out uint count);
            if (status != 6) Window.Check(status);
            var bytes = new byte[count];
            fixed (byte* p = bytes) Window.Check(Native.TextCopy(Handle, p, count, out _));
            return Window.Encoding.GetString(bytes);
        }
        set => Window.Update(new Property(this, PropertyKind.Text, value));
    }
    public event Action<UiEvent> Event
    {
        add { Window.SetSubscription(Handle, e => handlers?.Invoke(e)); handlers += value; }
        remove { Window.Guard(); handlers -= value; if (handlers is null) Window.SetSubscription(Handle, null); }
    }
}
public sealed class Label : Control { internal Label(Window w, ulong h) : base(w, h) { } }
public sealed class Button : Control
{
    internal Button(Window w, ulong h) : base(w, h) { }
    public ButtonIcon Icon { get => (ButtonIcon)Features.Get(this, 45).First; set => Features.Set(this, 45, first: (uint)value); }
    public Button SetIcon(ButtonIcon value) { Icon = value; return this; }
    private Action? clicked;
    private void OnEvent(UiEvent e) { if (e.Kind == EventKind.Click) clicked?.Invoke(); }
    public event Action Click
    {
        add { Window.Guard(); if (clicked is null) Event += OnEvent; clicked += value; }
        remove { Window.Guard(); clicked -= value; if (clicked is null) Event -= OnEvent; }
    }
    public void Invoke() { Window.Guard(); Window.Check(Native.Invoke(Handle)); }
}
public sealed class Toggle : Control
{
    internal Toggle(Window w, ulong h) : base(w, h) { }
    private Action<bool>? changed;
    private void OnEvent(UiEvent e) { if (e.Kind == EventKind.Change) changed?.Invoke(e.Value != 0); }
    public event Action<bool> Changed
    {
        add { Window.Guard(); if (changed is null) Event += OnEvent; changed += value; }
        remove { Window.Guard(); changed -= value; if (changed is null) Event -= OnEvent; }
    }
    public bool Checked { set => Window.Update(new Property(this, PropertyKind.Checked, Integer: value ? 1u : 0u)); }
    public Toggle SetChecked(bool value) { Checked = value; return this; }
    public void Invoke() { Window.Guard(); Window.Check(Native.Invoke(Handle)); }
}
public sealed partial class TextInput : Control
{
    internal TextInput(Window w, ulong h) : base(w, h) { }
    private Action<string>? changed;
    private Action? submitted;
    private void OnChange(UiEvent e) { if (e.Kind == EventKind.Change) changed?.Invoke(Text); }
    private void OnSubmit(UiEvent e) { if (e.Kind == EventKind.Submit) submitted?.Invoke(); }
    public event Action<string> Changed
    {
        add { Window.Guard(); if (changed is null) Event += OnChange; changed += value; }
        remove { Window.Guard(); changed -= value; if (changed is null) Event -= OnChange; }
    }
    public event Action Submitted
    {
        add { Window.Guard(); if (submitted is null) Event += OnSubmit; submitted += value; }
        remove { Window.Guard(); submitted -= value; if (submitted is null) Event -= OnSubmit; }
    }
}
public sealed class ScrollView : Control
{
    internal ScrollView(Window w, ulong h) : base(w, h) { }
    public float Offset { set => Window.Update(new Property(this, PropertyKind.ScrollOffset, A: value)); }
    public ScrollView SetOffset(float value) { Offset = value; return this; }
}
public enum ImageStatus : uint { Empty, Loading, Ready, Error }
public sealed unsafe class Image : Control
{
    internal Image(Window w, ulong h) : base(w, h) { }
    public Image Source(string path, uint width = 192, uint height = 144)
    {
        Window.Guard(); var bytes = Window.Utf8(path);
        fixed (byte* p = bytes) Window.Check(Native.ImageSource(Handle, Window.Span(p, bytes), width, height));
        return this;
    }
    public Image Unload() => Source("");
    public ImageStatus Status
    {
        get { Window.Guard(); Window.Check(Native.ImageState(Handle, out uint value)); return (ImageStatus)value; }
    }
}
public readonly record struct FileItem(ulong Id, string Name, string Path, bool Directory = false);
public sealed unsafe class FileList : Control
{
    internal FileList(Window w, ulong h) : base(w, h) { }
    public FileList SetItems(ReadOnlySpan<FileItem> items)
    {
        Window.Guard();
        if (items.Length > 1000000) throw new ArgumentOutOfRangeException(nameof(items));
        var rows = new Native.Item[items.Length];
        using var pins = new Window.Pins();
        for (int i = 0; i < items.Length; ++i)
            rows[i] = new()
            {
                Size = (uint)sizeof(Native.Item),
                Id = items[i].Id,
                Directory = items[i].Directory ? 1u : 0u,
                Name = pins.Text(items[i].Name),
                Path = pins.Text(items[i].Path)
            };
        fixed (Native.Item* p = rows) Window.Check(Native.ListItems(Handle, p, (uint)rows.Length));
        return this;
    }
    public FileList Filter(string query)
    {
        Window.Guard(); var bytes = Window.Utf8(query);
        fixed (byte* p = bytes) Window.Check(Native.ListFilter(Handle, Window.Span(p, bytes)));
        return this;
    }
    public FileList Select(uint? index) { Window.Guard(); Window.Check(Native.ListSelect(Handle, index ?? uint.MaxValue)); return this; }
    public (uint Count, ulong? SelectedId) State
    {
        get
        {
            Window.Guard(); Window.Check(Native.ListState(Handle, out uint count, out ulong id, out uint selected));
            return (count, selected != 0 ? id : null);
        }
    }
}

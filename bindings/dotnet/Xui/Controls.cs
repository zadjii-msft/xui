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
    public void FixedSize(float width, float height) => Window.Update(new Property(this, PropertyKind.FixedSize, A: width, B: height));
    public void MinimumSize(float width, float height) => Window.Update(new Property(this, PropertyKind.MinimumSize, A: width, B: height));
    public void MaximumSize(float width, float height) => Window.Update(new Property(this, PropertyKind.MaximumSize, A: width, B: height));
    public void PreferredSize(float width, float height) => Window.Update(new Property(this, PropertyKind.PreferredSize, A: width, B: height));
    public void AutoSize(bool value) => Window.Update(new Property(this, PropertyKind.AutoSize, Integer: value ? 1u : 0u));
}
public sealed class Stack : Element
{
    internal Stack(Window window, ulong handle) : base(window, handle) { }
    public void Add(Element child, float flex = 0)
    {
        Window.Guard(); child.BelongsTo(Window); Window.Check(Native.StackAdd(Handle, child.Handle, flex));
    }
    public void Spacing(float value) => Window.Update(new Property(this, PropertyKind.Spacing, A: value));
    public void Padding(float value) => Window.Update(new Property(this, PropertyKind.Padding, A: value, B: value, C: value, D: value));
}
public abstract unsafe class Control : Element
{
    private Action<UiEvent>? handlers;
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
    public void Focus(bool selectAll = false) { Window.Guard(); Window.Check(Native.Focus(Handle, selectAll ? 1u : 0u)); }
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
    private Action? clicked;
    private void OnEvent(UiEvent _) => clicked?.Invoke();
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
    private void OnEvent(UiEvent e) => changed?.Invoke(e.Value != 0);
    public event Action<bool> Changed
    {
        add { Window.Guard(); if (changed is null) Event += OnEvent; changed += value; }
        remove { Window.Guard(); changed -= value; if (changed is null) Event -= OnEvent; }
    }
    public bool Checked { set => Window.Update(new Property(this, PropertyKind.Checked, Integer: value ? 1u : 0u)); }
    public void Invoke() { Window.Guard(); Window.Check(Native.Invoke(Handle)); }
}
public sealed class TextInput : Control
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
}
public enum ImageStatus : uint { Empty, Loading, Ready, Error }
public sealed unsafe class Image : Control
{
    internal Image(Window w, ulong h) : base(w, h) { }
    public void Source(string path, uint width = 192, uint height = 144)
    {
        Window.Guard(); var bytes = Window.Utf8(path);
        fixed (byte* p = bytes) Window.Check(Native.ImageSource(Handle, Window.Span(p, bytes), width, height));
    }
    public void Unload() => Source("");
    public ImageStatus Status
    {
        get { Window.Guard(); Window.Check(Native.ImageState(Handle, out uint value)); return (ImageStatus)value; }
    }
}
public readonly record struct FileItem(ulong Id, string Name, string Path, bool Directory = false);
public sealed unsafe class FileList : Control
{
    internal FileList(Window w, ulong h) : base(w, h) { }
    public void SetItems(ReadOnlySpan<FileItem> items)
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
    }
    public void Filter(string query)
    {
        Window.Guard(); var bytes = Window.Utf8(query);
        fixed (byte* p = bytes) Window.Check(Native.ListFilter(Handle, Window.Span(p, bytes)));
    }
    public void Select(uint? index) { Window.Guard(); Window.Check(Native.ListSelect(Handle, index ?? uint.MaxValue)); }
    public (uint Count, ulong? SelectedId) State
    {
        get
        {
            Window.Guard(); Window.Check(Native.ListState(Handle, out uint count, out ulong id, out uint selected));
            return (count, selected != 0 ? id : null);
        }
    }
}

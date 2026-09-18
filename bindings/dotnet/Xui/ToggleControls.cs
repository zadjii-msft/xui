namespace Xui;

public sealed partial class ToggleSwitch
{
    private Action<bool>? changed;
    private void OnChange(UiEvent e) { if (e.Kind == EventKind.Change) changed?.Invoke(e.Value != 0); }
    public event Action<bool> Changed
    {
        add { Window.Guard(); if (changed is null) Event += OnChange; changed += value; }
        remove { Window.Guard(); changed -= value; if (changed is null) Event -= OnChange; }
    }
    public void Invoke() { Window.Guard(); Window.Check(Native.Invoke(Handle)); }
    public ControlStyle? Style { set => SetControlStyle(value); }
    public ToggleSwitch SetStyle(ControlStyle? value) { SetControlStyle(value); return this; }
    public ToggleSwitch SetStyleValues(StylePart part, PartStyleValues values) { SetControlStyleValues(part, values); return this; }
    public PartStyleValues GetStyleValues(StylePart part, bool effective = false) => GetControlStyleValues(part, effective);
}

public sealed partial class ToggleButton
{
    private Action<bool>? toggled;
    private void OnToggle(UiEvent e) { if (e.Kind == EventKind.Change) toggled?.Invoke(e.Value != 0); }
    public event Action<bool> Toggled
    {
        add { Window.Guard(); if (toggled is null) Event += OnToggle; toggled += value; }
        remove { Window.Guard(); toggled -= value; if (toggled is null) Event -= OnToggle; }
    }
    public event Action<bool> Changed { add => Toggled += value; remove => Toggled -= value; }
    public void Invoke() { Window.Guard(); Window.Check(Native.Invoke(Handle)); }
    private ButtonStyle? style;
    public ButtonStyle? Style
    {
        get { Window.Guard(); return style; }
        set => SetStyle(value);
    }
    public ToggleButton SetStyle(ButtonStyle? value)
    {
        Window.Guard();
        if (value is null) Window.Check(Native.ButtonSetStyle(Handle, 0));
        else if (!value.TryApply(Window, Handle))
            value.WithNativeHandle(Window, handle => Window.Check(Native.ButtonSetStyle(Handle, handle)));
        style = value;
        return this;
    }
    public ButtonStyleValues StyleValues { get => ReadStyleValues(false); set => SetStyleValues(value); }
    public ButtonStyleValues EffectiveStyleValues => ReadStyleValues(true);
    public unsafe ToggleButton SetStyleValues(ButtonStyleValues value)
    {
        Window.Guard(); ArgumentNullException.ThrowIfNull(value);
        var native = value.ToNative();
        Window.Check(Native.ButtonSetStyleValues(Handle, &native));
        return this;
    }
    private unsafe ButtonStyleValues ReadStyleValues(bool effective)
    {
        Window.Guard();
        var value = ButtonStyleValues.Empty.ToNative();
        Window.Check(Native.ButtonGetStyleValues(Handle, effective ? 1u : 0u, &value));
        return ButtonStyleValues.FromNative(value);
    }
}

public sealed partial class Progress
{
    public Progress SetCapacity(double used, double total, string unit = "bytes") { Features.Set(this, 47, a: used, b: total, text: unit); return this; }
}

public sealed partial class ProgressRing
{
    public ProgressRing SetCapacity(double used, double total, string unit = "bytes") { Features.Set(this, 47, a: used, b: total, text: unit); return this; }
}

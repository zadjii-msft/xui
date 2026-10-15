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
}

public sealed partial class Progress
{
    public Progress SetCapacity(double used, double total, string unit = "bytes") { Features.Set(this, 47, a: used, b: total, text: unit); return this; }
}

public sealed partial class ProgressRing
{
    public ProgressRing SetCapacity(double used, double total, string unit = "bytes") { Features.Set(this, 47, a: used, b: total, text: unit); return this; }
}

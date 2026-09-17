namespace Xui;

public enum CheckState : uint { Unchecked, Checked, Indeterminate }
public enum InfoBadgeKind : uint { Dot, Count, Icon }

public sealed partial class CheckBox
{
    private Action<CheckState>? changed;
    private void OnChange(UiEvent e) { if (e.Kind == EventKind.Change) changed?.Invoke((CheckState)e.Value); }
    public event Action<CheckState> Changed
    {
        add { Window.Guard(); if (changed is null) Event += OnChange; changed += value; }
        remove { Window.Guard(); changed -= value; if (changed is null) Event -= OnChange; }
    }
    public void Invoke() { Window.Guard(); Window.Check(Native.Invoke(Handle)); }
}

public sealed partial class HyperlinkButton
{
    private Action? clicked;
    private void OnClick(UiEvent e) { if (e.Kind == EventKind.Click) clicked?.Invoke(); }
    public event Action Click
    {
        add { Window.Guard(); if (clicked is null) Event += OnClick; clicked += value; }
        remove { Window.Guard(); clicked -= value; if (clicked is null) Event -= OnClick; }
    }
    public void Invoke() { Window.Guard(); Window.Check(Native.Invoke(Handle)); }
    private Button? appearance;
    private Button Appearance => appearance ??= new(Window, Handle);
    public ButtonStyle? Style { get => Appearance.Style; set => Appearance.Style = value; }
    public HyperlinkButton SetStyle(ButtonStyle? value) { Appearance.SetStyle(value); return this; }
    public ButtonStyleValues StyleValues { get => Appearance.StyleValues; set => Appearance.StyleValues = value; }
    public ButtonStyleValues EffectiveStyleValues => Appearance.EffectiveStyleValues;
    public HyperlinkButton SetStyleValues(ButtonStyleValues value) { Appearance.SetStyleValues(value); return this; }
}

public sealed partial class SelectorBar
{
    public ulong? Selected { get { var value = Features.Get(this, 50); return value.Second != 0 ? value.First : null; } }
    public SelectorBar SetSelected(ulong id) { Features.Set(this, 50, first: id); return this; }
    public SelectorBar SetItems(ReadOnlySpan<Choice> items, ulong? selected = null) { Features.Choices(this, items, selected); return this; }
    public SelectorBar Select(ulong id) { Features.Action(this, 1, id); return this; }
    private Action<ulong>? changed;
    private void OnChange(UiEvent e) { if (e.Kind == EventKind.Selection) changed?.Invoke(e.Value); }
    public event Action<ulong> Changed
    {
        add { Window.Guard(); if (changed is null) Event += OnChange; changed += value; }
        remove { Window.Guard(); changed -= value; if (changed is null) Event -= OnChange; }
    }
}

public sealed partial class InfoBadge
{
    public InfoBadgeKind Kind => (InfoBadgeKind)Features.Get(this, 51).First;
}

public sealed partial class MenuBar
{
    public MenuBar SetCommands(ReadOnlySpan<Command> commands) { Features.Commands(this, commands); return this; }
    public void Invoke(ulong id, bool pin = false) => Features.InvokeCommand(this, id, pin);
    public MenuBar Bind(ulong id, uint virtualKey, KeyModifiers modifiers) { Features.BindCommand(this, id, virtualKey, modifiers); return this; }
    private Action<ulong>? invoked, pinned;
    private void OnInvoke(UiEvent e) { if (e.Kind == EventKind.Click) invoked?.Invoke(e.Value); }
    private void OnPin(UiEvent e) { if (e.Kind == EventKind.Action) pinned?.Invoke(e.Value); }
    public event Action<ulong> Invoked
    {
        add { Window.Guard(); if (invoked is null) Event += OnInvoke; invoked += value; }
        remove { Window.Guard(); invoked -= value; if (invoked is null) Event -= OnInvoke; }
    }
    public event Action<ulong> Pinned
    {
        add { Window.Guard(); if (pinned is null) Event += OnPin; pinned += value; }
        remove { Window.Guard(); pinned -= value; if (pinned is null) Event -= OnPin; }
    }
}

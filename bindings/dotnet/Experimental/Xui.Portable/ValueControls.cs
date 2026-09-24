namespace Xui.Experimental.Portable;

public sealed class Toggle : Control
{
    private bool isChecked;
    private Action<bool>? changed;
    public string Text { get => Name; set => Name = value; }
    public bool Checked { get => Read(isChecked); set => Set(ref isChecked, value, ElementProperty.Checked); }
    public Toggle SetChecked(bool value) { Checked = value; return this; }
    public event Action<bool> Changed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); changed += value; }
        remove { Owner.VerifyEventRemoval(this); changed -= value; }
    }
    internal Toggle(Host host, string name) : base(host, ElementKind.Toggle, name) { }
    internal void RaiseChange(bool value)
    {
        if (isChecked == value) return;
        isChecked = value;
        changed?.Invoke(value);
    }
    internal override void Release() { changed = null; base.Release(); }
}

public sealed class CheckBox : Control
{
    private CheckState state;
    private bool threeState;
    private Action<CheckState>? changed;
    public string Text { get => Name; set => Name = value; }
    public CheckState State
    {
        get => Read(state);
        set
        {
            ValidateState(value);
            Set(ref state, value, ElementProperty.CheckState);
        }
    }
    public bool ThreeState { get => Read(threeState); set => Set(ref threeState, value, ElementProperty.ThreeState); }
    public CheckBox SetState(CheckState value) { State = value; return this; }
    public CheckBox SetThreeState(bool value) { ThreeState = value; return this; }
    public event Action<CheckState> Changed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); changed += value; }
        remove { Owner.VerifyEventRemoval(this); changed -= value; }
    }
    internal CheckBox(Host host, string name) : base(host, ElementKind.CheckBox, name) { }
    internal void RaiseChange(CheckState value)
    {
        ValidateState(value);
        if (state == value) return;
        state = value;
        changed?.Invoke(value);
    }
    private static void ValidateState(CheckState value)
    {
        if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value), "Unknown check state.");
    }
    internal override void Release() { changed = null; base.Release(); }
}

public sealed class Progress : Control
{
    private (NumericRange Range, double Value) progress = (new(0, 100), 0);
    private ProgressState state;
    public NumericRange Range
    {
        get => Read(progress).Range;
        set
        {
            ValidateRange(value);
            Set(ref progress, (value, Math.Clamp(progress.Value, value.Minimum, value.Maximum)), ElementProperty.Range);
        }
    }
    public double Value
    {
        get => Read(progress).Value;
        set
        {
            if (!double.IsFinite(value) || value < progress.Range.Minimum || value > progress.Range.Maximum)
                throw new ArgumentOutOfRangeException(nameof(value), "Progress must be finite and within its range.");
            Set(ref progress, (progress.Range, value), ElementProperty.Value);
        }
    }
    public ProgressState State
    {
        get => Read(state);
        set
        {
            if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value), "Only determinate and indeterminate progress are supported.");
            Set(ref state, value, ElementProperty.ProgressState);
        }
    }
    public Progress SetRange(NumericRange value) { Range = value; return this; }
    public Progress SetValue(double value) { Value = value; return this; }
    public Progress SetState(ProgressState value) { State = value; return this; }
    internal Progress(Host host, string name) : base(host, ElementKind.Progress, name) { }
    private static void ValidateRange(NumericRange value)
    {
        if (!double.IsFinite(value.Minimum) || !double.IsFinite(value.Maximum) || value.Minimum >= value.Maximum ||
            !double.IsFinite(value.Maximum - value.Minimum) ||
            !double.IsFinite(value.SmallStep) || value.SmallStep <= 0 ||
            !double.IsFinite(value.LargeStep) || value.LargeStep <= 0)
            throw new ArgumentOutOfRangeException(nameof(value), "Progress requires finite increasing bounds and positive finite steps.");
    }
}

namespace Xui.Experimental.Portable;

public readonly record struct Choice(ulong Id, string Text, bool Enabled = true);

public interface ISingleChoiceElementPeer : IElementPeer
{
    void ValidateChoices(IReadOnlyList<Choice> items, ulong? selected);
}

public interface ISelectionControlEvents : IControlEvents
{
    bool SelectionChanged(ulong selected);
}

public interface IRangeElementPeer : IElementPeer
{
    void ValidateRange(NumericRange range, double value);
}

public interface IRangeControlEvents : IControlEvents
{
    bool RangePreviewed(double value);
    bool RangeChanged(double value);
    bool RangeCanceled(double value);
}

public sealed class SingleChoice : Control
{
    public const int MaximumItems = 4096;
    public const ulong MaximumId = (ulong)long.MaxValue - 100;
    private (IReadOnlyList<Choice> Items, ulong? Selected) choices = (Array.AsReadOnly(Array.Empty<Choice>()), null);
    private Action<ulong>? changed;

    public IReadOnlyList<Choice> Items => Read(choices).Items;
    public ulong? Selected => Read(choices).Selected;
    public event Action<ulong> Changed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); changed += value; }
        remove { Owner.VerifyEventRemoval(this); changed -= value; }
    }

    internal SingleChoice(Host host, string name) : base(host, ElementKind.SingleChoice, name) { }

    public SingleChoice SetItems(ReadOnlySpan<Choice> items, ulong? selected = null)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        if (items.Length > MaximumItems) throw new ArgumentOutOfRangeException(nameof(items), "SingleChoice supports at most 4096 entries.");
        var snapshot = items.ToArray();
        var ids = new HashSet<ulong>();
        foreach (var item in snapshot)
        {
            Values.Text(item.Text);
            if (item.Id == 0 || item.Id > MaximumId || !ids.Add(item.Id))
                throw new ArgumentException("Choice IDs must be unique, nonzero, and within the supported 64-bit range.", nameof(items));
        }
        if (selected.HasValue) RequireChoice(snapshot, selected.Value);
        else if (choices.Selected is ulong previous && snapshot.Any(item => item.Id == previous && item.Enabled))
            selected = previous;
        else
        {
            foreach (var item in snapshot)
                if (item.Enabled) { selected = item.Id; break; }
        }
        if (choices.Selected == selected && choices.Items.SequenceEqual(snapshot)) return this;
        IReadOnlyList<Choice> retained = Array.AsReadOnly(snapshot);
        Owner.ValidateChoiceSupport(this, retained, selected);
        Set(ref choices, (retained, selected), ElementProperty.Choices);
        return this;
    }

    public SingleChoice SetSelected(ulong selected)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        RequireChoice(choices.Items, selected);
        if (choices.Selected == selected) return this;
        Owner.ValidateChoiceSupport(this, choices.Items, selected);
        Set(ref choices, (choices.Items, selected), ElementProperty.Choices);
        return this;
    }

    internal void RaiseChanged(ulong selected)
    {
        RequireChoice(choices.Items, selected);
        if (choices.Selected == selected) return;
        choices.Selected = selected;
        changed?.Invoke(selected);
    }

    private static void RequireChoice(IReadOnlyList<Choice> items, ulong selected)
    {
        if (!items.Any(item => item.Id == selected && item.Enabled))
            throw new ArgumentException("The selected choice must exist and be enabled.", nameof(selected));
    }

    internal override void Release() { changed = null; base.Release(); }
}

public sealed class RangeInput : Control
{
    private (NumericRange Range, double Value, double? Preview) snapshot = (new(0, 100), 0, null);
    private Action<double>? previewed;
    private Action<double>? changed;
    private Action<double>? canceled;

    public NumericRange Range { get => Read(snapshot).Range; set => SetRange(value); }
    public double Value { get => Read(snapshot).Value; set => SetValue(value); }
    public double PreviewValue { get { var current = Read(snapshot); return current.Preview ?? current.Value; } }

    public event Action<double> Previewed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); previewed += value; }
        remove { Owner.VerifyEventRemoval(this); previewed -= value; }
    }
    public event Action<double> Changed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); changed += value; }
        remove { Owner.VerifyEventRemoval(this); changed -= value; }
    }
    public event Action<double> Canceled
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); canceled += value; }
        remove { Owner.VerifyEventRemoval(this); canceled -= value; }
    }

    internal RangeInput(Host host, string name) : base(host, ElementKind.RangeInput, name) { }

    public RangeInput SetRange(NumericRange range)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        RangeMath.Validate(range);
        double value = Math.Clamp(snapshot.Value, range.Minimum, range.Maximum);
        if (snapshot == (range, value, null)) return this;
        Owner.ValidateRangeSupport(this, range, value);
        Set(ref snapshot, (range, value, null), ElementProperty.Range);
        return this;
    }

    public RangeInput SetValue(double value)
    {
        VerifyAccess();
        Owner.VerifyElementMutation(this);
        RangeMath.ValidateValue(snapshot.Range, value);
        if (snapshot.Value == value && snapshot.Preview is null) return this;
        Owner.ValidateRangeSupport(this, snapshot.Range, value);
        Set(ref snapshot, (snapshot.Range, value, null), ElementProperty.Value);
        return this;
    }

    internal void RaisePreview(double value)
    {
        RangeMath.ValidateValue(snapshot.Range, value);
        if ((snapshot.Preview ?? snapshot.Value) == value) return;
        snapshot.Preview = value;
        previewed?.Invoke(value);
    }
    internal void RaiseChanged(double value)
    {
        RangeMath.ValidateValue(snapshot.Range, value);
        bool notify = snapshot.Value != value;
        snapshot = (snapshot.Range, value, null);
        if (notify) changed?.Invoke(value);
    }
    internal void RaiseCanceled(double value, bool notify)
    {
        RangeMath.ValidateValue(snapshot.Range, value);
        if (value != snapshot.Value) throw new InvalidOperationException("Range cancellation must report the current committed value.");
        snapshot.Preview = null;
        if (notify) canceled?.Invoke(value);
    }
    internal void ResetPreview() => snapshot.Preview = null;
    internal override void Release() { previewed = null; changed = null; canceled = null; ResetPreview(); base.Release(); }
}

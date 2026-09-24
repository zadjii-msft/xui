namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    public SingleChoice SingleChoice(string name) => Create(() => new SingleChoice(this, name));
    public RangeInput RangeInput(string name) => Create(() => new RangeInput(this, name));

    internal void ValidateChoiceSupport(SingleChoice choice, IReadOnlyList<Choice> items, ulong? selected)
    {
        if (attachment is { } current && current.Peers.TryGetValue(choice, out var peer))
        {
            if (peer is not ISingleChoiceElementPeer choices)
                throw new NotSupportedException("SingleChoice requires an ISingleChoiceElementPeer backend.");
            InputOperation(() => { choices.ValidateChoices(items, selected); return true; });
        }
    }

    internal void ValidateRangeSupport(RangeInput range, NumericRange value, double currentValue)
    {
        if (attachment is { } current && current.Peers.TryGetValue(range, out var peer))
        {
            if (peer is not IRangeElementPeer native)
                throw new NotSupportedException("RangeInput requires an IRangeElementPeer backend.");
            InputOperation(() => { native.ValidateRange(value, currentValue); return true; });
        }
    }

    private void ValidateSelectionPeer(Element element, IElementPeer peer)
    {
        if (element is SingleChoice choice)
        {
            if (peer is not ISingleChoiceElementPeer native)
                throw new NotSupportedException("SingleChoice requires an ISingleChoiceElementPeer backend.");
            InputOperation(() => { native.ValidateChoices(choice.Items, choice.Selected); return true; });
        }
        if (element is RangeInput range)
        {
            if (peer is not IRangeElementPeer native)
                throw new NotSupportedException("RangeInput requires an IRangeElementPeer backend.");
            InputOperation(() => { native.ValidateRange(range.Range, range.Value); return true; });
        }
    }

    internal void ResetRangePreviews(Element element)
    {
        if (element is RangeInput range) range.ResetPreview();
        foreach (var child in element.Children) ResetRangePreviews(child);
    }
}

using Xui.Experimental.Portable;
using P = Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

internal sealed class WindowsSingleChoicePeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsPeer(backend, element, events), ISingleChoiceElementPeer
{
    public void ValidateChoices(IReadOnlyList<P.Choice> items, ulong? selected)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (items.Count > P.SingleChoice.MaximumItems) throw new ArgumentOutOfRangeException(nameof(items));
        var ids = new HashSet<ulong>();
        foreach (var item in items)
            if (item.Id == 0 || item.Id > P.SingleChoice.MaximumId || !ids.Add(item.Id) ||
                item.Text is null || item.Text.Contains('\0'))
                throw new ArgumentException("Invalid native choice metadata.", nameof(items));
        if (selected.HasValue && !items.Any(item => item.Id == selected.Value && item.Enabled))
            throw new ArgumentException("The selected native choice must exist and be enabled.", nameof(selected));
    }
}

internal sealed class WindowsRangePeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsPeer(backend, element, events), IRangeElementPeer
{
    public void ValidateRange(P.NumericRange range, double value)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        P.RangeMath.Validate(range);
        P.RangeMath.ValidateValue(range, value);
    }
}

namespace Xui.Experimental.Portable;

public interface IFocusableElementPeer : IElementPeer
{
    bool TryFocus();
    bool HasFocus { get; }
}

public interface ITextSelectionPeer : IFocusableElementPeer
{
    TextSelection Selection { get; set; }
}

/// <summary>Ordered UTF-16 offsets. Setting a selection deliberately changes native editing state.</summary>
public readonly record struct TextSelection(int Start, int End)
{
    public TextSelection ClampTo(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        Validate();
        int start = Math.Min(Math.Min(Start, End), text.Length);
        int end = Math.Min(Math.Max(Start, End), text.Length);
        bool Split(int offset) => offset > 0 && offset < text.Length &&
            char.IsHighSurrogate(text[offset - 1]) && char.IsLowSurrogate(text[offset]);
        if (start == end && Split(start)) { start--; end--; }
        else
        {
            if (Split(start)) start--;
            if (Split(end)) end++;
        }
        return new(start, end);
    }

    internal void Validate()
    {
        if (Start < 0 || End < 0) throw new ArgumentOutOfRangeException(nameof(TextSelection), "Selection offsets must be nonnegative.");
    }
}

public sealed partial class Host
{
    /// <summary>Requests native focus for a live control. Disabled, hidden, and nonfocusable controls return false.</summary>
    public bool TryFocus(Control control)
    {
        ArgumentNullException.ThrowIfNull(control);
        VerifyElementMutation(control);
        var peer = InputPeer<IFocusableElementPeer>(control);
        if (!control.AcceptsInput()) return false;
        return InputOperation(peer.TryFocus);
    }

    public bool HasFocus(Control control)
    {
        VerifyAccess();
        var peer = InputPeer<IFocusableElementPeer>(control);
        return InputOperation(() => peer.HasFocus);
    }

    public TextSelection GetSelection(TextInput input)
    {
        VerifyAccess();
        var peer = InputPeer<ITextSelectionPeer>(input);
        var selection = InputOperation(() => peer.Selection);
        selection.Validate();
        if (selection.Start > selection.End) throw new InvalidOperationException("The backend returned an unordered selection.");
        return selection;
    }

    /// <summary>Sets a native range, clamping against the current native text without splitting surrogate pairs.</summary>
    public void SetSelection(TextInput input, TextSelection selection)
    {
        ArgumentNullException.ThrowIfNull(input);
        VerifyElementMutation(input);
        selection.Validate();
        var peer = InputPeer<ITextSelectionPeer>(input);
        InputOperation(() => { peer.Selection = selection; return true; });
    }

    private T InputPeer<T>(Control control) where T : IElementPeer
    {
        ArgumentNullException.ThrowIfNull(control);
        if (!ReferenceEquals(control.Owner, this)) throw new ArgumentException("The control belongs to another host.", nameof(control));
        ObjectDisposedException.ThrowIf(control.Disposed, control);
        if (attachment is null) throw new InvalidOperationException("Native input operations require an attached host.");
        if (!attachment.Peers.TryGetValue(control, out var elementPeer))
            throw new InvalidOperationException("The control has not been mounted.");
        return elementPeer is T peer ? peer :
            throw new NotSupportedException("This backend does not implement the requested native input operation.");
    }

    private T InputOperation<T>(Func<T> operation)
    {
        updating++;
        T result = default!;
        Exception? failure = null;
        try { result = operation(); }
        catch (Exception error) { failure = error; }
        finally { updating--; }
        FinishInteractionDelivery(failure);
        return result;
    }
}

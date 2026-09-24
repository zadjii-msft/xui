namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    internal void VerifyTextLayoutSupport(Label label, LabelTextLayout? layout)
    {
        if (attachment is { } current && current.Peers.TryGetValue(label, out var peer))
            ValidateLabelTextLayout(peer, layout);
    }

    private void ValidateTextLayoutPeer(Element element, IElementPeer peer)
    {
        if (element is Label { TextLayout: { } layout })
            ValidateLabelTextLayout(peer, layout);
    }

    private void ValidateLabelTextLayout(IElementPeer peer, LabelTextLayout? layout)
    {
        if (peer is ITextLayoutPeer text)
            InputOperation(() => { text.ValidateTextLayout(layout); return true; });
        else if (layout is not null)
            throw new NotSupportedException("Explicit label text layout requires an ITextLayoutPeer.");
    }
}

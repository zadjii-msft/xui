namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    internal bool CanSetRevealOpen(Reveal reveal, bool open)
    {
        if (attachment is { } current && current.Peers.TryGetValue(reveal, out var peer))
            return InputOperation(() => RequireRevealPeer(peer).CanSetOpen(open));
        return true;
    }

    internal void VerifyRevealMotionSupport(Reveal reveal, RevealMotion motion)
    {
        ArgumentNullException.ThrowIfNull(motion);
        if (attachment is { } current && current.Peers.TryGetValue(reveal, out var peer))
            InputOperation(() => { RequireRevealPeer(peer).ValidateMotion(motion); return true; });
    }

    private void ValidateRevealPeer(Element element, IElementPeer peer)
    {
        if (element is Reveal reveal)
        {
            RevealLayoutMath.ValidateElement(reveal);
            InputOperation(() => { RequireRevealPeer(peer).ValidateMotion(reveal.Motion); return true; });
        }
    }

    /// <summary>Reads current native reveal progress without starting or advancing a managed clock.</summary>
    public RevealPresentation GetRevealPresentation(Reveal reveal)
    {
        VerifyComponent(reveal);
        var current = attachment ?? throw new InvalidOperationException("Reveal presentation requires an attached host.");
        if (!current.Peers.TryGetValue(reveal, out var peer))
            throw new InvalidOperationException("The reveal has not been mounted.");
        var presentation = InputOperation(() => RequireRevealPeer(peer).Presentation);
        presentation.Validate();
        return presentation;
    }

    private static IRevealElementPeer RequireRevealPeer(IElementPeer peer) =>
        peer as IRevealElementPeer ??
            throw new NotSupportedException("Retained reveal presentation requires an IRevealElementPeer.");
}

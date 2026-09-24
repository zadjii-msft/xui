using Xui.Experimental.Portable;
using P = Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

public sealed partial class WindowsBackend
{
    public void ValidateTree(P.Element root)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(root);
        ValidateRevealSubtree(root, false);
    }
    public void ValidateInsertion(P.Element parent, P.Element candidateRoot)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(parent);
        ArgumentNullException.ThrowIfNull(candidateRoot);
        bool inside = false;
        for (P.Element? ancestor = parent; ancestor is not null; ancestor = ancestor.Parent)
            if (ancestor is P.Reveal) inside = true;
        ValidateRevealSubtree(candidateRoot, inside);
    }
    private void ValidateRevealSubtree(P.Element element, bool inside)
    {
        if (element is P.Reveal reveal)
        {
            if (!SupportsReveal) throw new NotSupportedException("The native runtime does not provide portable Reveal accessibility and close veto semantics.");
            if (reveal.FixedSize is not null || reveal.PreferredSize is not null || reveal.WidthConstraints is not null ||
                reveal.HeightConstraints is not null || reveal.Flex != 0)
                throw new NotSupportedException("Size the Reveal child, not the expanding Reveal container.");
            inside = true;
        }
        if (inside && element is P.MultilineText)
            throw new NotSupportedException("This Windows Reveal cannot contain native RichEdit content without losing its native text accessibility provider.");
        foreach (var child in element.Children) ValidateRevealSubtree(child, inside);
    }
}

internal sealed class WindowsRevealPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsPeer(backend, element, events), IRevealElementPeer
{
    private bool canceled;
    internal static PortableRevealState State(bool open, P.RevealMotion motion) => new(open, motion.DurationMilliseconds,
        motion.Direction switch
        {
            P.RevealDirection.Bottom => RevealDirection.Bottom,
            P.RevealDirection.Right => RevealDirection.Right,
            _ => throw new NotSupportedException("Unsupported native Reveal direction.")
        });
    public bool CanSetOpen(bool open)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed || canceled, this);
        return ((Reveal)Native).CanSetPortableOpen(open);
    }
    public void ValidateMotion(P.RevealMotion motion)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed || canceled, this);
        ArgumentNullException.ThrowIfNull(motion);
        var state = State(((P.Reveal)Element).Open, motion);
        if (NativeReady) ((Reveal)Native).ValidatePortableState(state);
    }
    public P.RevealPresentation Presentation
    {
        get
        {
            Backend.VerifyAccess();
            ObjectDisposedException.ThrowIf(Disposed || canceled, this);
            var value = ((Reveal)Native).PortablePresentation;
            return new(value.Progress, value.Animating);
        }
    }
    internal void CancelMotion()
    {
        Backend.VerifyAccess();
        if (canceled || Disposed || !NativeReady) return;
        ((Reveal)Native).CancelPortableMotion();
        canceled = true;
    }
    public override void Dispose()
    {
        Backend.VerifyAccess();
        if (Disposed) return;
        CancelMotion();
        base.Dispose();
    }
}

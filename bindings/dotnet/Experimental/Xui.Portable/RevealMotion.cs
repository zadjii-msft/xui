namespace Xui.Experimental.Portable;

public enum RevealDirection { Bottom, Right }

/// <summary>Opt-in motion for one retained expanding reveal, not a general layout animation.</summary>
public sealed record RevealMotion
{
    public static RevealMotion Default { get; } = new();
    public uint DurationMilliseconds { get; }
    public RevealDirection Direction { get; }

    public RevealMotion(uint durationMilliseconds = 0, RevealDirection direction = RevealDirection.Bottom)
    {
        if (durationMilliseconds > 400)
            throw new ArgumentOutOfRangeException(nameof(durationMilliseconds), "Reveal duration must be between 0 and 400 milliseconds.");
        if (!Enum.IsDefined(direction)) throw new ArgumentOutOfRangeException(nameof(direction));
        DurationMilliseconds = durationMilliseconds;
        Direction = direction;
    }
}

/// <summary>An actual native presentation snapshot, independent of the requested open state.</summary>
public readonly record struct RevealPresentation(float Progress, bool Animating)
{
    internal void Validate()
    {
        if (!float.IsFinite(Progress) || Progress is < 0 or > 1)
            throw new InvalidOperationException("The backend returned invalid reveal presentation progress.");
    }
}

/// <summary>Native retained clipping, close preflight, and observed motion presentation.</summary>
/// <remarks>
/// Initial attachment is settled. A changed motion configuration settles the previous
/// logical target before applying the new pair; an unchanged configuration retargets
/// from current presentation. Disposal stops native clocks and callbacks before unmount.
/// </remarks>
public interface IRevealElementPeer : IElementPeer
{
    /// <summary>Returns false only when closing would hide a focused or composing descendant.</summary>
    bool CanSetOpen(bool open);
    void ValidateMotion(RevealMotion motion);
    RevealPresentation Presentation { get; }
}

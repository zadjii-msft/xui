namespace Xui.Experimental.Portable;

/// <summary>Local-origin sizes for the reveal clip and the full retained child allocation.</summary>
public readonly record struct RevealArrangement(Size ClipSize, Size ContentSize);

public static class RevealLayoutMath
{
    /// <summary>Measures animated demand without allowing an exact parent offer to undo progress.</summary>
    public static Size Measure(Size fullNatural, MeasureConstraint width, MeasureConstraint height,
        RevealDirection direction, RevealPresentation presentation)
    {
        Validate(fullNatural, direction, presentation);
        width.Validate();
        height.Validate();
        if (presentation.Progress == 0 && !presentation.Animating) return default;
        bool vertical = direction == RevealDirection.Bottom;
        var animatedOffer = vertical ? height : width;
        float extent = (vertical ? fullNatural.Height : fullNatural.Width) * presentation.Progress;
        if (animatedOffer.Mode != MeasureMode.Unspecified) extent = Math.Min(extent, animatedOffer.Size);
        float cross = vertical
            ? LayoutMath.MeasureAxis(fullNatural.Width, width, null)
            : LayoutMath.MeasureAxis(fullNatural.Height, height, null);
        return vertical ? new(cross, extent) : new(extent, cross);
    }

    /// <summary>Clips progress inside the parent's slot while keeping the child full-sized on the animation axis.</summary>
    public static RevealArrangement Arrange(Size fullNatural, Size allocation, RevealDirection direction,
        RevealPresentation presentation)
    {
        Validate(fullNatural, direction, presentation);
        Values.Size(allocation.Width, allocation.Height);
        if (direction == RevealDirection.Bottom)
            return new(new(allocation.Width, Math.Min(allocation.Height, fullNatural.Height * presentation.Progress)),
                new(allocation.Width, fullNatural.Height));
        return new(new(Math.Min(allocation.Width, fullNatural.Width * presentation.Progress), allocation.Height),
            new(fullNatural.Width, allocation.Height));
    }

    internal static void ValidateElement(Reveal reveal)
    {
        if (reveal.FixedSize is not null || reveal.PreferredSize is not null ||
            reveal.WidthConstraints is not null || reveal.HeightConstraints is not null || reveal.Flex != 0)
            throw new NotSupportedException("Size the retained child, not the expanding Reveal; outer size, axis constraints, and nonzero flex are unsupported.");
    }

    private static void Validate(Size fullNatural, RevealDirection direction, RevealPresentation presentation)
    {
        Values.Size(fullNatural.Width, fullNatural.Height);
        if (!Enum.IsDefined(direction)) throw new ArgumentOutOfRangeException(nameof(direction));
        float extent = direction == RevealDirection.Bottom ? fullNatural.Height : fullNatural.Width;
        if (extent == float.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(fullNatural), "Reveal content requires a bounded natural extent on the animation axis.");
        presentation.Validate();
    }
}

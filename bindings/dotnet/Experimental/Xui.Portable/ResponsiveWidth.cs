namespace Xui.Experimental.Portable;

public enum WidthMode { Compact, Medium, Expanded }

/// <summary>Breakpoints in the host's logical layout units, independent of font scaling.</summary>
public sealed record WidthBreakpoints
{
    public static WidthBreakpoints Default { get; } = new();
    public float Medium { get; }
    public float Expanded { get; }

    public WidthBreakpoints(float medium = 720, float expanded = 1120)
    {
        if (!float.IsFinite(medium) || medium <= 0)
            throw new ArgumentOutOfRangeException(nameof(medium), "The medium breakpoint must be finite and positive.");
        if (!float.IsFinite(expanded) || expanded <= medium)
            throw new ArgumentOutOfRangeException(nameof(expanded), "The expanded breakpoint must be finite and greater than medium.");
        Medium = medium;
        Expanded = expanded;
    }

    public WidthMode Select(float width)
    {
        if (!float.IsFinite(width) || width < 0)
            throw new ArgumentOutOfRangeException(nameof(width), "Viewport width must be finite and nonnegative.");
        return width >= Expanded ? WidthMode.Expanded : width >= Medium ? WidthMode.Medium : WidthMode.Compact;
    }
}

/// <summary>Observes the owned host's allocated layout space, not the outer window or a scroll extent.</summary>
public interface IHostViewportBackend : IBackend
{
    /// <remarks>
    /// Supply an initial size synchronously before returning a nonnull subscription.
    /// Later notifications use the host UI thread. Dimensions use the same logical units
    /// and available rectangle as native root layout. Disposal stops native observation.
    /// </remarks>
    IDisposable ObserveViewport(Action<Size> changed);
}

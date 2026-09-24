using System.Runtime.InteropServices;

namespace Xui;

/// <summary>An explicit sizing override for one axis. A null length uses natural measurement.</summary>
/// <remarks>Use a nullable AxisConstraints value of null to inherit legacy sizing on that axis.
/// Auto is an explicit override and is different from inheritance.</remarks>
public readonly record struct AxisConstraints
{
    public float? Length { get; }
    public float Minimum { get; }
    public float? Maximum { get; }
    public static AxisConstraints Auto => default;

    public AxisConstraints(float? length = null, float minimum = 0, float? maximum = null)
    {
        if (!float.IsFinite(minimum) || minimum < 0)
            throw new ArgumentOutOfRangeException(nameof(minimum), "The minimum must be finite and nonnegative.");
        if (maximum is { } limit && (!float.IsFinite(limit) || limit < minimum))
            throw new ArgumentOutOfRangeException(nameof(maximum), "The maximum must be finite and at least the minimum.");
        if (length is { } fixedLength &&
            (!float.IsFinite(fixedLength) || fixedLength < minimum || (maximum is { } cap && fixedLength > cap)))
            throw new ArgumentOutOfRangeException(nameof(length), "The length must be finite and within its bounds.");
        Length = length;
        Minimum = minimum;
        Maximum = maximum;
    }
}

public abstract partial class Element
{
    /// <summary>Whether the loaded runtime provides atomic, independent axis overrides.</summary>
    public static bool SupportsAxisConstraints => Native.AxisConstraintsAvailable.Value;

    /// <summary>Atomically overrides both sizing axes without replacing the element or its legacy sizing.</summary>
    /// <remarks>Null inherits the latest legacy sizing for that axis. Auto explicitly selects natural measurement.
    /// A smaller parent allocation takes precedence over the minimum. Only supported native control families accept overrides.</remarks>
    public Element SetAxisConstraints(AxisConstraints? width, AxisConstraints? height)
    {
        Window.Guard();
        RequireAxisConstraints();
        var value = new Native.AxisConstraintsValue
        {
            Size = 40,
            Version = 0x00010000,
            Width = EncodeAxis(width),
            Height = EncodeAxis(height)
        };
        Window.Check(Native.ElementSetAxisConstraints(Handle, in value));
        return this;
    }

    /// <summary>Returns the explicit overrides, retaining the distinction between Auto and inherited sizing.</summary>
    public (AxisConstraints? Width, AxisConstraints? Height) GetAxisConstraints()
    {
        Window.Guard();
        RequireAxisConstraints();
        var value = new Native.AxisConstraintsValue { Size = 40, Version = 0x00010000 };
        Window.Check(Native.ElementGetAxisConstraints(Handle, ref value));
        if (value.Size != 40 || value.Version != 0x00010000)
            throw new InvalidOperationException("The native runtime returned an invalid axis constraint version.");
        return (DecodeAxis(value.Width), DecodeAxis(value.Height));
    }

    private static void RequireAxisConstraints()
    {
        if (!SupportsAxisConstraints)
            throw new NotSupportedException("This native XUI runtime does not support independent axis constraints.");
    }

    private static Native.AxisConstraintValue EncodeAxis(AxisConstraints? value) => value is { } axis
        ? new()
        {
            Flags = 1u | (axis.Length.HasValue ? 2u : 0u) | (axis.Maximum.HasValue ? 4u : 0u),
            Length = axis.Length.GetValueOrDefault(),
            Minimum = axis.Minimum,
            Maximum = axis.Maximum.GetValueOrDefault()
        } : default;

    private static AxisConstraints? DecodeAxis(Native.AxisConstraintValue value)
    {
        if ((value.Flags & ~7u) != 0 ||
            ((value.Flags & 1) == 0 && (value.Flags != 0 || value.Minimum != 0)) ||
            ((value.Flags & 2) == 0 && value.Length != 0) ||
            ((value.Flags & 4) == 0 && value.Maximum != 0))
            throw new InvalidOperationException("The native runtime returned invalid axis constraint flags.");
        if (value.Flags == 0) return null;
        try
        {
            return new((value.Flags & 2) != 0 ? value.Length : null, value.Minimum,
                (value.Flags & 4) != 0 ? value.Maximum : null);
        }
        catch (ArgumentOutOfRangeException error)
        {
            throw new InvalidOperationException("The native runtime returned invalid axis constraint bounds.", error);
        }
    }
}

internal static partial class Native
{
    internal static readonly Lazy<bool> AxisConstraintsAvailable = new(() =>
        HasExports("xui_element_set_axis_constraints", "xui_element_get_axis_constraints"));

    [StructLayout(LayoutKind.Sequential)]
    internal struct AxisConstraintValue
    {
        internal uint Flags;
        internal float Length, Minimum, Maximum;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AxisConstraintsValue
    {
        internal uint Size, Version;
        internal AxisConstraintValue Width, Height;
    }

    [LibraryImport("xui", EntryPoint = "xui_element_set_axis_constraints")]
    internal static partial int ElementSetAxisConstraints(ulong element, in AxisConstraintsValue constraints);
    [LibraryImport("xui", EntryPoint = "xui_element_get_axis_constraints")]
    internal static partial int ElementGetAxisConstraints(ulong element, ref AxisConstraintsValue constraints);
}

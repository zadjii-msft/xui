using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Xui;

public enum StyleFontStyle : uint { Normal, Italic, Oblique }
public enum StyleAlignment : uint { Start, Center, End, Stretch }

public sealed record PartStyleValues
{
    public ThemeColor? Background { get; init; }
    public ThemeColor? Foreground { get; init; }
    public ThemeColor? BorderBrush { get; init; }
    public Insets? BorderThickness { get; init; }
    public Insets? Padding { get; init; }
    public float? CornerRadius { get; init; }
    public float? Size { get; init; }
    public string? FontFamily { get; init; }
    public float? FontSize { get; init; }
    public uint? FontWeight { get; init; }
    public StyleFontStyle? FontStyle { get; init; }
    public StyleAlignment? HorizontalAlignment { get; init; }
    public StyleAlignment? VerticalAlignment { get; init; }
    public float? Spacing { get; init; }
    public float? RowHeight { get; init; }
    public float? HeaderHeight { get; init; }
    public float? Indentation { get; init; }
    public float? Thickness { get; init; }
    public float? Width { get; init; }
    public float? Height { get; init; }
    public float? RowGap { get; init; }
    public float? ColumnGap { get; init; }
    public uint? MaximumLines { get; init; }
    public bool? Wrapping { get; init; }
    internal static readonly PartStyleValues Empty = new();

    internal void Validate(StyleTarget target, StylePart part, ulong state = 0)
    {
        if (!Enum.IsDefined(target) || !Enum.IsDefined(part))
            throw new ArgumentException("Unsupported style target or part.");
        ulong mask = (Background.HasValue ? 1u : 0) | (Foreground.HasValue ? 2u : 0) |
            (BorderBrush.HasValue ? 4u : 0) | (BorderThickness.HasValue ? 8u : 0) |
            (Padding.HasValue ? 16u : 0) | (CornerRadius.HasValue ? 32u : 0) | (Size.HasValue ? 64u : 0) |
            (FontFamily is not null ? 128u : 0) | (FontSize.HasValue ? 256u : 0) | (FontWeight.HasValue ? 512u : 0) |
            (FontStyle.HasValue ? 1024u : 0) | (HorizontalAlignment.HasValue ? 2048u : 0) | (VerticalAlignment.HasValue ? 4096u : 0) |
            (Spacing.HasValue ? 8192u : 0) | (RowHeight.HasValue ? 16384u : 0) | (HeaderHeight.HasValue ? 32768u : 0) |
            (Indentation.HasValue ? 65536u : 0) | (Thickness.HasValue ? 131072u : 0) | (Width.HasValue ? 262144u : 0) |
            (Height.HasValue ? 524288u : 0) | (RowGap.HasValue ? 1048576u : 0) | (ColumnGap.HasValue ? 2097152u : 0) |
            (MaximumLines.HasValue ? 4194304u : 0) | (Wrapping.HasValue ? 8388608u : 0);
        var schema = StyleSchemaCatalog.Find(target, part) ?? throw new ArgumentException("Unsupported style target or part.");
        var limits = StyleSchemaCatalog.Limits(target, part);
        if ((state & ~schema.States) != 0) throw new ArgumentException("Unsupported state on this style part.");
        ulong allowed = state == 0 ? schema.Properties : schema.StateProperties;
        if ((mask & ~allowed) != 0) throw new ArgumentException("Unsupported property on this style part.");
        new ButtonStyleValues { Background = Background, Foreground = Foreground, BorderBrush = BorderBrush,
            BorderThickness = BorderThickness, Padding = Padding, CornerRadius = CornerRadius }.Validate();
        if (Size is { } size) ButtonStyleValues.Dimension(size);
        if (FontFamily is { } family && (family.Length == 0 || family.Length > limits.FontFamilyUtf16 || family.Contains('\0') ||
            new UTF8Encoding(false, true).GetByteCount(family) > 1024)) throw new ArgumentException("Invalid font family.");
        if (FontSize is { } fontSize) { ButtonStyleValues.Dimension(fontSize); if (fontSize == 0 || fontSize > limits.FontSize) throw new ArgumentOutOfRangeException(nameof(FontSize)); }
        if (FontWeight is < 1 or > 999) throw new ArgumentOutOfRangeException(nameof(FontWeight));
        if (FontStyle is { } fontStyle && (!Enum.IsDefined(fontStyle) || (limits.FontStyles & (1u << (int)fontStyle)) == 0))
            throw new ArgumentOutOfRangeException(nameof(FontStyle));
        if (HorizontalAlignment is { } horizontal && (!Enum.IsDefined(horizontal) || (limits.HorizontalAlignments & (1u << (int)horizontal)) == 0))
            throw new ArgumentOutOfRangeException(nameof(HorizontalAlignment));
        if (VerticalAlignment is { } vertical && (!Enum.IsDefined(vertical) || (limits.VerticalAlignments & (1u << (int)vertical)) == 0))
            throw new ArgumentOutOfRangeException(nameof(VerticalAlignment));
        foreach (var dimension in new[] { Spacing, RowHeight, HeaderHeight, Indentation, Thickness, Width, Height, RowGap, ColumnGap })
            if (dimension is { } number) ButtonStyleValues.Dimension(number);
        if (RowHeight is 0) throw new ArgumentOutOfRangeException(nameof(RowHeight));
        if (MaximumLines is > 32768) throw new ArgumentOutOfRangeException(nameof(MaximumLines));
    }
    internal unsafe void Append(List<Native.StyleProperty> records, StylePart part, ulong state, Window.Pins pins)
    {
        if (FontFamily is { } fontFamily) _ = new UTF8Encoding(false, true).GetByteCount(fontFamily);
        Native.StyleProperty Record(uint property, uint type) => new() {
            Size = (uint)sizeof(Native.StyleProperty), Version = 0x10000,
            Property = property, ValueType = type, Part = (uint)part, State = state
        };
        void Color(uint property, ThemeColor? color)
        {
            if (color is not { } c) return;
            var r = Record(property, 1); r.Color = new() { Light = c.Light, Dark = c.Dark }; records.Add(r);
        }
        void Edges(uint property, Insets? insets)
        {
            if (insets is not { } e) return;
            var r = Record(property, 2); r.Insets = new() { Left = e.Left, Top = e.Top, Right = e.Right, Bottom = e.Bottom }; records.Add(r);
        }
        void Number(uint property, double? number)
        {
            if (number is not { } n) return;
            var r = Record(property, 3); r.Number = n; records.Add(r);
        }
        Color(1, Background); Color(2, Foreground); Color(4, BorderBrush);
        Edges(8, BorderThickness); Edges(16, Padding); Number(32, CornerRadius); Number(64, Size);
        if (FontFamily is { } family) { var r = Record(128, 4); r.Text = pins.Text(family); records.Add(r); }
        Number(256, FontSize); Number(512, FontWeight); Number(1024, FontStyle is { } fs ? (uint)fs : null);
        Number(2048, HorizontalAlignment is { } h ? (uint)h : null); Number(4096, VerticalAlignment is { } v ? (uint)v : null);
        Number(8192, Spacing); Number(16384, RowHeight); Number(32768, HeaderHeight); Number(65536, Indentation);
        Number(131072, Thickness); Number(262144, Width); Number(524288, Height);
        Number(1048576, RowGap); Number(2097152, ColumnGap); Number(4194304, MaximumLines);
        Number(8388608, Wrapping is { } wrap ? wrap ? 1 : 0 : null);
    }
    internal static unsafe PartStyleValues FromNative(IEnumerable<Native.StyleProperty> records)
    {
        var result = Empty;
        foreach (var r in records)
        {
            var c = new ThemeColor(r.Color.Light, r.Color.Dark);
            var e = new Insets(r.Insets.Left, r.Insets.Top, r.Insets.Right, r.Insets.Bottom);
            result = r.Property switch {
                1 => result with { Background = c }, 2 => result with { Foreground = c },
                4 => result with { BorderBrush = c }, 8 => result with { BorderThickness = e },
                16 => result with { Padding = e }, 32 => result with { CornerRadius = (float)r.Number },
                64 => result with { Size = (float)r.Number },
                128 => result with { FontFamily = Marshal.PtrToStringUTF8((nint)r.Text.Data, checked((int)r.Text.Length)) },
                256 => result with { FontSize = (float)r.Number }, 512 => result with { FontWeight = (uint)r.Number },
                1024 => result with { FontStyle = (StyleFontStyle)(uint)r.Number },
                2048 => result with { HorizontalAlignment = (StyleAlignment)(uint)r.Number },
                4096 => result with { VerticalAlignment = (StyleAlignment)(uint)r.Number },
                8192 => result with { Spacing = (float)r.Number }, 16384 => result with { RowHeight = (float)r.Number },
                32768 => result with { HeaderHeight = (float)r.Number }, 65536 => result with { Indentation = (float)r.Number },
                131072 => result with { Thickness = (float)r.Number }, 262144 => result with { Width = (float)r.Number },
                524288 => result with { Height = (float)r.Number }, 1048576 => result with { RowGap = (float)r.Number },
                2097152 => result with { ColumnGap = (float)r.Number }, 4194304 => result with { MaximumLines = (uint)r.Number },
                8388608 => result with { Wrapping = r.Number != 0 },
                _ => throw new InvalidOperationException("Unknown native style property.")
            };
        }
        return result;
    }
}

public sealed record PartStyle(StylePart Part, PartStyleValues Values);
public sealed record ControlStyleRule(StylePart Part, StyleState State, PartStyleValues Values);

/// <summary>An immutable shared style with weak native identities for each window.</summary>
public sealed class ControlStyle
{
    private sealed class NativeIdentity { internal ulong Value; }
    private readonly ConditionalWeakTable<Window, NativeIdentity> identities = new();
    private readonly int depth;
    public StyleTarget Target { get; }
    public IReadOnlyList<PartStyle> Parts { get; }
    public IReadOnlyList<ControlStyleRule> Rules { get; }
    public ControlStyle? BasedOn { get; }
    public ControlStyle(StyleTarget target, IReadOnlyList<PartStyle> parts,
        IReadOnlyList<ControlStyleRule>? rules = null, ControlStyle? basedOn = null)
    {
        ArgumentNullException.ThrowIfNull(parts);
        if (!Enum.IsDefined(target) || !StyleSchemaCatalog.HasTarget(target)) throw new ArgumentOutOfRangeException(nameof(target));
        if (basedOn is not null && basedOn.Target != target) throw new ArgumentException("Style base target mismatch.");
        depth = (basedOn?.depth ?? 0) + 1;
        if (depth > 16 || parts.Count > 64 || rules?.Count > 256) throw new ArgumentException("Style limits exceeded.");
        var partCopy = parts.ToArray(); var ruleCopy = rules?.ToArray() ?? [];
        var keys = new HashSet<(StylePart, ulong)>();
        foreach (var p in partCopy)
        {
            ArgumentNullException.ThrowIfNull(p); ArgumentNullException.ThrowIfNull(p.Values);
            p.Values.Validate(target, p.Part);
            if (!keys.Add((p.Part, 0))) throw new ArgumentException("Duplicate style part.");
        }
        foreach (var r in ruleCopy)
        {
            ArgumentNullException.ThrowIfNull(r); ArgumentNullException.ThrowIfNull(r.Values);
            if (!Enum.IsDefined(r.State)) throw new ArgumentException("A rule requires one supported state.");
            r.Values.Validate(target, r.Part, (ulong)r.State);
            if (!keys.Add((r.Part, (ulong)r.State))) throw new ArgumentException("Duplicate style state for this part.");
        }
        Target = target; Parts = Array.AsReadOnly(partCopy); Rules = Array.AsReadOnly(ruleCopy); BasedOn = basedOn;
    }
    internal unsafe void WithNativeHandle(Window window, Action<ulong> apply)
    {
        window.Guard();
        var identity = identities.GetOrCreateValue(window);
        if (identity.Value != 0)
        {
            ulong retained = 0;
            window.Check(Native.ControlStyleReacquire(window.Handle, identity.Value, &retained));
            if (retained != 0)
            {
                try { apply(retained); } finally { window.Check(Native.ControlStyleRelease(retained)); }
                return;
            }
        }
        var records = new List<Native.StyleProperty>();
        using var pins = new Window.Pins();
        foreach (var p in Parts) p.Values.Append(records, p.Part, 0, pins);
        foreach (var r in Rules) r.Values.Append(records, r.Part, (ulong)r.State, pins);
        var array = records.ToArray();
        void Create(ulong parent)
        {
            ulong handle = 0;
            fixed (Native.StyleProperty* p = array)
            {
                var options = new Native.ControlStyleOptions { Size = (uint)sizeof(Native.ControlStyleOptions),
                    Version = 0x10000, Target = (uint)Target, Properties = p, PropertyCount = (uint)array.Length, BasedOn = parent };
                window.Check(Native.ControlStyleCreate(window.Handle, &options, &handle));
            }
            identity.Value = handle;
            try { apply(handle); } finally { window.Check(Native.ControlStyleRelease(handle)); }
        }
        if (BasedOn is { } parent) parent.WithNativeHandle(window, Create);
        else Create(0);
    }
    internal unsafe bool TryApply(Window window, ulong handle, bool tooltip = false)
    {
        if (!identities.TryGetValue(window, out var identity)) return false;
        uint applied = 0;
        window.Check(tooltip ? Native.WindowTrySetTooltipStyle(handle, identity.Value, &applied) :
            Native.ControlTrySetStyle(handle, identity.Value, &applied));
        return applied != 0;
    }
}

public sealed unsafe partial class Window
{
    private ControlStyle? tooltipStyle;
    public ControlStyle? TooltipStyle { get { Guard(); return tooltipStyle; } set => SetTooltipStyle(value); }
    public Window SetTooltipStyle(ControlStyle? value)
    {
        Guard();
        if (value is null) Check(Native.WindowSetTooltipStyle(Handle, 0));
        else
        {
            if (value.Target != StyleTarget.Tooltip) throw new ArgumentException("Expected a Tooltip style.", nameof(value));
            if (!value.TryApply(this, Handle, tooltip: true))
                value.WithNativeHandle(this, handle => Check(Native.WindowSetTooltipStyle(Handle, handle)));
        }
        tooltipStyle = value;
        return this;
    }
    public Window SetTooltipStyleValues(StylePart part, PartStyleValues values)
    {
        Guard(); ArgumentNullException.ThrowIfNull(values);
        values.Validate(StyleTarget.Tooltip, part);
        using var pins = new Pins();
        var records = new List<Native.StyleProperty>();
        values.Append(records, part, 0, pins);
        var array = records.ToArray();
        fixed (Native.StyleProperty* p = array)
            Check(Native.WindowSetTooltipStyleValues(Handle, (uint)part, p, (uint)array.Length));
        return this;
    }
    public PartStyleValues GetTooltipStyleValues(StylePart part, bool effective = false)
    {
        Guard();
        uint count = 0;
        Check(Native.WindowGetTooltipStyleValues(Handle, (uint)part, effective ? 1u : 0u, null, 0, &count));
        var records = new Native.StyleProperty[count];
        fixed (Native.StyleProperty* p = records)
            Check(Native.WindowGetTooltipStyleValues(Handle, (uint)part, effective ? 1u : 0u, p, count, &count));
        return PartStyleValues.FromNative(records);
    }
}

public abstract unsafe partial class Element
{
    private ControlStyle? controlStyle;
    public ControlStyle? ControlStyle { get { Window.Guard(); return controlStyle; } set => SetControlStyle(value); }
    public Element SetControlStyle(ControlStyle? value)
    {
        Window.Guard();
        if (value is null) Window.Check(Native.ControlSetStyle(Handle, 0));
        else if (!value.TryApply(Window, Handle))
            value.WithNativeHandle(Window, handle => Window.Check(Native.ControlSetStyle(Handle, handle)));
        controlStyle = value;
        return this;
    }
    public Element SetControlStyleValues(StylePart part, PartStyleValues values)
    {
        Window.Guard(); ArgumentNullException.ThrowIfNull(values);
        using var pins = new Window.Pins();
        var records = new List<Native.StyleProperty>(); values.Append(records, part, 0, pins);
        var array = records.ToArray();
        fixed (Native.StyleProperty* p = array)
            Window.Check(Native.ControlSetStyleValues(Handle, (uint)part, p, (uint)array.Length));
        return this;
    }
    public PartStyleValues GetControlStyleValues(StylePart part, bool effective = false)
    {
        Window.Guard();
        uint count = 0;
        Window.Check(Native.ControlGetStyleValues(Handle, (uint)part, effective ? 1u : 0u, null, 0, &count));
        var records = new Native.StyleProperty[count];
        fixed (Native.StyleProperty* p = records)
            Window.Check(Native.ControlGetStyleValues(Handle, (uint)part, effective ? 1u : 0u, p, count, &count));
        return PartStyleValues.FromNative(records);
    }
}

public sealed partial class Toggle
{
    public ControlStyle? Style { get => ControlStyle; set => SetControlStyle(value); }
    public Toggle SetStyle(ControlStyle? value) { SetControlStyle(value); return this; }
    public Toggle SetStyleValues(StylePart part, PartStyleValues values) { SetControlStyleValues(part, values); return this; }
    public PartStyleValues GetStyleValues(StylePart part, bool effective = false) => GetControlStyleValues(part, effective);
}

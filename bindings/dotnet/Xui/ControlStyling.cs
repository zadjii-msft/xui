using System.Runtime.CompilerServices;

namespace Xui;

public enum StyleTarget : uint { Toggle }
public enum StylePart : uint { Root, Label, Indicator, Mark }
[Flags]
public enum StyleState : ulong { Focused = 1, Checked = 2, Hovered = 4, Pressed = 8, Disabled = 16 }

public sealed record PartStyleValues
{
    public ThemeColor? Background { get; init; }
    public ThemeColor? Foreground { get; init; }
    public ThemeColor? BorderBrush { get; init; }
    public Insets? BorderThickness { get; init; }
    public Insets? Padding { get; init; }
    public float? CornerRadius { get; init; }
    public float? Size { get; init; }
    internal static readonly PartStyleValues Empty = new();

    internal void Validate(StyleTarget target, StylePart part)
    {
        if (target != StyleTarget.Toggle || !Enum.IsDefined(part))
            throw new ArgumentException("Unsupported style target or part.");
        uint mask = (Background.HasValue ? 1u : 0) | (Foreground.HasValue ? 2u : 0) |
            (BorderBrush.HasValue ? 4u : 0) | (BorderThickness.HasValue ? 8u : 0) |
            (Padding.HasValue ? 16u : 0) | (CornerRadius.HasValue ? 32u : 0) | (Size.HasValue ? 64u : 0);
        uint allowed = part switch { StylePart.Root => 63, StylePart.Indicator => 109, _ => 2 };
        if ((mask & ~allowed) != 0) throw new ArgumentException("Unsupported property on this style part.");
        new ButtonStyleValues { Background = Background, Foreground = Foreground, BorderBrush = BorderBrush,
            BorderThickness = BorderThickness, Padding = Padding, CornerRadius = CornerRadius }.Validate();
        if (Size is { } size) ButtonStyleValues.Dimension(size);
    }
    internal unsafe void Append(List<Native.StyleProperty> records, StylePart part, ulong state)
    {
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
        void Number(uint property, float? number)
        {
            if (number is not { } n) return;
            var r = Record(property, 3); r.Number = n; records.Add(r);
        }
        Color(1, Background); Color(2, Foreground); Color(4, BorderBrush);
        Edges(8, BorderThickness); Edges(16, Padding); Number(32, CornerRadius); Number(64, Size);
    }
    internal static PartStyleValues FromNative(IEnumerable<Native.StyleProperty> records)
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
        if (target != StyleTarget.Toggle) throw new ArgumentOutOfRangeException(nameof(target));
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
            r.Values.Validate(target, r.Part);
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
        foreach (var p in Parts) p.Values.Append(records, p.Part, 0);
        foreach (var r in Rules) r.Values.Append(records, r.Part, (ulong)r.State);
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
    internal unsafe bool TryApply(Window window, ulong handle)
    {
        if (!identities.TryGetValue(window, out var identity)) return false;
        uint applied = 0;
        window.Check(Native.ControlTrySetStyle(handle, identity.Value, &applied));
        return applied != 0;
    }
}

public abstract unsafe partial class Control
{
    private ControlStyle? controlStyle;
    public ControlStyle? ControlStyle { get { Window.Guard(); return controlStyle; } set => SetControlStyle(value); }
    public Control SetControlStyle(ControlStyle? value)
    {
        Window.Guard();
        if (value is null) Window.Check(Native.ControlSetStyle(Handle, 0));
        else if (!value.TryApply(Window, Handle))
            value.WithNativeHandle(Window, handle => Window.Check(Native.ControlSetStyle(Handle, handle)));
        controlStyle = value;
        return this;
    }
    public Control SetControlStyleValues(StylePart part, PartStyleValues values)
    {
        Window.Guard(); ArgumentNullException.ThrowIfNull(values);
        var records = new List<Native.StyleProperty>(); values.Append(records, part, 0);
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

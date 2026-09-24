using Android.Content;
using Android.Content.Res;
using Android.Graphics;
using Android.Graphics.Drawables;
using Android.Util;
using Android.Widget;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal sealed class NativeTypography(TextView view)
{
    private readonly float size = view.TextSize;
    private readonly Typeface? typeface = view.Typeface;

    internal void Apply(Typography? typography)
    {
        if (typography is null)
        {
            view.SetTextSize(ComplexUnitType.Px, size);
            view.Typeface = typeface;
            return;
        }
        view.SetTextSize(ComplexUnitType.Sp, typography.ResolvedFontSize);
        view.SetTypeface(typeface, typography.ResolvedFontWeight == 700 ? TypefaceStyle.Bold : TypefaceStyle.Normal);
    }
}

internal sealed class NativeThemePalette : IDisposable
{
    internal ColorStateList Text { get; }
    internal ColorStateList? Hint { get; }
    internal ColorStateList? Button { get; }
    internal ColorStateList? Input { get; }
    internal ColorStateList? Progress { get; }
    internal ColorStateList? ButtonBackground { get; }
    internal int Background { get; }
    private readonly List<ColorStateList> owned = [];

    internal NativeThemePalette(Context context, ThemeSettings theme)
    {
        bool dark = theme.Mode == ThemeMode.Dark ||
            (theme.Mode == ThemeMode.System &&
             (context.Resources!.Configuration!.UiMode & UiMode.NightMask) == UiMode.NightYes);
        using var styled = new global::Android.Views.ContextThemeWrapper(context, dark
            ? global::Android.Resource.Style.ThemeMaterialNoActionBar
            : global::Android.Resource.Style.ThemeMaterialLightNoActionBar);
        using var label = new TextView(styled);
        using var input = new EditText(styled);
        using var checkbox = new global::Android.Widget.CheckBox(styled);
        using var progress = new ProgressBar(styled, null, global::Android.Resource.Attribute.ProgressBarStyleHorizontal);
        using var colors = styled.ObtainStyledAttributes([global::Android.Resource.Attribute.ColorBackground,
            global::Android.Resource.Attribute.ColorButtonNormal, global::Android.Resource.Attribute.ColorAccent,
            global::Android.Resource.Attribute.ColorControlNormal, global::Android.Resource.Attribute.ColorControlActivated,
            global::Android.Resource.Attribute.DisabledAlpha]);
        Background = colors.GetColor(0, dark ? Color.Black : Color.White);
        ButtonBackground = colors.GetColorStateList(1);
        var textColors = label.TextColors ?? throw new InvalidOperationException("Android did not provide native text colors.");
        if (theme.Resources.Background is { } background) Background = Argb(dark ? background.Dark : background.Light);
        Text = theme.Resources.Foreground is { } text
            ? Recolor(textColors, textColors.DefaultColor, Argb(dark ? text.Dark : text.Light), preserveAlpha: false) : textColors;
        Hint = input.HintTextColors;
        Button = checkbox.ButtonTintList;
        Input = input.BackgroundTintList;
        Progress = progress.ProgressTintList;
        int accent = colors.GetColor(2, Color.Black);
        var normal = colors.GetColorStateList(3) ?? textColors;
        int activated = colors.GetColor(4, new Color(accent));
        int disabledColor = normal.GetColorForState([-global::Android.Resource.Attribute.StateEnabled], new Color(normal.DefaultColor));
        if (!normal.IsStateful)
        {
            int alpha = (int)Math.Round(((uint)disabledColor >> 24) * colors.GetFloat(5, 0.3f));
            disabledColor = (disabledColor & 0xffffff) | (alpha << 24);
        }
        if (Button is null)
        {
            Button = new ColorStateList(
                [[-global::Android.Resource.Attribute.StateEnabled], [global::Android.Resource.Attribute.StateChecked], []],
                [disabledColor, activated, normal.DefaultColor]);
            owned.Add(Button);
        }
        if (Input is null)
        {
            Input = new ColorStateList(
                [[-global::Android.Resource.Attribute.StateEnabled], [global::Android.Resource.Attribute.StateFocused],
                    [global::Android.Resource.Attribute.StatePressed], []],
                [disabledColor, activated, activated, normal.DefaultColor]);
            owned.Add(Input);
        }
        if (theme.Resources.Accent is { } selected)
        {
            int replacement = Argb(dark ? selected.Dark : selected.Light);
            Button = Recolor(Button, activated, replacement);
            Input = Recolor(Input, activated, replacement);
            if (Progress is not null) Progress = Recolor(Progress, accent, replacement);
        }
    }

    private static int Argb(uint rgb) => unchecked((int)(0xff000000 | rgb));
    private ColorStateList Recolor(ColorStateList original, int originalColor, int replacement, bool preserveAlpha = true)
    {
        int[] attributes = [global::Android.Resource.Attribute.StateEnabled, global::Android.Resource.Attribute.StateFocused,
            global::Android.Resource.Attribute.StatePressed, global::Android.Resource.Attribute.StateHovered,
            global::Android.Resource.Attribute.StateChecked, global::Android.Resource.Attribute.StateSelected,
            global::Android.Resource.Attribute.StateActivated, global::Android.Resource.Attribute.StateWindowFocused];
        int count = 1 << attributes.Length;
        var states = new int[count + 1][];
        var colors = new int[count + 1];
        int Replace(int color, bool enabled) => enabled && (color & 0xffffff) == (originalColor & 0xffffff)
            ? (preserveAlpha ? color & unchecked((int)0xff000000) : unchecked((int)0xff000000)) | (replacement & 0xffffff) : color;
        for (int index = 0; index < count; index++)
        {
            states[index] = attributes.Select((attribute, bit) => (index & (1 << bit)) != 0 ? attribute : -attribute).ToArray();
            colors[index] = Replace(original.GetColorForState(states[index], new Color(original.DefaultColor)), (index & 1) != 0);
        }
        states[count] = [];
        colors[count] = Replace(original.DefaultColor, true);
        var result = new ColorStateList(states, colors);
        owned.Add(result);
        return result;
    }

    public void Dispose()
    {
        foreach (var colors in owned) colors.Dispose();
        owned.Clear();
    }
}

internal sealed class NativeThemeState
{
    private readonly global::Android.Views.View view;
    private readonly ColorStateList? text;
    private readonly ColorStateList? hint;
    private readonly ColorStateList? background;
    private readonly ColorStateList? button;
    private readonly ColorStateList? progress;
    private readonly ColorStateList? indeterminate;

    internal NativeThemeState(global::Android.Views.View view)
    {
        this.view = view;
        if (view is TextView label) { text = label.TextColors; hint = label.HintTextColors; }
        background = view.BackgroundTintList;
        if (view is CompoundButton choice) button = choice.ButtonTintList;
        if (view is ProgressBar indicator) { progress = indicator.ProgressTintList; indeterminate = indicator.IndeterminateTintList; }
    }

    internal void Apply(NativeThemePalette? palette)
    {
        if (view is TextView label)
        {
            label.SetTextColor(palette?.Text ?? text);
            if (view is EditText) label.SetHintTextColor(palette?.Hint ?? hint);
        }
        if (view is EditText) view.BackgroundTintList = palette?.Input ?? background;
        else if (view is global::Android.Widget.Button && view is not CompoundButton)
            view.BackgroundTintList = palette?.ButtonBackground ?? background;
        if (view is CompoundButton choice) choice.ButtonTintList = palette?.Button ?? button;
        if (view is ProgressBar indicator)
        {
            indicator.ProgressTintList = palette?.Progress ?? progress;
            indicator.IndeterminateTintList = palette?.Progress ?? indeterminate;
        }
    }
}

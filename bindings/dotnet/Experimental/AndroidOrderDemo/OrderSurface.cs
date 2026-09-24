using Android.Content;
using Android.Views;
using Android.Widget;

namespace Xui.Experimental.AndroidOrderDemo;

internal sealed class OrderSurface(Context context) : FrameLayout(context)
{
    public override WindowInsets? OnApplyWindowInsets(WindowInsets? insets)
    {
        if (insets is null) return null;
        if (OperatingSystem.IsAndroidVersionAtLeast(30))
        {
            var padding = insets.GetInsets(WindowInsets.Type.SystemBars() |
                WindowInsets.Type.DisplayCutout() | WindowInsets.Type.Ime());
            SetPadding(padding.Left, padding.Top, padding.Right, padding.Bottom);
        }
        else
        {
            SetPadding(insets.SystemWindowInsetLeft, insets.SystemWindowInsetTop,
                insets.SystemWindowInsetRight, insets.SystemWindowInsetBottom);
        }
        return insets;
    }
}

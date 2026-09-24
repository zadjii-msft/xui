using Android.Content;
using Android.Content.Res;
using Android.Graphics.Drawables;
using Android.Util;
using Android.Views;
using Android.Widget;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

public sealed partial class AndroidBackend
{
    private NativeThemePalette? palette;
    private ThemeSettings? theme;
    private Drawable? originalSurfaceBackground;
    private bool themeStarted;
    private ThemeConfigurationObserver? themeObserver;
    private bool refreshPosted;
    internal NativeThemePalette? Palette => palette;

    public void ValidateTheme(ThemeSettings? value)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (value is not null && !Enum.IsDefined(value.Mode)) throw new ArgumentOutOfRangeException(nameof(value));
    }

    public void ApplyTheme(ThemeSettings? value)
    {
        ValidateTheme(value);
        if (value is null && !themeStarted) return;
        if (!themeStarted)
        {
            originalSurfaceBackground = surface.Background;
            themeStarted = true;
        }
        NativeThemePalette? next = value is null ? null : new NativeThemePalette(surface.Context!, value);
        var previous = palette;
        palette = next;
        theme = value;
        try
        {
            if (next is null) surface.Background = originalSurfaceBackground;
            else surface.SetBackgroundColor(new global::Android.Graphics.Color(next.Background));
            foreach (var peer in peers) peer.ApplyTheme(next);
            if (value is not null && themeObserver is null)
            {
                var context = surface.Context!.ApplicationContext
                    ?? throw new InvalidOperationException("Android has no application context for theme changes.");
                themeObserver = new ThemeConfigurationObserver(this, context);
            }
            else if (value is null && themeObserver is not null)
            {
                themeObserver.Dispose();
                themeObserver = null;
            }
            if (value is null)
            {
                themeStarted = false;
                originalSurfaceBackground = null;
            }
        }
        finally { previous?.Dispose(); }
    }

    private void QueueThemeRefresh()
    {
        if (disposed || refreshPosted || theme is null) return;
        refreshPosted = true;
        try
        {
            dispatcher.Post(() =>
            {
                refreshPosted = false;
                if (disposed || theme is null) return;
                try
                {
                    ApplyTheme(theme);
                    foreach (var peer in peers) peer.ApplyTypography();
                }
                catch (Exception error)
                {
                    Log.Error("Xui.Android", error.ToString());
                    throw;
                }
            });
        }
        catch
        {
            refreshPosted = false;
            throw;
        }
    }

    private void ReleaseTheme()
    {
        var failures = new List<Exception>();
        void Cleanup(System.Action action)
        {
            try { action(); }
            catch (Exception error) { failures.Add(error); }
        }
        if (themeObserver is not null) Cleanup(themeObserver.Dispose);
        themeObserver = null;
        if (themeStarted) Cleanup(() => surface.Background = originalSurfaceBackground);
        if (palette is not null) Cleanup(palette.Dispose);
        palette = null;
        theme = null;
        originalSurfaceBackground = null;
        if (failures.Count != 0) throw new AggregateException("Android theme cleanup failed.", failures);
    }

    private sealed class ThemeConfigurationObserver : Java.Lang.Object, IComponentCallbacks
    {
        private readonly WeakReference<AndroidBackend> owner;
        private Context? context;
        internal ThemeConfigurationObserver(AndroidBackend backend, Context application)
        {
            owner = new(backend);
            context = application;
            try { application.RegisterComponentCallbacks(this); }
            catch
            {
                context = null;
                Dispose();
                throw;
            }
        }
        public void OnConfigurationChanged(Configuration newConfig)
        {
            if (owner.TryGetTarget(out var backend)) backend.QueueThemeRefresh();
        }
        public void OnLowMemory() { }
        protected override void Dispose(bool disposing)
        {
            if (disposing && context is { } application)
            {
                context = null;
                try { application.UnregisterComponentCallbacks(this); }
                finally { base.Dispose(disposing); }
                return;
            }
            base.Dispose(disposing);
        }
    }
}

internal sealed partial class AndroidPeer
{
    private readonly Dictionary<TextView, NativeTypography> typography = [];
    private readonly Dictionary<View, NativeThemeState> themeStates = [];

    public void ValidateTypography(Typography? value)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (value is not null && Element.Kind is not (ElementKind.Label or ElementKind.Button or
            ElementKind.TextInput or ElementKind.Toggle or ElementKind.CheckBox))
            throw new NotSupportedException("This native control does not support portable typography.");
    }

    private void InitializePresentation()
    {
        foreach (var view in owned) RegisterPresentation(view);
    }

    private void RegisterPresentation(View view)
    {
        if (view is TextView text)
        {
            var state = new NativeTypography(text);
            typography.Add(text, state);
            if (Element is Control { Typography: { } style }) state.Apply(style);
        }
        if (view is TextView or ProgressBar)
        {
            var state = new NativeThemeState(view);
            themeStates.Add(view, state);
            if (Backend.Palette is { } current) state.Apply(current);
        }
    }

    internal void ApplyTypography()
    {
        var value = (Element as Control)?.Typography;
        foreach (var state in typography.Values) state.Apply(value);
        View.RequestLayout();
    }

    internal void ApplyTheme(NativeThemePalette? value)
    {
        foreach (var state in themeStates.Values) state.Apply(value);
        tabs?.ApplyTheme(value);
        navigation?.ApplyTheme(value);
        singleChoice?.ApplyTheme(value);
    }
}

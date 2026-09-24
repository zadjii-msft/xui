using Android.Views;
using Xui.Experimental.Portable;
using Size = Xui.Experimental.Portable.Size;

namespace Xui.Experimental.Android;

public sealed partial class AndroidBackend : IHostViewportBackend
{
    public IDisposable ObserveViewport(Action<Size> changed)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(changed);
        return new ViewportObservation(surface, dispatcher, changed);
    }

    private sealed class ViewportObservation : Java.Lang.Object, ViewTreeObserver.IOnGlobalLayoutListener
    {
        private readonly WeakReference<View> surface;
        private readonly AndroidDispatcher dispatcher;
        private Action<Size>? changed;
        private Size? last;
        private bool retired;

        internal ViewportObservation(View view, AndroidDispatcher dispatcher, Action<Size> changed)
        {
            surface = new(view);
            this.dispatcher = dispatcher;
            this.changed = changed;
            var observer = view.ViewTreeObserver ?? throw new InvalidOperationException("Android has no native layout observer.");
            observer.AddOnGlobalLayoutListener(this);
            try { OnGlobalLayout(); }
            catch (Exception openingError)
            {
                try { Dispose(); }
                catch (Exception cleanupError) { throw new AggregateException(openingError, cleanupError); }
                throw;
            }
        }

        public void OnGlobalLayout()
        {
            if (retired) return;
            if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Native viewport notifications require the UI thread.");
            if (!surface.TryGetTarget(out var view) || view.Handle == IntPtr.Zero)
                throw new ObjectDisposedException("The native host surface was retired before its viewport observer.");
            float density = view.Resources!.DisplayMetrics!.Density;
            var current = new Size(Math.Max(0, view.Width - view.PaddingLeft - view.PaddingRight) / density,
                Math.Max(0, view.Height - view.PaddingTop - view.PaddingBottom) / density);
            if (last == current) return;
            last = current;
            changed!(current);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && !retired)
            {
                if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Retire native viewport observation on the UI thread.");
                retired = true;
                changed = null;
                if (surface.TryGetTarget(out var view) && view.Handle != IntPtr.Zero &&
                    view.ViewTreeObserver is { IsAlive: true } observer)
                    observer.RemoveOnGlobalLayoutListener(this);
            }
            base.Dispose(disposing);
        }
    }
}

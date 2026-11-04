using System.Runtime.InteropServices;

namespace Xui;

internal sealed class CallbackLifetime : IDisposable
{
    private HashSet<Registration>? registrations = [];
    internal bool Retired { get; private set; }
    internal int Count => registrations?.Count ?? 0;

    internal Registration Register(object target, Action release)
    {
        ObjectDisposedException.ThrowIf(Retired, this);
        var registration = new Registration(this, target, release);
        try { registrations!.Add(registration); }
        catch { registration.Dispose(); throw; }
        return registration;
    }

    public void Dispose()
    {
        if (Retired) return;
        Retired = true;
        var retired = registrations!;
        registrations = null;
        List<Exception>? failures = null;
        foreach (var registration in retired)
        {
            try { registration.Dispose(); }
            catch (Exception error) { (failures ??= []).Add(error); }
        }
        if (failures is not null) throw new AggregateException("Callback registration cleanup failed.", failures);
    }

    internal sealed class Registration : IDisposable
    {
        private readonly CallbackLifetime owner;
        private GCHandle root;
        private Action? release;
        private int dispatches;
        internal bool Retired { get; private set; }
        internal bool Released => !root.IsAllocated;
        internal nint Context => GCHandle.ToIntPtr(root);
        internal Registration(CallbackLifetime owner, object target, Action release)
        {
            this.owner = owner;
            this.release = release;
            root = GCHandle.Alloc(target, GCHandleType.Weak);
        }
        internal Call Enter()
        {
            if (Retired || owner.Retired) return default;
            ++dispatches;
            return new(this);
        }
        public void Dispose()
        {
            if (Retired) return;
            Retired = true;
            owner.registrations?.Remove(this);
            if (dispatches == 0) Release();
        }
        private void Release()
        {
            var callback = release;
            release = null;
            try { callback?.Invoke(); }
            finally { if (root.IsAllocated) root.Free(); }
        }
        private void Leave()
        {
            if (--dispatches == 0 && Retired) Release();
        }
        internal readonly struct Call(Registration? registration) : IDisposable
        {
            internal bool Active => registration is not null;
            public void Dispose() => registration?.Leave();
        }
    }
}

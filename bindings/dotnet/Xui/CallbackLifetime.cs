using System.Runtime.InteropServices;

namespace Xui;

internal sealed class CallbackLifetime : IDisposable
{
    private Registration? first;
    private int count;
    internal bool Retired { get; private set; }
    internal int Count => count;
    private void Attach(Registration registration)
    {
        ObjectDisposedException.ThrowIf(Retired, this);
        registration.Next = first;
        if (first is not null) first.Previous = registration;
        first = registration;
        ++count;
    }
    private void Detach(Registration registration)
    {
        if (registration.Previous is not null) registration.Previous.Next = registration.Next;
        else first = registration.Next;
        if (registration.Next is not null) registration.Next.Previous = registration.Previous;
        registration.Previous = registration.Next = null;
        --count;
    }

    internal Registration Register(object target, Action release)
    {
        ObjectDisposedException.ThrowIf(Retired, this);
        return new ActionRegistration(this, target, release);
    }

    private sealed class ActionRegistration : Registration
    {
        private Action? release;
        internal ActionRegistration(CallbackLifetime owner, object target, Action release) : base(owner)
        {
            this.release = release;
            try { Bind(target); } catch { Dispose(); throw; }
        }
        protected override void Release()
        {
            var callback = release;
            release = null;
            callback?.Invoke();
        }
    }
    public void Dispose()
    {
        if (Retired) return;
        Retired = true;
        for (var registration = first; registration is not null; registration = registration.Next)
            registration.Retired = true;
        List<Exception>? failures = null;
        while (first is { } registration)
        {
            try { registration.Dispose(); }
            catch (Exception error) { (failures ??= []).Add(error); }
        }
        if (failures is not null) throw new AggregateException("Callback registration cleanup failed.", failures);
    }

    internal abstract class Registration : IDisposable
    {
        private CallbackLifetime? owner;
        private GCHandle root;
        private int dispatches;
        private bool detached;
        internal Registration? Previous, Next;
        internal bool Retired { get; set; }
        internal bool Released => !root.IsAllocated;
        internal nint Context => GCHandle.ToIntPtr(root);
        protected Registration(CallbackLifetime owner)
        {
            this.owner = owner;
            owner.Attach(this);
        }
        protected void Rebind(CallbackLifetime lifetime, object target)
        {
            if (!Released || !Retired) throw new InvalidOperationException("The callback registration is still in use.");
            owner = lifetime;
            detached = Retired = false;
            lifetime.Attach(this);
            try { Bind(target); } catch { Dispose(); throw; }
        }
        protected void Bind(object target)
        {
            root = GCHandle.Alloc(target, GCHandleType.Weak);
        }
        internal Call Enter()
        {
            if (Retired) return default;
            ++dispatches;
            return new(this);
        }
        public void Dispose()
        {
            if (detached) return;
            Retired = true;
            detached = true;
            owner!.Detach(this);
            owner = null;
            if (dispatches == 0) FinishRelease();
        }
        private void FinishRelease()
        {
            try { Release(); }
            finally { if (root.IsAllocated) root.Free(); }
        }
        protected abstract void Release();
        private void Leave()
        {
            if (--dispatches == 0 && Retired) FinishRelease();
        }
        internal readonly struct Call(Registration? registration) : IDisposable
        {
            internal bool Active => registration is not null;
            public void Dispose() => registration?.Leave();
        }
    }
}

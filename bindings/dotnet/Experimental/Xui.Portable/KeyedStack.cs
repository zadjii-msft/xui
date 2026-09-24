namespace Xui.Experimental.Portable;

public sealed class KeyedUpdateException : InvalidOperationException
{
    public bool ModelCommitted { get; }
    internal KeyedUpdateException(bool modelCommitted, Exception inner)
        : base($"Keyed update {(modelCommitted ? "failed after model commit" : "was rejected before model commit")}: {inner.Message}", inner)
    {
        ModelCommitted = modelCommitted;
    }
}

public sealed class KeyedItem
{
    public string Key { get; }
    internal Type ComponentType { get; }
    internal Func<Host, IPortableComponent> Factory { get; }
    internal Action<IPortableComponent>? Update { get; }

    private KeyedItem(string key, Type componentType, Func<Host, IPortableComponent> factory, Action<IPortableComponent>? update)
    {
        ArgumentException.ThrowIfNullOrEmpty(key);
        Key = Values.Text(key);
        ComponentType = componentType;
        Factory = factory;
        Update = update;
    }

    public static KeyedItem Create<T>(string key, Func<Host, T> create, Action<T>? update = null)
        where T : class, IPortableComponent
    {
        ArgumentNullException.ThrowIfNull(create);
        return new(key, typeof(T), host => create(host), update is null ? null : component => update((T)component));
    }
}

public class KeyedStack : Stack
{
    internal sealed record Entry(string Key, Type ComponentType, IPortableComponent Component, Element Root);
    internal List<Entry> Entries { get; set; } = [];

    internal KeyedStack(Host host, Axis axis, ElementKind kind = ElementKind.Stack) : base(host, axis, kind) { }

    public void Reconcile(IReadOnlyList<KeyedItem> items)
    {
        VerifyAccess();
        if (this is PageView) throw new InvalidOperationException("Use SetPages to update owned page content and selection together.");
        Owner.Reconcile(this, items);
    }

    internal override void Release()
    {
        Entries.Clear();
        base.Release();
    }
}

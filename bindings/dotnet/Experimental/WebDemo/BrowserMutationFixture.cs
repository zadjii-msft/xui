#if DEBUG
using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

internal sealed class BrowserMutationFixture : IDisposable
{
    private readonly Host host;
    private readonly KeyedStack rows;
    private readonly IJSInProcessObjectReference module;
    private int changes;
    private int submits;

    public BrowserMutationFixture(IJSInProcessObjectReference module)
    {
        this.module = module;
        host = new Host(new BrowserDispatcher(error => module.InvokeVoid("reportError", "errors", error.ToString())));
        try
        {
            using (var build = host.BeginBuild())
            {
                rows = host.KeyedStack(Axis.Vertical);
                rows.Spacing(8).Padding(16);
                host.SetContent(rows);
                build.Complete();
            }
            Reconcile("a,b,c");
            Attach();
        }
        catch { host.Dispose(); throw; }
    }

    public object State() => new
    {
        attached = host.IsAttached, changes, submits,
        rows = rows.Children.Select(root => new
        {
            key = ((TextInput)root.Children[0]).AutomationId,
            text = ((TextInput)root.Children[0]).Text
        }).ToArray()
    };

    public void Reconcile(string keys)
    {
        var items = keys.Length == 0 ? [] : keys.Split(',').Select(key =>
            key == "factory-error"
                ? KeyedItem.Create<Row>(key, _ => throw new InvalidOperationException("Intentional keyed factory failure."))
                : key.StartsWith('!')
                    ? KeyedItem.Create<ReplacementRow>(key[1..], owner => new ReplacementRow(owner, key[1..], this))
                    : KeyedItem.Create<Row>(key, owner => new Row(owner, key, this))).ToArray();
        rows.Reconcile(items);
    }

    public void Attach() => host.Attach(new DomBackend(module, "app", "errors"));
    public void Dispose() => host.Dispose();

    private class Row : IPortableComponent
    {
        public Element Root { get; }
        public Row(Host host, string key, BrowserMutationFixture fixture)
        {
            using var build = host.BeginBuild();
            var root = host.Stack(Axis.Vertical);
            var input = host.TextInput($"Row {key}");
            input.AutomationId = key;
            input.Text = $"Value {key}";
            input.Changed += _ => fixture.changes++;
            input.Submitted += () => fixture.submits++;
            root.Add(input);
            Root = root;
            host.SetContent(root);
            build.Complete();
        }
    }

    private sealed class ReplacementRow(Host host, string key, BrowserMutationFixture fixture) : Row(host, key, fixture);
}
#endif

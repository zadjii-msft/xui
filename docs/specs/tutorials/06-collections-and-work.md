# 6. Collections and background work

This independent exercise shows a virtual list.
It does not change the task-card sample or save its in-memory tasks.
Use it when your application needs more rows than a fixed composition can reasonably contain.

## Supply an immutable source

Create a separate handwritten C# application with the XUI binding reference.
This complete `Program.cs` describes one million rows without allocating a million controls:

```csharp
using Xui;

internal sealed class Rows : IReadOnlyImmutableSource
{
    public ulong Count => 1_000_000;
    public ItemKey Key(ulong index) => new(index + 1, 1);
    public ulong? Find(ItemKey key) =>
        key.Version == 1 && key.Id > 0 && key.Id <= Count ? key.Id - 1 : null;
    public ItemContent Item(ulong index, ulong column = 0) =>
        new($"Task {index + 1}", "Immutable demonstration row");
}

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        using var window = new Window("Virtual tasks", 640, 480);
        var items = window.ItemsView("Tasks");
        using (var source = window.ImmutableSource(new Rows()))
            items.SetSource(source);
        window.SetContent(window.Stack().Padding(16).Add(items, flex: 1));
        window.Run();
    }
}
```

The control retains the source after the temporary source wrapper is disposed.
Only requested row content crosses the binding.
The [feature sample](../../../bindings/dotnet/Sample/FeatureDemo.cs) uses this source pattern with additional controls.

## Keep identities stable

`Count` and the key mapping stay fixed for one snapshot.
`Key` maps an index to a stable identity.
`Find` performs the inverse mapping without a scan.
The two operations agree on the key version.
Do not use a mutable row position as a permanent identity after sorting or replacement.

Callbacks must be short and nonblocking.
Do not query a database, enumerate a directory, or call another source callback from `Item`.
Primary and secondary binding fields each have a 1,024-byte UTF-8 limit.
An oversized field is an error, not a reason to materialize every row.

For selection, use focused keys and compact membership queries.
Do not expand select-all into a million-element managed array.
For hierarchical data, use TreeView's owner-bound requests rather than inventing list expansion semantics.

## Add background work deliberately

The bindings do not install a synchronization context or marshal worker callbacks automatically.
An `await` continuation is therefore not proof that you are back on the UI thread.
Do not call `SetSource`, a control setter, or a completion token from a worker.

Use this sequence when integrating a data service:

1. On the UI thread, assign a request identity and establish cancellation ownership.
2. On a worker, fetch data into an application-owned immutable snapshot.
3. Deliver the result through an application-owned UI queue.
4. On the UI thread, reject canceled, replaced, or closed-owner results before applying the snapshot.

Build that queue with the host's supported delivery mechanism.
Do not substitute a timer callback on a thread-pool thread.
The [C++ collection contract](../collections.md) and [binding ownership contract](../bindings.md#ownership-and-data-limits) describe the available boundaries.
The file-explorer sources provide a larger controller example; they are not required by this tutorial.

Tree and map tokens belong to one control.
Successful completion consumes a token.
Cancellation and window disposal revoke it.
Rust request wrappers cancel on drop; C# request wrappers require disposal.

## Check the result

Scroll the list and confirm that it remains responsive.
Inspect fetch counts if your application adds instrumentation.
Do not turn this functional exercise into a frame-time or memory guarantee.
Test stale completion, cancellation, and owner closure before connecting real background work.

Continue with [reload and error diagnosis](07-reload-and-errors.md).

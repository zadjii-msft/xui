using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public readonly record struct MillerColumn(string Title, ImmutableSource Source, ItemKey? Selected = null);
public readonly record struct MillerItemEvent(uint Column, ItemKey Key);

public sealed unsafe partial class MillerColumns
{
    public const int MaxColumns = 32;
    private readonly Dictionary<uint, ItemsView> children = [];
    private Action<MillerItemEvent>? selectionChanged, itemActivated;

    public event Action<MillerItemEvent> SelectionChanged
    {
        add { Window.Guard(); Subscribe(); selectionChanged += value; }
        remove { Window.Guard(); selectionChanged -= value; UnsubscribeIfEmpty(); }
    }
    public event Action<MillerItemEvent> ItemActivated
    {
        add { Window.Guard(); Subscribe(); itemActivated += value; }
        remove { Window.Guard(); itemActivated -= value; UnsubscribeIfEmpty(); }
    }
    private void Subscribe() => Window.SetMillerSubscription(Handle, (kind, item) =>
    {
        if (kind == EventKind.Selection) selectionChanged?.Invoke(item);
        else if (kind == EventKind.Click) itemActivated?.Invoke(item);
    });
    private void UnsubscribeIfEmpty()
    {
        if (selectionChanged is null && itemActivated is null) Window.SetMillerSubscription(Handle, null);
    }

    public uint ColumnCount => State.Count;
    public uint ActiveColumn
    {
        get => State.Active;
        set { Window.Guard(); Window.Check(Native.MillerActive(Handle, value)); }
    }
    public double ColumnWidth
    {
        get => State.Width;
        set { Window.Guard(); Window.Check(Native.MillerWidth(Handle, value)); }
    }
    public MillerColumns SetActiveColumn(uint value) { ActiveColumn = value; return this; }
    public MillerColumns SetColumnWidth(double value) { ColumnWidth = value; return this; }
    public double HorizontalOffset
    {
        get => ScrollState.Offset;
        set { Window.Guard(); Window.Check(Native.MillerScroll(Handle, value)); }
    }
    public double MaximumHorizontalOffset => ScrollState.Maximum;
    public MillerColumns SetHorizontalOffset(double value) { HorizontalOffset = value; return this; }
    private (double Offset, double Maximum) ScrollState
    {
        get
        {
            Window.Guard();
            double offset, maximum;
            Window.Check(Native.MillerScrollState(Handle, &offset, &maximum));
            return (offset, maximum);
        }
    }
    private (uint Count, uint Active, double Width) State
    {
        get
        {
            Window.Guard();
            uint count, active; double width;
            Window.Check(Native.MillerState(Handle, &count, &active, &width));
            return (count, active, width);
        }
    }

    public MillerColumns SetColumns(ReadOnlySpan<MillerColumn> columns)
    {
        Window.Guard();
        if (columns.Length > MaxColumns) throw new ArgumentOutOfRangeException(nameof(columns));
        using var pins = new Window.Pins();
        var records = new Native.MillerColumn[columns.Length];
        for (int i = 0; i < columns.Length; ++i)
        {
            var column = columns[i];
            ArgumentNullException.ThrowIfNull(column.Source);
            column.Source.BelongsTo(Window);
            records[i] = new()
            {
                Size = (uint)sizeof(Native.MillerColumn),
                Title = pins.Text(column.Title),
                Source = column.Source.Handle,
                HasSelection = column.Selected.HasValue ? 1u : 0u,
                SelectedId = column.Selected?.Id ?? 0,
                SelectedVersion = column.Selected?.Version ?? 0
            };
        }
        fixed (Native.MillerColumn* p = records) Window.Check(Native.MillerSetColumns(Handle, p, (uint)records.Length));
        return this;
    }

    public ItemsView Column(uint index)
    {
        Window.Guard();
        if (index >= MaxColumns) throw new ArgumentOutOfRangeException(nameof(index));
        if (!children.TryGetValue(index, out var child))
            children.Add(index, child = new(Window, Features.Child(this, index)));
        return child;
    }

    public void FocusColumn(uint index)
    {
        ActiveColumn = index;
        Column(index).Focus();
    }
}

public sealed unsafe partial class Window
{
    private readonly Dictionary<ulong, MillerSubscription> millerSubscriptions = [];
    internal void SetMillerSubscription(ulong handle, Action<EventKind, MillerItemEvent>? action)
    {
        Guard();
        if (action is null)
        {
            Check(Native.MillerSubscribe(handle, null, 0));
            if (millerSubscriptions.Remove(handle, out var old)) old.Free();
            return;
        }
        if (millerSubscriptions.TryGetValue(handle, out var current)) { current.Action = action; return; }
        var subscription = new MillerSubscription(this, action);
        try
        {
            Check(Native.MillerSubscribe(handle, &MillerTrampoline, GCHandle.ToIntPtr(subscription.Root)));
            millerSubscriptions.Add(handle, subscription);
        }
        catch
        {
            Native.MillerSubscribe(handle, null, 0);
            subscription.Free();
            throw;
        }
    }
    private sealed class MillerSubscription
    {
        internal readonly Window Window;
        internal Action<EventKind, MillerItemEvent> Action;
        internal GCHandle Root;
        internal MillerSubscription(Window window, Action<EventKind, MillerItemEvent> action)
        {
            Window = window; Action = action;
            Root = GCHandle.Alloc(this, GCHandleType.Weak);
        }
        internal void Free() { if (Root.IsAllocated) Root.Free(); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int MillerTrampoline(nint context, Native.MillerEvent* value)
    {
        MillerSubscription? subscription = null;
        try
        {
            subscription = GCHandle.FromIntPtr(context).Target as MillerSubscription;
            if (subscription is null) return 8;
            ++subscription.Window.callbacks;
            try { subscription.Action((EventKind)value->Kind, new(value->Column, new(value->Id, value->Version))); }
            finally { --subscription.Window.callbacks; }
            return 0;
        }
        catch (Exception error)
        {
            if (subscription is not null) subscription.Window.callbackError = error;
            return 8;
        }
    }
}

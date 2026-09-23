using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public sealed record ShellAction(ItemKey Key, string Label, string CanonicalVerb, bool Enabled);

/// <summary>A cancellable, selection-scoped Shell snapshot. Use only on its window's UI thread.</summary>
public sealed unsafe class ShellActionSession : IDisposable
{
    private readonly Window window;
    private readonly Func<bool> current;
    private readonly List<ShellAction> received = [];
    private GCHandle root;
    private ulong handle;
    private Exception? callbackError;

    public ShellActionSession(Window window, IReadOnlyList<string> paths, Func<bool> current)
    {
        this.window = window;
        this.current = current;
        ArgumentNullException.ThrowIfNull(current);
        window.VerifyAccess();
        if (paths.Count is < 1 or > 256) throw new ArgumentOutOfRangeException(nameof(paths));
        root = GCHandle.Alloc(this);
        try
        {
            using var pins = new Window.Pins();
            var values = paths.Select(pins.Text).ToArray();
            fixed (Native.Text* p = values)
                window.Check(Native.ShellActionsCreate(window.Handle, p, (uint)values.Length,
                    &Current, GCHandle.ToIntPtr(root), out handle));
        }
        catch { root.Free(); throw; }
        window.Closed += OnClosed;
    }

    public IReadOnlyList<ShellAction> Actions { get; private set; } = [];
    public bool Ready { get; private set; }
    public bool Finished { get; private set; }
    public bool IsDisposed => handle == 0;

    public void Read()
    {
        Guard();
        bool wasReady = Ready;
        received.Clear();
        window.Check(Native.ShellActionsRead(handle, wasReady ? null : &Receive, GCHandle.ToIntPtr(root), out uint state));
        if (callbackError is { } error) throw new InvalidOperationException("Shell snapshot callback failed.", error);
        Ready = state != 0;
        Finished = state == 2;
        if (state == 1 && !wasReady) Actions = received.ToArray();
    }

    public void Invoke(ItemKey key)
    {
        Guard();
        if (!current() || !Actions.Any(action => action.Key == key && action.Enabled))
            throw new InvalidOperationException("The Shell action is no longer available.");
        window.Check(Native.ShellActionsInvoke(handle, key.Id, key.Version));
    }

    public void ShowWindowsMenu()
    {
        Guard();
        window.Check(Native.ShellActionsWindows(handle));
    }

    private void Guard()
    {
        window.VerifyAccess();
        ObjectDisposedException.ThrowIf(handle == 0, this);
    }

    private void OnClosed(WindowClosedEventArgs _) => Dispose();

    public void Dispose()
    {
        if (handle == 0) return;
        window.VerifyAccess();
        window.Check(Native.ShellActionsDestroy(handle));
        handle = 0;
        window.Closed -= OnClosed;
        root.Free();
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static uint Current(nint context)
    {
        var session = (ShellActionSession)GCHandle.FromIntPtr(context).Target!;
        try { return session.current() ? 1u : 0u; }
        catch (Exception error) { session.callbackError = error; return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Receive(nint context, ulong id, ulong version, Native.Text label, Native.Text verb, uint enabled)
    {
        var session = (ShellActionSession)GCHandle.FromIntPtr(context).Target!;
        try
        {
            session.received.Add(new(new(id, version),
                Window.Encoding.GetString(new ReadOnlySpan<byte>(label.Data, checked((int)label.Length))),
                Window.Encoding.GetString(new ReadOnlySpan<byte>(verb.Data, checked((int)verb.Length))), enabled != 0));
            return 0;
        }
        catch (Exception error) { session.callbackError = error; return 8; }
    }
}

internal static unsafe partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_shell_actions_create")]
    internal static partial int ShellActionsCreate(ulong window, Text* paths, uint count,
        delegate* unmanaged[Cdecl]<nint, uint> current, nint context, out ulong result);
    [LibraryImport("xui", EntryPoint = "xui_shell_actions_read")]
    internal static partial int ShellActionsRead(ulong session,
        delegate* unmanaged[Cdecl]<nint, ulong, ulong, Text, Text, uint, int> receiver, nint context, out uint ready);
    [LibraryImport("xui", EntryPoint = "xui_shell_actions_invoke")]
    internal static partial int ShellActionsInvoke(ulong session, ulong id, ulong version);
    [LibraryImport("xui", EntryPoint = "xui_shell_actions_windows")]
    internal static partial int ShellActionsWindows(ulong session);
    [LibraryImport("xui", EntryPoint = "xui_shell_actions_destroy")]
    internal static partial int ShellActionsDestroy(ulong session);
}

namespace Xui;

public enum PreviewState : uint { Idle, Loading, Accepted, Unsupported, Failed, Retiring }
public enum PreviewReason : uint
{
    None, NoHandler, Restricted, UnsupportedProvider, MissingProvider, InitializationFailed,
    RenderFailed, TimedOut, BrokerFailed, ResourceLimit, Cancelled, Hidden, UnsupportedArchitecture, ActivationFailed
}
public enum PreviewPhase : uint { None, Policy, Discovery, Activation, Initialize, Render, Resize, Focus, Unload }
public enum PreviewCleanup : uint { None, Pending, Unloaded, BrokerTerminated, ProviderUnknown }
public readonly record struct PreviewStatus(ulong Generation, PreviewState State, PreviewReason Reason,
    PreviewPhase Phase, int HResult, PreviewCleanup Cleanup);

public sealed unsafe partial class Window
{
    public ShellPreview ShellPreview(string name)
    {
        Guard(); using var pins = new Pins();
        Check(Native.ShellPreviewCreate(Handle, pins.Text(name), out ulong control));
        return new(this, control);
    }
}
/// <summary>Explicit installed-handler preview. Read-only initialization is not a content or network sandbox.</summary>
public sealed unsafe class ShellPreview : Control
{
    internal ShellPreview(Window window, ulong handle) : base(window, handle) { }
    private Action<PreviewStatus>? changed;
    private void OnChanged(UiEvent e)
    {
        if (e.Kind != EventKind.Change) return;
        var status = Status;
        if (status.Generation == e.Value) changed?.Invoke(status);
    }
    public event Action<PreviewStatus> Changed
    {
        add { Window.Guard(); if (changed is null) Event += OnChanged; changed += value; }
        remove { Window.Guard(); changed -= value; if (changed is null) Event -= OnChanged; }
    }
    public PreviewStatus Status
    {
        get
        {
            Window.Guard();
            var value = new Native.PreviewStatus { Size = (uint)sizeof(Native.PreviewStatus), Version = 1 };
            Window.Check(Native.ShellPreviewGetStatus(Handle, ref value));
            return new(value.Generation, (PreviewState)value.State, (PreviewReason)value.Reason,
                (PreviewPhase)value.Phase, value.HResult, (PreviewCleanup)value.Cleanup);
        }
    }
    public ulong LoadLocal(string path)
    {
        Window.Guard(); using var pins = new Window.Pins();
        Window.Check(Native.ShellPreviewLoadLocal(Handle, pins.Text(path), out ulong generation));
        return generation;
    }
    public void Cancel(ulong generation) { Window.Guard(); Window.Check(Native.ShellPreviewCancel(Handle, generation)); }
    public void Unload() { Window.Guard(); Window.Check(Native.ShellPreviewUnload(Handle)); }
    public void FocusContent(bool reverse = false) { Window.Guard(); Window.Check(Native.ShellPreviewFocusContent(Handle, reverse ? 1u : 0u)); }
}

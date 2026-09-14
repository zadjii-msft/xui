using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Xui;

public sealed class XuiException(int status, string message, Exception? inner = null) : Exception(message, inner)
{
    public int Status { get; } = status;
}
public enum Theme : uint { Dark, Light, HighContrast }
public enum Axis : uint { Horizontal, Vertical }
public enum EventKind : uint { Click = 1, Change, Submit, Key, Selection, View, Preview, Cancel, Action, Dismiss, Request, FilterOpen }
public readonly record struct UiEvent(EventKind Kind, ulong Value);
public enum PropertyKind : uint
{
    Text = 1, Name, Enabled, Checked, FixedSize, MinimumSize, MaximumSize,
    AutoSize, Spacing, Padding, ScrollOffset, AutomationId, Theme, PreferredSize
}
public readonly record struct Property(Element Target, PropertyKind Kind, string? Text = null,
    float A = 0, float B = 0, float C = 0, float D = 0, ulong Integer = 0);
public sealed unsafe partial class Window : IDisposable
{
    internal ulong Handle { get; private set; }
    private readonly int thread = Environment.CurrentManagedThreadId;
    private readonly Dictionary<ulong, Subscription> subscriptions = [];
    private bool running;
    private int callbacks;
    private Exception? callbackError;
    private Action<UiEvent>? key;
    internal static readonly UTF8Encoding Encoding = new(false, true);

    public Window(string title = "XUI bindings", float width = 600, float height = 720, Theme theme = Theme.Dark, bool customTitlebar = false)
    {
        if (Native.VersionGet() != Native.Version) throw new XuiException(5, "The XUI runtime ABI version does not match.");
        var bytes = Utf8(title);
        fixed (byte* p = bytes)
        {
            var options = new Native.Options
            {
                Size = (uint)sizeof(Native.Options),
                Version = Native.Version,
                Title = Span(p, bytes),
                Width = width,
                Height = height,
                Theme = (uint)theme
            };
            ulong handle;
            if (customTitlebar) Check(Native.WindowCreateFeatures(&options, 1, &handle));
            else Check(Native.WindowCreate(in options, out handle));
            Handle = handle;
        }
    }
    internal void Guard()
    {
        if (thread != Environment.CurrentManagedThreadId) throw new XuiException(4, "Use the creating UI thread.");
        ObjectDisposedException.ThrowIf(Handle == 0, this);
    }
    internal static byte[] Utf8(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        if (text.Contains('\0')) throw new ArgumentException("Embedded NUL is not supported.", nameof(text));
        var bytes = Encoding.GetBytes(text);
        if (bytes.Length > 1048576) throw new ArgumentException("The UTF-8 string exceeds 1 MiB.", nameof(text));
        return bytes;
    }
    internal static Native.Text Span(byte* pointer, byte[] bytes) => new() { Data = pointer, Length = (uint)bytes.Length };
    internal void Check(int status)
    {
        if (status == 0) return;
        byte* bytes = stackalloc byte[1024];
        var copy = Native.ErrorCopy(bytes, 1024, out uint count, out _);
        var message = copy == 0 ? Encoding.GetString(bytes, (int)count) : "The native call failed.";
        throw new XuiException(status, message, status == 8 ? callbackError : null);
    }
    public Stack Stack(Axis axis = Axis.Vertical)
    {
        Guard(); Check(Native.StackCreate(Handle, (uint)axis, out var handle)); return new(this, handle);
    }
    internal ulong Create(uint kind, string name, Element? content = null)
    {
        Guard(); content?.BelongsTo(this);
        var bytes = Utf8(name);
        fixed (byte* p = bytes)
        {
            Check(Native.Create(Handle, kind, Span(p, bytes), content?.Handle ?? 0, out var handle));
            return handle;
        }
    }
    public Label Label(string text) => new(this, Create(3, text));
    public Button Button(string text) => new(this, Create(4, text));
    public Toggle Toggle(string text) => new(this, Create(5, text));
    public TextInput TextInput(string name) => new(this, Create(6, name));
    public ScrollView ScrollView(Element content, string name) => new(this, Create(7, name, content));
    public Image Image(string name) => new(this, Create(8, name));
    public FileList FileList(string name) => new(this, Create(9, name));
    public Window SetContent(Stack root) { Guard(); root.BelongsTo(this); Check(Native.Content(Handle, root.Handle)); return this; }
    public Window SetTheme(Theme theme)
    {
        Guard();
        var p = new Native.Property { Size = (uint)sizeof(Native.Property), Kind = 13, Target = Handle, Integer = (uint)theme };
        Check(Native.Update(Handle, &p, 1));
        return this;
    }
    public Window Update(params ReadOnlySpan<Property> properties)
    {
        Guard();
        if (properties.Length > 4096) throw new ArgumentOutOfRangeException(nameof(properties));
        var native = new Native.Property[properties.Length];
        using var pins = new Pins();
        for (int i = 0; i < properties.Length; ++i)
        {
            var p = properties[i]; p.Target.BelongsTo(this);
            native[i] = new()
            {
                Size = (uint)sizeof(Native.Property),
                Kind = (uint)p.Kind,
                Target = p.Target.Handle,
                Text = p.Text is null ? default : pins.Text(p.Text),
                A = p.A,
                B = p.B,
                C = p.C,
                D = p.D,
                Integer = p.Integer
            };
        }
        fixed (Native.Property* p = native) Check(Native.Update(Handle, p, (uint)native.Length));
        return this;
    }
    public event Action<UiEvent> Key
    {
        add { Guard(); SetSubscription(Handle, e => key?.Invoke(e)); key += value; }
        remove { Guard(); key -= value; if (key is null) SetSubscription(Handle, null); }
    }
    public void Run()
    {
        Guard();
        if (running) throw new XuiException(7, "The window is already running.");
        running = true;
        try { Check(Native.Run(Handle)); }
        finally { running = false; }
    }
    public void Close() { Guard(); Check(Native.Close(Handle)); }
    public int CallbackStatus { get { Guard(); Check(Native.CallbackError(Handle, out int status)); return status; } }
    public void Dispose()
    {
        if (Handle == 0) return;
        Guard();
        if (running || callbacks != 0) throw new XuiException(7, "Close the window and return from Run before Dispose.");
        Check(Native.WindowDestroy(Handle));
        Handle = 0;
        foreach (var s in subscriptions.Values) s.Free();
        subscriptions.Clear(); key = null;
    }
    internal void SetSubscription(ulong handle, Action<UiEvent>? action)
    {
        Guard();
        if (action is null)
        {
            Check(Native.Subscribe(handle, null, 0));
            if (subscriptions.Remove(handle, out var old)) old.Free();
            return;
        }
        if (subscriptions.TryGetValue(handle, out var current)) { current.Action = action; return; }
        var subscription = new Subscription(this, action);
        try
        {
            Check(Native.Subscribe(handle, &Trampoline, GCHandle.ToIntPtr(subscription.Root)));
            subscriptions.Add(handle, subscription);
        }
        catch
        {
            Native.Subscribe(handle, null, 0);
            subscription.Free(); throw;
        }
    }
    private sealed class Subscription
    {
        internal readonly Window Window;
        internal Action<UiEvent> Action;
        internal GCHandle Root;
        internal Subscription(Window window, Action<UiEvent> action)
        {
            Window = window; Action = action;
            Root = GCHandle.Alloc(this, GCHandleType.Weak);
        }
        internal void Free() { if (Root.IsAllocated) Root.Free(); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Trampoline(nint context, Native.Event* value)
    {
        Subscription? s = null;
        try
        {
            s = GCHandle.FromIntPtr(context).Target as Subscription;
            if (s is null) return 8;
            ++s.Window.callbacks;
            try { s.Action(new((EventKind)value->Kind, value->Value)); }
            finally { --s.Window.callbacks; }
            return 0;
        }
        catch (Exception error)
        {
            if (s is not null) s.Window.callbackError = error;
            return 8;
        }
    }
    internal sealed class Pins : IDisposable
    {
        private readonly List<GCHandle> handles = [];
        internal Native.Text Text(string text)
        {
            var bytes = Utf8(text);
            var handle = GCHandle.Alloc(bytes, GCHandleType.Pinned);
            try { handles.Add(handle); }
            catch { handle.Free(); throw; }
            return Span((byte*)handle.AddrOfPinnedObject(), bytes);
        }
        public void Dispose() { foreach (var handle in handles) handle.Free(); handles.Clear(); }
    }
}

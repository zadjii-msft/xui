using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

namespace Xui;

public readonly record struct NumericRange(double Minimum, double Maximum, double SmallStep = 1, double LargeStep = 10);
public readonly record struct TextSelection(ulong Start, ulong End);
public readonly record struct ItemKey(ulong Id, ulong Version = 0);
public readonly record struct SelectionInfo(ItemKey? Focused, uint StorageTerms);
public readonly record struct Choice(ulong Id, string Text, bool Enabled = true, ulong Version = 0);
public readonly record struct TextRun(string Text, bool Bold = false, bool Italic = false, bool Underline = false, string Link = "");
public readonly record struct RgbaColor(byte Red, byte Green, byte Blue, byte Alpha = 255)
{
    internal ulong Packed => Red | (ulong)Green << 8 | (ulong)Blue << 16 | (ulong)Alpha << 24;
    internal static RgbaColor FromPacked(ulong value) => new((byte)value, (byte)(value >> 8), (byte)(value >> 16), (byte)(value >> 24));
}
public enum ProgressState : uint { Determinate, Indeterminate, Paused, Error, Unknown }
public enum ItemsPresentation : uint { List, Tiles, Grouped }
public enum CompactNavigation : uint { Stacked, Overlay }
public enum DateTimePresentation : uint { Date, Time, Calendar }
public enum TextCommand : uint { Undo, Redo, Copy, Cut, Paste, SelectAll }
public enum StatusSeverity : uint { Information, Success, Warning, Error }
public enum HostState : uint { Idle, Loading, Ready, Playing, Paused, Stopped, Suspended, Error }
public enum ButtonBehavior : uint { Momentary, Repeat, Toggle, Dropdown }
public enum TrackSizing : uint { Fixed, Automatic, Star }
public readonly record struct GridTrack(TrackSizing Sizing = TrackSizing.Star, float Value = 1, float Minimum = 0, float Maximum = float.MaxValue);
public readonly record struct GridColumn(string Name, float Width = 120, bool Numeric = false, bool Filterable = false, bool Checkable = false);
public enum CommandKind : uint { Action, Submenu, Separator }
[Flags] public enum KeyModifiers : uint { None = 0, Control = 1, Shift = 2, Alt = 4 }
public readonly record struct Command(ulong Id, string Label, ulong Parent = 0, CommandKind Kind = CommandKind.Action,
    bool Enabled = true, bool? Checked = null, string ShortcutHint = "", string PinLabel = "");
public readonly record struct GeoPoint(double Latitude, double Longitude);
public readonly record struct MapMarker(ulong Id, GeoPoint Location, string Name);
public readonly record struct ScenePoint(float X, float Y);
public readonly record struct SceneColor(float Red, float Green, float Blue, float Alpha = 1);
public readonly record struct SceneTransform(double M11 = 1, double M12 = 0, double M21 = 0, double M22 = 1, double Dx = 0, double Dy = 0);
public readonly record struct SceneClip(float X, float Y, float Width, float Height);
public sealed record VectorShape(ulong Id, IReadOnlyList<ScenePoint> Points, string Name = "", bool Closed = false,
    bool Interactive = false, SceneColor Fill = default, SceneColor Stroke = default, float StrokeWidth = 1,
    SceneTransform? Transform = null, SceneClip? Clip = null);

internal static unsafe class Features
{
    internal static SelectionInfo Selection(Element e)
    { var v = Get(e, 41); return new(v.A != 0 ? new ItemKey(v.First, v.Second) : null, checked((uint)v.B)); }
    internal static bool Contains(Element e, ItemKey key)
    { e.Window.Guard(); uint selected; e.Window.Check(Native.CollectionContains(e.Handle, key.Id, key.Version, &selected)); return selected != 0; }
    internal const uint Version = 0x10001;
    internal static Native.FeatureValue Value() => new() { Size = (uint)sizeof(Native.FeatureValue), Version = Version };
    internal static void Set(Element e, uint property, double a = 0, double b = 0, double c = 0, double d = 0,
        ulong first = 0, ulong second = 0, string? text = null)
    {
        e.Window.Guard(); using var pins = new Window.Pins();
        var v = Value(); v.A = a; v.B = b; v.C = c; v.D = d; v.First = first; v.Second = second;
        if (text is not null) v.Text = pins.Text(text);
        e.Window.Check(Native.FeatureSet(e.Handle, property, &v));
    }
    internal static Native.FeatureValue Get(Element e, uint property)
    {
        e.Window.Guard(); var v = Value(); e.Window.Check(Native.FeatureGet(e.Handle, property, &v)); return v;
    }
    internal static void Action(Element e, uint action, ulong first = 0, ulong second = 0)
    { e.Window.Guard(); e.Window.Check(Native.FeatureAction(e.Handle, action, first, second)); }
    internal static ulong Child(Element e, uint index)
    { e.Window.Guard(); ulong h; e.Window.Check(Native.FeatureChild(e.Handle, index, &h)); return h; }
    internal static void Choices(Element e, ReadOnlySpan<Choice> choices, ulong? selected)
    {
        e.Window.Guard(); if (choices.Length > 4096) throw new ArgumentOutOfRangeException(nameof(choices));
        using var pins = new Window.Pins(); var rows = new Native.Choice[choices.Length];
        for (int i = 0; i < rows.Length; ++i) rows[i] = new()
        { Size = (uint)sizeof(Native.Choice), Id = choices[i].Id, Version = choices[i].Version, Flags = choices[i].Enabled ? 0u : 1u, Text = pins.Text(choices[i].Text) };
        fixed (Native.Choice* p = rows) e.Window.Check(Native.Choices(e.Handle, p, (uint)rows.Length, selected ?? 0, selected.HasValue ? 1u : 0u));
    }
    internal static void Add(Element e, Element child, uint row = 0, uint column = 0, uint rows = 1, uint columns = 1)
    { e.Window.Guard(); child.BelongsTo(e.Window); e.Window.Check(Native.PanelAdd(e.Handle, child.Handle, row, column, rows, columns)); }
    internal static void Popup(Element e, Control anchor)
    { e.Window.Guard(); anchor.BelongsTo(e.Window); e.Window.Check(Native.PopupShow(e.Handle, anchor.Handle)); }
    internal static void Source(Element e, ImmutableSource source)
    { e.Window.Guard(); source.BelongsTo(e.Window); e.Window.Check(Native.SourceAttach(e.Handle, source.Handle)); }
    internal static void Commands(Element e, ReadOnlySpan<Command> commands, bool contextMenu = false)
    {
        e.Window.Guard(); if (commands.Length > 4096) throw new ArgumentOutOfRangeException(nameof(commands));
        using var pins = new Window.Pins(); var records = new Native.CommandRecord[commands.Length];
        for (int i = 0; i < records.Length; ++i)
        {
            var c = commands[i]; records[i] = new() { Size = (uint)sizeof(Native.CommandRecord), Id = c.Id, Parent = c.Parent,
                Kind = (uint)c.Kind, Label = pins.Text(c.Label), Hint = pins.Text(c.ShortcutHint), PinLabel = pins.Text(c.PinLabel),
                Flags = (c.Enabled ? 0u : 1u) | (c.Checked == true ? 2u : 0u) | (c.Checked.HasValue ? 4u : 0u) };
        }
        fixed (Native.CommandRecord* p = records)
            e.Window.Check(contextMenu ? Native.ContextMenuItems(e.Handle, p, (uint)records.Length) : Native.CommandsSet(e.Handle, p, (uint)records.Length));
    }
    internal static void InvokeCommand(Element e, ulong id, bool pin)
    { e.Window.Guard(); e.Window.Check(Native.CommandInvoke(e.Handle, id, pin ? 1u : 0u)); }
    internal static void BindCommand(Element e, ulong id, uint key, KeyModifiers modifiers)
    { e.Window.Guard(); e.Window.Check(Native.CommandBind(e.Handle, id, key, (uint)modifiers)); }
}
public sealed unsafe partial class Window
{
    public static bool WebContentEnabled => (Native.Capabilities() & 1) != 0;
    internal ulong FeatureCreate(uint kind, string name, Element? content = null, Element? second = null, uint mode = 0)
    {
        Guard(); content?.BelongsTo(this); second?.BelongsTo(this);
        uint version = Native.FeatureVersion();
        if ((version >> 16) != (Features.Version >> 16) || version < Features.Version)
            throw new XuiException(5, "This runtime does not support the feature ABI.");
        using var pins = new Pins();
        var o = new Native.FeatureOptions { Size = (uint)sizeof(Native.FeatureOptions), Version = Features.Version,
            Name = pins.Text(name), Content = content?.Handle ?? 0, Second = second?.Handle ?? 0, Mode = mode };
        ulong result; Check(Native.FeatureCreate(Handle, kind, &o, &result)); return result;
    }
    internal void ForeignEnter() { Guard(); ++callbacks; }
    internal void ForeignExit() { --callbacks; }
    internal void ForeignError(Exception error) { callbackError = error; }
    public void ShowShellCommands(Control anchor, ReadOnlySpan<string> paths)
    {
        Guard(); anchor.BelongsTo(this);
        if (paths.Length > 256) throw new ArgumentOutOfRangeException(nameof(paths));
        using var pins = new Pins(); var values = new Native.Text[paths.Length];
        for (int i = 0; i < paths.Length; ++i) values[i] = pins.Text(paths[i]);
        fixed (Native.Text* p = values) Check(Native.ShellShow(anchor.Handle, p, (uint)values.Length));
    }
}
public static class ControlFeatures
{
    public static T Help<T>(this T control, string text) where T : Control
    { Features.Set(control, 7, text: text); return control; }
    public static T TooltipDelay<T>(this T control, uint milliseconds) where T : Control
    { Features.Set(control, 8, first: milliseconds); return control; }
    public static T Visible<T>(this T control, bool visible) where T : Control
    { Features.Set(control, 37, first: visible ? 1u : 0u); return control; }
    public static Button Behavior(this Button button, ButtonBehavior behavior)
    { Features.Set(button, 9, first: (uint)behavior); return button; }
    public static bool IsChecked(this Button button) => Features.Get(button, 10).First != 0;
    public static Button RepeatTiming(this Button button, uint delay, uint interval)
    { Features.Set(button, 11, first: delay, second: interval); return button; }
}
public sealed partial class RangeInput
{
    public RangeInput ChangeValue(double value) { Features.Action(this, 2, BitConverter.DoubleToUInt64Bits(value)); return this; }
    public void OnChange(Action<double> callback)
    { ArgumentNullException.ThrowIfNull(callback); Event += e => { if (e.Kind == EventKind.Change) callback(BitConverter.UInt64BitsToDouble(e.Value)); }; }
}
public sealed partial class NumericInput
{
    public void OnChange(Action<double> callback)
    { ArgumentNullException.ThrowIfNull(callback); Event += e => { if (e.Kind == EventKind.Change) callback(BitConverter.UInt64BitsToDouble(e.Value)); }; }
    public NumericInput ChangeValue(double value) { Features.Action(this, 2, BitConverter.DoubleToUInt64Bits(value)); return this; }
    public void Step(bool increase) => Features.Action(this, 3, increase ? 1u : 0u);
}
public sealed partial class RadioGroup
{
    public RadioGroup SetItems(ReadOnlySpan<Choice> items, ulong? selected = null) { Features.Choices(this, items, selected); return this; }
    public RadioGroup Select(ulong id) { Features.Action(this, 1, id); return this; }
}
public sealed partial class ComboBox
{
    public ComboBox SetItems(ReadOnlySpan<Choice> items, ulong? selected = null) { Features.Choices(this, items, selected); return this; }
    public ComboBox Select(ulong id) { Features.Action(this, 1, id); return this; }
}
public sealed partial class TabStrip
{
    public TabStrip SetTabs(ReadOnlySpan<Choice> items, ulong? selected = null) { Features.Choices(this, items, selected); return this; }
    public TabStrip Select(ulong id) { Features.Action(this, 1, id); return this; }
}
public sealed partial class Breadcrumb { public Breadcrumb SetSegments(ReadOnlySpan<Choice> segments) { Features.Choices(this, segments, null); return this; } }
public sealed partial class SplitButton
{
    private Button? primary, secondary;
    public Button Primary => primary ??= new(Window, Features.Child(this, 0));
    public Button Secondary => secondary ??= new(Window, Features.Child(this, 1));
}
public sealed partial class Popup { public void Show(Control anchor) => Features.Popup(this, anchor); }
public sealed partial class ContentDialog
{
    private Button? primary, cancel;
    public ContentDialog SetValidationMessage(string message)
    {
        Window.Guard(); using var pins = new Window.Pins();
        Window.Check(Native.DialogValidation(Handle, pins.Text(message)));
        return this;
    }
    public void Show(Control anchor) => Features.Popup(this, anchor);
    public Button Primary => primary ??= new(Window, Features.Child(this, 0));
    public Button CancelButton => cancel ??= new(Window, Features.Child(this, 1));
    public void OnResult(Action<bool> result) => Window.SetSubscription(Handle, e => result(e.Value == 0));
}
public sealed partial class LocationPicker
{
    private TextInput? editor;
    private NavigationPane? navigation;
    public void Show(Control anchor) => Features.Popup(this, anchor);
    public TextInput Editor => editor ??= new(Window, Features.Child(this, 0));
    public NavigationPane Navigation => navigation ??= new(Window, Features.Child(this, 1));
}
public sealed partial class ViewPicker
{
    private RadioGroup? choices;
    private RangeInput? size;
    public void Show(Control anchor) => Features.Popup(this, anchor);
    public RadioGroup Choices => choices ??= new(Window, Features.Child(this, 0));
    public RangeInput Size => size ??= new(Window, Features.Child(this, 1));
}
public sealed partial class CommandSurface
{
    public void Show(Control anchor) => Features.Popup(this, anchor);
    public CommandSurface SetCommands(ReadOnlySpan<Command> commands) { Features.Commands(this, commands); return this; }
    public void OnCommand(Action<ulong, bool> action) => Window.SetSubscription(Handle, e => action(e.Value, (uint)e.Kind == 9));
    public void Invoke(ulong id, bool pin = false) => Features.InvokeCommand(this, id, pin);
    public CommandSurface Bind(ulong id, uint virtualKey, KeyModifiers modifiers) { Features.BindCommand(this, id, virtualKey, modifiers); return this; }
}
public sealed partial class CommandBar
{
    public CommandBar SetCommands(ReadOnlySpan<Command> commands) { Features.Commands(this, commands); return this; }
    public void Invoke(ulong id, bool pin = false) => Features.InvokeCommand(this, id, pin);
    public CommandBar Bind(ulong id, uint virtualKey, KeyModifiers modifiers) { Features.BindCommand(this, id, virtualKey, modifiers); return this; }
}
public sealed partial class MultilineText
{
    public new string Text { get => base.Text; set => Document = value; }
    public void Command(TextCommand command) => Features.Action(this, 4, (uint)command);
}
public sealed unsafe partial class RichText
{
    public new string Text { get => base.Text; set => Document = value; }
    public void Command(TextCommand command) => Features.Action(this, 4, (uint)command);
    public RichText SetRuns(ReadOnlySpan<TextRun> runs)
    {
        Window.Guard(); if (runs.Length > 4096) throw new ArgumentOutOfRangeException(nameof(runs));
        using var pins = new Window.Pins(); var values = new Native.TextRun[runs.Length];
        for (int i = 0; i < runs.Length; ++i) values[i] = new() { Size = (uint)sizeof(Native.TextRun),
            Text = pins.Text(runs[i].Text), Link = pins.Text(runs[i].Link),
            Flags = (runs[i].Bold ? 1u : 0u) | (runs[i].Italic ? 2u : 0u) | (runs[i].Underline ? 4u : 0u) };
        fixed (Native.TextRun* p = values) Window.Check(Native.RichRuns(Handle, p, (uint)values.Length));
        return this;
    }
}
public delegate void PasswordReceiver(ReadOnlySpan<byte> utf8);
public sealed unsafe partial class PasswordInput
{
    public ulong Length => Features.Get(this, 16).First;
    public void OnChange(Action callback)
    { ArgumentNullException.ThrowIfNull(callback); Window.SetSubscription(Handle, _ => callback()); }
    public PasswordInput SetPassword(ReadOnlySpan<char> password)
    {
        Window.Guard();
        if (password.Length > 4096 || password.Contains('\0')) throw new ArgumentException("Invalid password length or NUL.", nameof(password));
        var bytes = new byte[Window.Encoding.GetByteCount(password)];
        Window.Encoding.GetBytes(password, bytes);
        try
        {
            fixed (byte* p = bytes) { var v = Features.Value(); v.Text = Window.Span(p, bytes); Window.Check(Native.FeatureSet(Handle, 16, &v)); }
        }
        finally { CryptographicOperations.ZeroMemory(bytes); }
        return this;
    }
    private sealed record SecretCall(Window Window, PasswordReceiver Receiver);
    public void WithPassword(PasswordReceiver receiver)
    {
        ArgumentNullException.ThrowIfNull(receiver); Window.Guard();
        var root = GCHandle.Alloc(new SecretCall(Window, receiver));
        try { Window.Check(Native.PasswordRead(Handle, &Receive, GCHandle.ToIntPtr(root))); }
        finally { root.Free(); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Receive(nint context, byte* bytes, uint length)
    {
        SecretCall? call = null;
        try
        {
            call = (SecretCall)GCHandle.FromIntPtr(context).Target!;
            call.Window.ForeignEnter();
            try { call.Receiver(new ReadOnlySpan<byte>(bytes, checked((int)length))); }
            finally { call.Window.ForeignExit(); }
            return 0;
        }
        catch (Exception e) { call?.Window.ForeignError(e); return 8; }
    }
}
public sealed partial class DateTimePicker
{
    public DateTimePicker SetValue(DateTime value) { Value = value; return this; }
    public DateTime Value
    {
        get { var v = Features.Get(this, 18); return new((int)(v.First / 10000), (int)(v.First / 100 % 100), (int)(v.First % 100),
            (int)(v.Second / 10000), (int)(v.Second / 100 % 100), (int)(v.Second % 100), DateTimeKind.Unspecified); }
        set
        {
            if (value.Kind != DateTimeKind.Unspecified) throw new ArgumentException("Use local Gregorian fields without a time zone.");
            Features.Set(this, 18, first: (ulong)(value.Year * 10000 + value.Month * 100 + value.Day),
                second: (ulong)(value.Hour * 10000 + value.Minute * 100 + value.Second));
        }
    }
}
public sealed partial class InlineStatus
{ public InlineStatus SetMessage(string message, StatusSeverity severity = StatusSeverity.Information) { Features.Set(this, 20, text: message, first: (uint)severity); return this; } }
public sealed partial class MediaPlayback
{
    public HostState State => (HostState)Features.Get(this, 38).First;
    public MediaPlayback LoadLocal(string path) { Features.Set(this, 23, text: path); return this; }
    public MediaPlayback Seek(double seconds) { Features.Set(this, 40, a: seconds); return this; }
}
public sealed unsafe partial class WebContent
{
    public HostState State => (HostState)Features.Get(this, 38).First;
    public WebContent SetHtml(string html) { Features.Set(this, 25, text: html); return this; }
    public WebContent SetProfileRoot(string path) { Features.Set(this, 26, text: path); return this; }
    public WebContent SetAllowedOrigins(ReadOnlySpan<string> origins)
    {
        Window.Guard(); if (origins.Length > 16) throw new ArgumentOutOfRangeException(nameof(origins));
        using var pins = new Window.Pins(); var values = new Native.Text[origins.Length];
        for (int i = 0; i < origins.Length; ++i) values[i] = pins.Text(origins[i]);
        fixed (Native.Text* p = values) Window.Check(Native.WebOrigins(Handle, p, (uint)values.Length));
        return this;
    }
    public void Navigate(string uri)
    {
        Window.Guard(); using var pins = new Window.Pins(); Window.Check(Native.WebNavigate(Handle, pins.Text(uri)));
    }
    public WebEvaluation Evaluate(string script)
    {
        Window.Guard(); using var pins = new Window.Pins(); ulong token;
        Window.Check(Native.WebEvaluate(Handle, pins.Text(script), &token)); return new(Window, token);
    }
}
public readonly record struct WebResult(string Text, bool IsError);
public sealed unsafe class WebEvaluation : IDisposable
{
    private readonly Window window;
    private ulong handle;
    internal WebEvaluation(Window owner, ulong token) { window = owner; handle = token; }
    public WebResult? TryGetResult()
    {
        window.Guard(); ObjectDisposedException.ThrowIf(handle == 0, this);
        uint count, failed;
        int status = Native.WebResult(handle, null, 0, &count, &failed);
        if (status == 7) return null;
        if (status != 6) window.Check(status);
        var bytes = new byte[count];
        fixed (byte* p = bytes) window.Check(Native.WebResult(handle, p, count, &count, &failed));
        return new(Window.Encoding.GetString(bytes), failed != 0);
    }
    public void Dispose() { if (handle == 0) return; if (window.Handle != 0) { window.Guard(); window.Check(Native.RequestCancel(handle)); } handle = 0; }
}
public sealed partial class HistoryChart { public void Append(double value) => Features.Set(this,2, a: value); }
public sealed partial class Wrap { public Wrap Add(Element child) { Features.Add(this, child); return this; } }
public sealed partial class PageView { public PageView Add(Element child) { Features.Add(this, child); return this; } }
public sealed unsafe partial class Grid
{
    public Grid Add(Element child, uint row = 0, uint column = 0, uint rowSpan = 1, uint columnSpan = 1) { Features.Add(this, child, row, column, rowSpan, columnSpan); return this; }
    public Grid SetTracks(ReadOnlySpan<GridTrack> rows, ReadOnlySpan<GridTrack> columns)
    {
        Window.Guard();
        static Native.GridTrack[] Convert(ReadOnlySpan<GridTrack> tracks)
        {
            if (tracks.Length > 256) throw new ArgumentOutOfRangeException(nameof(tracks));
            var result = new Native.GridTrack[tracks.Length];
            for (int i = 0; i < result.Length; ++i) result[i] = new() { Size = (uint)sizeof(Native.GridTrack),
                Sizing = (uint)tracks[i].Sizing, Value = tracks[i].Value, Minimum = tracks[i].Minimum, Maximum = tracks[i].Maximum };
            return result;
        }
        var r = Convert(rows); var c = Convert(columns);
        fixed (Native.GridTrack* rp = r, cp = c) Window.Check(Native.GridTracks(Handle, rp, (uint)r.Length, cp, (uint)c.Length));
        return this;
    }
}
public sealed unsafe partial class DataGrid
{
    public SelectionInfo Selection => Features.Selection(this);
    public bool Contains(ItemKey key) => Features.Contains(this, key);
    public DataGrid SetFilter(uint sourceColumn, string query)
    { Window.Guard(); using var pins = new Window.Pins(); Window.Check(Native.GridFilter(Handle, sourceColumn, pins.Text(query))); return this; }
    public DataGrid SetSort(uint sourceColumn, bool descending)
    { Window.Guard(); Window.Check(Native.GridSort(Handle, sourceColumn, descending ? 1u : 0u)); return this; }
    public DataGrid SetChecked(ItemKey key, bool value)
    { Window.Guard(); Window.Check(Native.GridCheck(Handle, key.Id, key.Version, value ? 1u : 0u)); return this; }
    public DataGrid SetSource(ImmutableSource source) { Features.Source(this, source); return this; }
    public DataGrid Select(ItemKey key) { Features.Action(this, 1, key.Id, key.Version); return this; }
    public DataGrid SetColumns(ReadOnlySpan<GridColumn> columns)
    {
        Window.Guard(); if (columns.Length > 256) throw new ArgumentOutOfRangeException(nameof(columns));
        using var pins = new Window.Pins(); var values = new Native.Column[columns.Length];
        for (int i = 0; i < values.Length; ++i) values[i] = new() { Size = (uint)sizeof(Native.Column), Name = pins.Text(columns[i].Name), Width = columns[i].Width,
            Flags = (columns[i].Numeric ? 1u : 0u) | (columns[i].Filterable ? 2u : 0u) | (columns[i].Checkable ? 4u : 0u) };
        fixed (Native.Column* p = values) Window.Check(Native.GridColumns(Handle, p, (uint)values.Length));
        return this;
    }
    public DataGrid SetColumnWidth(uint column, float width) { Window.Guard(); Window.Check(Native.GridColumnWidth(Handle, column, width)); return this; }
    public DataGrid SetColumnOrder(ReadOnlySpan<uint> order)
    { Window.Guard(); if (order.Length > 256) throw new ArgumentOutOfRangeException(nameof(order)); fixed (uint* p = order) Window.Check(Native.GridColumnOrder(Handle, p, (uint)order.Length)); return this; }
}
public sealed partial class ItemsView
{
    public SelectionInfo Selection => Features.Selection(this);
    public bool Contains(ItemKey key) => Features.Contains(this, key);
    public ItemsView SetSource(ImmutableSource source) { Features.Source(this, source); return this; }
    public ItemsView Select(ItemKey key) { Features.Action(this, 1, key.Id, key.Version); return this; }
    public ItemsView ItemSize(double width, double height) { Features.Set(this, 27, a: width, b: height); return this; }
}
public sealed partial class NavigationPane
{
    private ItemsView? items;
    public NavigationPane SetSource(ImmutableSource source) { Features.Source(this, source); return this; }
    public ItemsView Items => items ??= new(Window, Features.Child(this, 0));
}
public sealed unsafe partial class TreeView
{
    public SelectionInfo Selection => Features.Selection(this);
    public bool Contains(ItemKey key) => Features.Contains(this, key);
    public TreeView SetSource(ImmutableSource source) { Features.Source(this, source); return this; }
    public TreeView Expand(ItemKey key, bool expanded = true)
    { Window.Guard(); Window.Check(Native.TreeExpand(Handle, key.Id, key.Version, expanded ? 1u : 0u)); return this; }
    public void OnRequest(Action<TreeRequest> callback)
    { ArgumentNullException.ThrowIfNull(callback); Event += e => { if ((uint)e.Kind == 11) callback(new(this, e.Value)); }; }
}
public sealed unsafe class TreeRequest : IDisposable
{
    private readonly TreeView tree;
    private ulong handle;
    internal TreeRequest(TreeView owner, ulong token) { tree = owner; handle = token; }
    public ItemKey Node
    {
        get { tree.Window.Guard(); var v = Features.Value(); tree.Window.Check(Native.RequestInfo(handle, &v)); return new(v.First, v.Second); }
    }
    public void Complete(ImmutableSource children, string error = "")
    {
        tree.Window.Guard(); children.BelongsTo(tree.Window);
        ObjectDisposedException.ThrowIf(handle == 0, this);
        using var pins = new Window.Pins();
        int status = Native.TreeComplete(tree.Handle, handle, children.Handle, pins.Text(error));
        if (status == 0 || status == 11) handle = 0;
        tree.Window.Check(status);
    }
    public void Dispose() { if (handle == 0) return; if (tree.Window.Handle != 0) { tree.Window.Guard(); tree.Window.Check(Native.RequestCancel(handle)); } handle = 0; }
}
/// <summary>Immutable row content. Visible ImagePath values use asynchronous image resources.</summary>
/// <remarks>DataGrid uses visuals from source column zero. Folder selects Shell decoding and supplies the fallback icon.</remarks>
public readonly record struct ItemContent(string Primary, string Secondary = "", bool Enabled = true, double? Progress = null,
    bool? Checked = null, ButtonIcon Icon = ButtonIcon.None, string ImagePath = "");
public interface IReadOnlyImmutableSource
{
    ulong Count { get; }
    ItemKey Key(ulong index);
    ulong? Find(ItemKey key);
    ItemContent Item(ulong index, ulong column = 0);
    bool HasChildren(ItemKey key) => false;
}
public sealed class ImmutableSource : Element, IDisposable
{
    private bool disposed;
    internal ImmutableSource(Window window, ulong handle) : base(window, handle) { }
    internal new void BelongsTo(Window window)
    { ObjectDisposedException.ThrowIf(disposed, this); base.BelongsTo(window); }
    public void Dispose()
    {
        if (disposed) return;
        if (Window.Handle != 0) { Window.Guard(); Window.Check(Native.SourceRelease(Handle)); }
        disposed = true;
    }
}
public sealed unsafe partial class Window
{
    private sealed class SourcePin(IReadOnlyImmutableSource source, Window window)
    {
        internal readonly IReadOnlyImmutableSource Source = source;
        internal readonly WeakReference<Window> Owner = new(window);
        internal int References = 1;
    }
    public ImmutableSource ImmutableSource(IReadOnlyImmutableSource source)
    {
        Guard(); ArgumentNullException.ThrowIfNull(source);
        ulong count = source.Count;
        var pin = new SourcePin(source, this); var root = GCHandle.Alloc(pin); nint context = GCHandle.ToIntPtr(root);
        try
        {
            var options = new Native.SourceOptions { Size = (uint)sizeof(Native.SourceOptions), Version = Features.Version,
                Count = count, Context = context, Query = &QuerySource, Retain = &RetainSource, Release = &ReleaseSource };
            ulong handle; Check(Native.SourceCreateVisual(Handle, &options, &QueryVisual, &handle)); return new(this, handle);
        }
        finally { ReleaseSourceCore(context); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int QueryVisual(nint context, ulong index, ulong column, uint* icon, byte* output, uint capacity, uint* required)
    {
        Window? window = null;
        try
        {
            var pin = (SourcePin)GCHandle.FromIntPtr(context).Target!;
            if (!pin.Owner.TryGetTarget(out window)) return 11;
            window.ForeignEnter();
            try
            {
                var item = pin.Source.Item(index, column);
                if ((uint)item.Icon > (uint)ButtonIcon.Drive) throw new ArgumentException("Invalid item icon.");
                if (item.ImagePath.Length > 32767) throw new ArgumentException("Image path exceeds 32767 UTF-16 units.");
                var bytes = Utf8(item.ImagePath);
                *icon = (uint)item.Icon; *required = (uint)bytes.Length;
                if (capacity < bytes.Length) return 6;
                bytes.CopyTo(new Span<byte>(output, bytes.Length));
                return 0;
            }
            finally { window.ForeignExit(); }
        }
        catch (Exception error) { window?.ForeignError(error); return 8; }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void RetainSource(nint context)
    { try { ++((SourcePin)GCHandle.FromIntPtr(context).Target!).References; } catch { } }
    private static void ReleaseSourceCore(nint context)
    {
        var root = GCHandle.FromIntPtr(context);
        if (--((SourcePin)root.Target!).References == 0) root.Free();
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ReleaseSource(nint context) { try { ReleaseSourceCore(context); } catch { } }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int QuerySource(nint context, uint operation, ulong first, ulong second, Native.SourceRow* row)
    {
        Window? window = null;
        try
        {
            var pin = (SourcePin)GCHandle.FromIntPtr(context).Target!;
            if (!pin.Owner.TryGetTarget(out window)) return 11;
            window.ForeignEnter();
            try
            {
                switch (operation)
                {
                    case 0: var key = pin.Source.Key(first); row->Id = key.Id; row->Version = key.Version; break;
                    case 1:
                        var item = pin.Source.Item(first, second);
                        row->Flags = (item.Enabled ? 0u : 1u) | (item.Progress.HasValue ? 2u : 0u) | (item.Checked == true ? 4u : 0u) | (item.Checked.HasValue ? 8u : 0u);
                        row->Progress = item.Progress ?? 0;
                        static uint Copy(string text, byte* output)
                        {
                            var bytes = Utf8(text);
                            if (bytes.Length > 1024) throw new ArgumentException("A source field exceeds 1024 UTF-8 bytes.");
                            bytes.CopyTo(new Span<byte>(output, 1024)); return (uint)bytes.Length;
                        }
                        row->PrimaryLength = Copy(item.Primary, row->Primary); row->SecondaryLength = Copy(item.Secondary, row->Secondary); break;
                    case 2: row->Index = pin.Source.Find(new(first, second)) ?? ulong.MaxValue; break;
                    case 3: row->Index = pin.Source.HasChildren(new(first, second)) ? 1u : 0u; break;
                    default: return 1;
                }
                return 0;
            }
            finally { window.ForeignExit(); }
        }
        catch (Exception e) { window?.ForeignError(e); return 8; }
    }
}
public sealed unsafe partial class MapView
{
    public MapView SetView(GeoPoint center, double zoom) { Features.Set(this, 22, a: center.Latitude, b: center.Longitude, c: zoom); return this; }
    public (GeoPoint Center, double Zoom) View { get { var v = Features.Get(this, 22); return (new(v.A, v.B), v.C); } }
    public MapView Pan(double x, double y) { Features.Set(this, 39, a: x, b: y); return this; }
    internal Native.MapMarker[] Markers(ReadOnlySpan<MapMarker> markers, Window.Pins pins)
    {
        Window.Guard(); if (markers.Length > 256) throw new ArgumentOutOfRangeException(nameof(markers));
        var values = new Native.MapMarker[markers.Length];
        for (int i = 0; i < values.Length; ++i) values[i] = new() { Size = (uint)sizeof(Native.MapMarker), Id = markers[i].Id,
            Latitude = markers[i].Location.Latitude, Longitude = markers[i].Location.Longitude, Name = pins.Text(markers[i].Name) };
        return values;
    }
    public MapView SetMarkers(ReadOnlySpan<MapMarker> markers)
    {
        using var pins = new Window.Pins(); var values = Markers(markers, pins);
        fixed (Native.MapMarker* p = values) Window.Check(Native.MapMarkers(Handle, p, (uint)values.Length));
        return this;
    }
    public MapRequest RequestOverlay()
    { Window.Guard(); ulong token; Window.Check(Native.MapRequest(Handle, &token)); return new(this, token); }
}
public sealed unsafe class MapRequest : IDisposable
{
    private readonly MapView map;
    private ulong handle;
    internal MapRequest(MapView owner, ulong token) { map = owner; handle = token; }
    public void Complete(ReadOnlySpan<MapMarker> markers)
    {
        ObjectDisposedException.ThrowIf(handle == 0, this); using var pins = new Window.Pins(); var values = map.Markers(markers, pins);
        int status; fixed (Native.MapMarker* p = values) status = Native.MapComplete(map.Handle, handle, p, (uint)values.Length);
        if (status == 0 || status == 11) handle = 0;
        map.Window.Check(status);
    }
    public void Dispose() { if (handle == 0) return; if (map.Window.Handle != 0) { map.Window.Guard(); map.Window.Check(Native.RequestCancel(handle)); } handle = 0; }
}
public sealed unsafe partial class VectorCanvas
{
    public VectorCanvas SetScene(IReadOnlyList<VectorShape> shapes)
    {
        Window.Guard(); ArgumentNullException.ThrowIfNull(shapes);
        if (shapes.Count > 4096) throw new ArgumentOutOfRangeException(nameof(shapes));
        using var pins = new Window.Pins(); var values = new Native.Shape[shapes.Count]; var pointPins = new List<GCHandle>();
        try
        {
            int pointCount = 0;
            for (int i = 0; i < values.Length; ++i)
            {
                var s = shapes[i]; pointCount = checked(pointCount + s.Points.Count);
                if (pointCount > 65536) throw new ArgumentOutOfRangeException(nameof(shapes));
                var points = new Native.ScenePoint[s.Points.Count];
                for (int j = 0; j < points.Length; ++j) points[j] = new() { X = s.Points[j].X, Y = s.Points[j].Y };
                var pin = GCHandle.Alloc(points, GCHandleType.Pinned); pointPins.Add(pin);
                var t = s.Transform ?? new SceneTransform(1, 0, 0, 1, 0, 0); var c = s.Clip ?? default;
                values[i] = new() { Size = (uint)sizeof(Native.Shape), Id = s.Id, Flags = (s.Closed ? 1u : 0u) | (s.Interactive ? 2u : 0u) | (s.Clip.HasValue ? 4u : 0u),
                    Points = (Native.ScenePoint*)pin.AddrOfPinnedObject(), Count = (uint)points.Length, Name = pins.Text(s.Name),
                    FillRed = s.Fill.Red, FillGreen = s.Fill.Green, FillBlue = s.Fill.Blue, FillAlpha = s.Fill.Alpha,
                    StrokeRed = s.Stroke.Red, StrokeGreen = s.Stroke.Green, StrokeBlue = s.Stroke.Blue, StrokeAlpha = s.Stroke.Alpha, StrokeWidth = s.StrokeWidth,
                    M11 = t.M11, M12 = t.M12, M21 = t.M21, M22 = t.M22, Dx = t.Dx, Dy = t.Dy,
                    ClipX = c.X, ClipY = c.Y, ClipWidth = c.Width, ClipHeight = c.Height };
            }
            fixed (Native.Shape* p = values) Window.Check(Native.CanvasScene(Handle, p, (uint)values.Length));
        }
        finally { foreach (var pin in pointPins) pin.Free(); }
        return this;
    }
}

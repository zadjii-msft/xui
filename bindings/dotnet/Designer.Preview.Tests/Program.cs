using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using Xui;
using Xui.Designer;
using Xui.Generator;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

internal static class Program
{
    private static int assertions;
    private static Exception? failure;
    private static readonly string Title = "XUI embedded preview regression " + Guid.NewGuid().ToString("N");
    private const string MappedSource = """
        component Good {
            view {
                VStack(spacing: 2, ref: Outer) {
                    Text("Retained preview", ref: Label);
                    HStack(ref: Row) {
                        Button("Mapped action", ref: Action);
                        Toggle("Mapped toggle", ref: Toggle);
                    }
                    Grid("Mapped grid", ref: Grid) { Text("Mapped cell", ref: Cell); }
                    TextInput("Mapped input", ref: Input);
                    Content(__xuiWindow.Label("Mapped external"), ref: External);
                }
            }
        }
        """;

    [STAThread]
    private static int Main()
    {
        try
        {
            var good = PreviewCompiler.Compile(MappedSource);
            var bad = PreviewCompiler.Compile("""component Bad { state int Count = int.Parse("bad"); view { Text($"{Count}"); } }""");
            var recovered = PreviewCompiler.Compile("""component Recovered { view { Text("Recovered preview"); } }""");
            Assert(good.Success && bad.Success && recovered.Success, "Fixture compilation failed.");
            Assert(!PreviewCompiler.Compile("component Invalid { view { Invalid(); } }").Success, "Invalid source compiled.");
            using var window = new Window(Title, 850, 650);
            window.SetShowActivated(false);
            var editor = window.MultilineText("Stable source editor");
            editor.Text = "editor content and undo remain";
            var scopeHost = window.CreateContentHost();
            PreviewHost? preview = null;
            var statuses = new List<(long Version, bool Success)>();
            WeakReference? oldContext = null;
            IReadOnlyList<PreviewNodeSnapshot>? oldNodes = null;
            var beforeForeground = GetForegroundWindow();
            preview = new PreviewHost(window, (version, message, success) =>
            {
                try
                {
                    statuses.Add((version, success));
                    Assert(GetForegroundWindow() == beforeForeground, "Preview changed foreground activation.");
                    if (version == 1 && success)
                    {
                        Assert(preview!.AppliedVersion == 1, "First preview was not committed.");
                        oldNodes = NodeMapTests(window, preview, 1, MappedSource);
                        oldContext = CurrentPreviewContext();
                        Assert(preview.View.GetBounds().Width > 100 && preview.View.GetBounds().Height > 50, "Preview has no usable native bounds.");
                        Assert(Texts(FindWindowW(null, Title)).Contains("Retained preview"), "Preview has no native label.");
                        preview.Supersede(2);
                        Assert(!preview.TryReadNodeMap(2, out var pendingNodes) && pendingNodes.Count == 0,
                            "Pending source received the previous source's node IDs.");
                        preview.Publish(2, bad.Assembly!, Theme.Dark);
                    }
                    else if (version == 2 && !success)
                    {
                        Assert(message.StartsWith("Preview construction failed:", StringComparison.Ordinal), message);
                        Assert(preview!.AppliedVersion == 1, "A constructor error replaced the previous preview.");
                        Assert(preview.TryReadNodeMap(1, out var preservedNodes) && preservedNodes.SequenceEqual(oldNodes!),
                            "A constructor error changed the last successful node map.");
                        Assert(!preview.TryReadNodeMap(2, out _), "A failed source received a node map.");
                        Assert(Texts(FindWindowW(null, Title)).Contains("Retained preview"), "A constructor error removed old native content.");
                        preview.Supersede(3);
                        preview.Publish(3, good.Assembly!, Theme.Dark);
                        preview.Supersede(4);
                        preview.Publish(4, recovered.Assembly!, Theme.Dark);
                        preview.Publish(3, bad.Assembly!, Theme.Dark);
                    }
                    else if (version == 4 && success)
                    {
                        Assert(preview!.AppliedVersion == 4, "Latest preview was not applied.");
                        Assert(!preview.TryReadNodeMap(1, out _) && !preview.TryReadNode(1, 0, out _),
                            "A replaced revision remained readable as the current preview.");
                        Assert(preview.TryReadNodeMap(4, out var replacementNodes) && replacementNodes.Count == 1 &&
                            replacementNodes[0] is { NodeId: 0, ElementType: "Label", ControlId: not null },
                            "A non-stack replacement root has an incorrect map.");
                        foreach (var node in oldNodes!.Where(n => n.ControlId.HasValue))
                            Assert(TextCopy(node.ControlId!.Value, null, 0, out _) == 2, "A snapshot retained a retired control handle.");
                        Assert(statuses.All(x => x.Version != 3), "An obsolete source version was reported.");
                        Assert(Texts(FindWindowW(null, Title)).Contains("Recovered preview"), "Recovery has no native content.");
                        preview.Dispose();
                        Assert(preview.AppliedVersion is null && !preview.TryReadNodeMap(4, out _),
                            "Disposed preview metadata remained available.");
                        window.Post(() =>
                        {
                            try
                            {
                                GC.Collect();
                                GC.WaitForPendingFinalizers();
                                GC.Collect();
                                Assert(oldContext is { IsAlive: false }, "The retired preview assembly context remained rooted.");
                                Assert(oldNodes![0].Version == 1, "Retained value snapshots changed after replacement.");
                                GC.KeepAlive(oldNodes);
                            }
                            catch (Exception error) { failure = error; }
                            window.Close();
                        });
                    }
                    else throw new InvalidOperationException($"Unexpected preview status {version}: {message}");
                }
                catch (Exception error) { Console.Error.WriteLine(error); failure = error; window.Close(); }
            });
            using (preview)
            {
                Assert(preview.AppliedVersion is null && !preview.TryReadNodeMap(0, out _),
                    "An empty preview exposed a node map.");
                window.SetContent(window.Stack().Add(editor, 1).Add(scopeHost, 1).Add(preview.View, 1));
                window.Post(() =>
                {
                    try
                    {
                        ScopeTests(window, scopeHost, editor, () =>
                        {
                            preview.Supersede(1);
                            preview.Publish(1, good.Assembly!, Theme.Dark);
                        });
                    }
                    catch (Exception error) { Console.Error.WriteLine(error); failure = error; window.Close(); }
                });
                using var timeout = new CancellationTokenSource();
                var watchdog = Task.Run(async () =>
                {
                    try
                    {
                        await Task.Delay(TimeSpan.FromSeconds(60), timeout.Token);
                        window.Post(() => { failure = new TimeoutException("Embedded preview regression timed out."); window.Close(); });
                    }
                    catch (OperationCanceledException) when (timeout.IsCancellationRequested) { }
                });
                window.Run();
                timeout.Cancel();
                watchdog.GetAwaiter().GetResult();
                if (failure is not null) throw failure;
                Assert(statuses.SequenceEqual(new[] { (1L, true), (2L, false), (4L, true) }), "Missing preview lifecycle statuses.");
            }
            ClosePendingTests(good.Assembly!);
            Console.WriteLine($"Embedded preview assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static IReadOnlyList<PreviewNodeSnapshot> NodeMapTests(Window window, PreviewHost preview, long version, string source)
    {
        var handles = HandleCount(window);
        var hwnd = FindWindowW(null, Title);
        var nativeChildren = Children(hwnd);
        var document = XuiSourceParser.Parse(source);
        Assert(document.Success, "Node-map fixture did not parse.");
        var authored = Flatten(document.Root!).ToArray();
        Assert(preview.TryReadNodeMap(version, out var nodes) && nodes.Count == authored.Length,
            "Preview metadata count differs from the source parser.");
        Assert(nodes[0].Bounds == preview.View.GetBounds(), "Root bounds do not match the embedded host.");
        var expectedTypes = new Dictionary<string, string>
        {
            ["VStack"] = "Stack", ["HStack"] = "Stack", ["Text"] = "Label", ["Content"] = "Label",
            ["Button"] = "Button", ["Toggle"] = "Toggle", ["Grid"] = "Grid", ["TextInput"] = "TextInput"
        };
        foreach (var node in authored)
        {
            var actual = nodes[node.Id];
            Assert(actual.Version == version && actual.NodeId == node.Id && actual.ElementType == expectedTypes[node.Kind],
                $"Wrong runtime mapping for source node {node.Id} ({node.Kind}).");
            Assert(preview.TryReadNode(version, node.Id, out var single) && single == actual, "Single-node and full-map reads differ.");
            Assert(float.IsFinite(actual.Bounds.X) && float.IsFinite(actual.Bounds.Y) &&
                actual.Bounds.Width >= 0 && actual.Bounds.Height >= 0, "Node bounds are invalid.");
            if (node.Kind is "Text" or "Button" or "Toggle")
            {
                var literal = (LiteralExpressionSyntax)SyntaxFactory.ParseExpression(node.Arguments.Single(a => a.IsPositional).Value);
                Assert(actual.ControlId.HasValue && ReadText(actual.ControlId.Value) == literal.Token.ValueText,
                    "The source ID mapped to a different native control.");
                if (node.Kind == "Text" && literal.Token.ValueText == "Retained preview")
                {
                    var peer = nativeChildren.Single(child => WindowText(child) == literal.Token.ValueText);
                    Assert(GetWindowRect(peer, out var rect), "Native label bounds are unavailable.");
                    var origin = new Point { X = rect.Left, Y = rect.Top };
                    Assert(ScreenToClient(hwnd, ref origin), "Native coordinate conversion failed.");
                    float scale = GetDpiForWindow(hwnd) / 96f;
                    Assert(scale > 0 && Math.Abs(origin.X - actual.Bounds.X * scale) <= 1 &&
                        Math.Abs(origin.Y - actual.Bounds.Y * scale) <= 1 &&
                        Math.Abs(rect.Right - rect.Left - actual.Bounds.Width * scale) <= 1 &&
                        Math.Abs(rect.Bottom - rect.Top - actual.Bounds.Height * scale) <= 1,
                        "Snapshot bounds differ from the actual native label.");
                }
            }
            if (node.Kind is "VStack" or "HStack") Assert(actual.ControlId is null, "A layout node invented a control handle.");
            if (node.Kind == "Content") Assert(ReadText(actual.ControlId!.Value) == "Mapped external",
                "Content did not map to its existing authored element.");
        }
        foreach (int invalid in new[] { -1, nodes.Count, int.MaxValue })
            Throws<ArgumentOutOfRangeException>(() => preview.TryReadNode(version, invalid, out _), "An invalid source ID was accepted.");
        Assert(Task.Run(() =>
        {
            try { _ = preview.AppliedVersion; return false; }
            catch (XuiException error) { return error.Status == 4; }
        }).GetAwaiter().GetResult(), "AppliedVersion omitted the UI-thread guard.");
        Assert(Task.Run(() =>
        {
            try { preview.TryReadNodeMap(version, out _); return false; }
            catch (XuiException error) { return error.Status == 4; }
        }).GetAwaiter().GetResult(), "Node-map read omitted the UI-thread guard.");
        Assert(Task.Run(() =>
        {
            try { preview.TryReadNode(version, 0, out _); return false; }
            catch (XuiException error) { return error.Status == 4; }
        }).GetAwaiter().GetResult(), "Node read omitted the UI-thread guard.");
        Assert(nodes is IList<PreviewNodeSnapshot> { IsReadOnly: true }, "Node map is mutable.");
        for (int i = 0; i < 100; i++)
            Assert(preview.TryReadNodeMap(version, out var reread) && reread.SequenceEqual(nodes), "Repeated node-map reads changed identity.");
        Assert(HandleCount(window) == handles && Children(hwnd).SequenceEqual(nativeChildren), "Node-map reads allocated native handles or peers.");
        return nodes;
    }

    private static IEnumerable<XuiSourceNode> Flatten(XuiSourceNode node)
    {
        yield return node;
        foreach (var child in node.Children)
            foreach (var descendant in Flatten(child)) yield return descendant;
    }

    private static string ReadText(ulong control)
    {
        int status = TextCopy(control, null, 0, out var length);
        Assert(status is 0 or 6, "Native text length failed.");
        var bytes = new byte[length];
        Assert(TextCopy(control, bytes, length, out _) == 0, "Native text read failed.");
        return Encoding.UTF8.GetString(bytes);
    }

    private static void ScopeTests(Window window, ContentHost host, MultilineText editor, Action complete)
    {
        var hwnd = FindWindowW(null, Title);
        Assert(hwnd != 0, "Native test window is missing.");
        var editorWindows = Children(hwnd).Where(h => ClassName(h).Contains("RICHEDIT", StringComparison.OrdinalIgnoreCase)).ToArray();
        Assert(editorWindows.Length != 0, "The editor has no native RichEdit.");
        var nativeEditor = editorWindows[0];
        SendMessageW(nativeEditor, 0xB1, 3, 7);
        ReplaceSelection(nativeEditor, 0xC2, 1, "EDIT");
        var beforeText = editor.Text;
        Assert(SendMessageW(nativeEditor, 0xC6, 0, 0) != 0, "The editor undo fixture is empty.");
        SendMessageW(nativeEditor, 0xB1, 2, 8);
        var beforeSelection = SendMessageW(nativeEditor, 0xB0, 0, 0);
        var beforeFocus = GetFocus();
        var beforeForeground = GetForegroundWindow();
        Throws<XuiException>(() => window.Label("Outside scope"), "Ordinary creation during Run was accepted.");
        var identity = WindowHandle(window);
        Assert(Task.Run(() => ContentHandleCount(identity, out _)).GetAwaiter().GetResult() == 4, "The ABI accepted a foreign UI thread.");
        var baseline = HandleCount(window);
        uint activeCount = 0;
        Button? stale = null;
        int events = 0;
        for (int i = 0; i < 100; i++)
        {
            var candidate = host.BeginUpdate();
            var label = window.Label($"Iteration {i}");
            var button = window.Button("Invoke current").OnClick(() => events++);
            var input = window.TextInput("Preview native input");
            input.Text = $"value {i}";
            var root = window.Stack().Add(label).Add(button).Add(input);
            candidate.Commit(root);
            Throws<InvalidOperationException>(() => candidate.Commit(root), "A candidate committed twice.");
            Assert(host.GetBounds().Width > 100, "Content host has no layout.");
            button.Invoke();
            Assert(events == i + 1, "Current callback did not execute.");
            if (stale is not null) Throws<XuiException>(() => stale.Invoke(), "A retired handle remained usable.");
            stale = button;
            Assert(IsWindow(nativeEditor), "The editor HWND was replaced.");
            Assert(editor.Text == beforeText, "The editor text changed.");
            Assert(SendMessageW(nativeEditor, 0xB0, 0, 0) == beforeSelection, "The editor selection changed.");
            Assert(SendMessageW(nativeEditor, 0xC6, 0, 0) != 0, "The editor undo history was cleared.");
            Assert(GetFocus() == beforeFocus && GetForegroundWindow() == beforeForeground, "Replacement changed focus or activation.");
            if (i == 0) activeCount = HandleCount(window);
            else Assert(HandleCount(window) == activeCount, "Live handles grew across replacements.");
        }
        host.Clear();
        Assert(HandleCount(window) == baseline, "Clearing a scope leaked handles.");
        using (var old = host.BeginUpdate())
        {
            var label = window.Label("Keep old");
            old.Commit(label);
            var count = HandleCount(window);
            for (int i = 0; i < 20; i++)
            {
                using (var aborted = host.BeginUpdate())
                {
                    window.Button("Aborted").OnClick(() => throw new InvalidOperationException("Must never run"));
                    Throws<XuiException>(() => host.BeginUpdate(), "Nested candidate was accepted.");
                    Throws<XuiException>(host.Clear, "A pending candidate allowed clear.");
                    Throws<XuiException>(() => aborted.Commit(editor), "A foreign root was accepted.");
                    Throws<InvalidOperationException>(() => window.KeyHandler = _ => true, "Candidate replaced the window key handler.");
                }
                Assert(label.Text == "Keep old", "Candidate rollback replaced old content.");
                Assert(HandleCount(window) == count, "Candidate rollback leaked handles.");
            }
        }
        var weakOwners = Enumerable.Range(0, 20).Select(_ => CreateOwnedResources(window, host)).ToArray();
        host.Clear();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Assert(weakOwners.All(x => !x.IsAlive), "Retired callback or source roots retained authored objects.");
        Assert(HandleCount(window) == baseline, "Resource ownership leaked native handles.");
        foreach (var name in new[] { "subscriptions", "menuSubscriptions", "fileSubscriptions", "millerSubscriptions", "contentScopes" })
        {
            var entries = (System.Collections.IDictionary)typeof(Window).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;
            Assert(entries.Count == 0, $"Retirement leaked {name}.");
        }
        var callbackScope = host.BeginUpdate();
        var throwing = window.Button("Throwing callback");
        int failures = 0;
        bool stalePostRan = false;
        callbackScope.CallbackFailed += error =>
        {
            try
            {
                failures++;
                Assert(error.Message == "Authored callback", "Wrong scoped callback diagnostic.");
                Assert(failures == 1 && window.CallbackStatus == 0, "Scoped callback closed the editor.");
                callbackScope.Dispose();
                Assert(!stalePostRan, "A retired or failed scope ran a queued callback.");
                Assert(HandleCount(window) == baseline, "Failed scope leaked handles.");
                complete();
            }
            catch (Exception error2) { failure = error2; window.Close(); }
        };
        throwing.Click += () =>
        {
            window.Post(() => stalePostRan = true);
            throw new InvalidOperationException("Authored callback");
        };
        callbackScope.Commit(throwing);
        throwing.Invoke();
        throwing.Invoke();
    }

    private static void ClosePendingTests(byte[] assembly)
    {
        for (int i = 0; i < 3; i++)
        {
            using var window = new Window("Pending preview close", 400, 300);
            window.SetShowActivated(false);
            int reports = 0;
            using var preview = new PreviewHost(window, (_, _, _) => reports++);
            window.SetContent(window.Stack().Add(preview.View, 1));
            window.Post(window.Close);
            preview.Supersede(1);
            preview.Publish(1, assembly, Theme.Dark);
            window.Run();
            Assert(reports == 0, "A pending preview reported after close.");
            preview.Dispose();
            preview.Publish(1, assembly, Theme.Dark);
            Assert(reports == 0, "A disposed preview accepted a publication.");
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference CurrentPreviewContext() =>
        new(AssemblyLoadContext.All.Single(c => c.Name == "XUI embedded preview"), trackResurrection: true);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference CreateOwnedResources(Window window, ContentHost host)
    {
        var update = host.BeginUpdate();
        var owner = new Source();
        var source = window.ImmutableSource(owner);
        var grid = window.DataGrid("Owned grid").SetSource(source);
        grid.OnContextMenu(() => { owner.Touch(); return []; }, _ => owner.Touch());
        grid.OnFileDrag(() => { owner.Touch(); return []; }, _ => owner.Touch());
        grid.OnFileDrop((_, _) => { owner.Touch(); return FileTransferEffect.None; },
            (_, _, _) => { owner.Touch(); return FileTransferEffect.None; });
        var columns = window.MillerColumns("Owned columns");
        columns.SelectionChanged += _ => owner.Touch();
        var button = window.Button("Owned button");
        button.Click += owner.Touch;
        window.Post(owner.Touch);
        var label = window.Label("Owned resources");
        label.SetControlStyle(new ControlStyle(StyleTarget.Label,
            [new PartStyle(StylePart.Label, new PartStyleValues { FontSize = 14 })]));
        update.Commit(label);
        return new WeakReference(owner);
    }
    private sealed class Source : IReadOnlyImmutableSource
    {
        public ulong Count => 1;
        public ItemKey Key(ulong index) => new(1, 0);
        public ulong? Find(ItemKey key) => key.Id == 1 ? 0UL : null;
        public ItemContent Item(ulong index, ulong column = 0) => new("Owned source");
        public void Touch() { }
    }

    private static uint HandleCount(Window window)
    {
        int status = ContentHandleCount(WindowHandle(window), out var count);
        Assert(status == 0, "Could not read native handle count.");
        return count;
    }
    private static ulong WindowHandle(Window window) =>
        (ulong)typeof(Window).GetProperty("Handle", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(window)!;
    private static List<nint> Children(nint window)
    {
        var result = new List<nint>();
        EnumChildWindows(window, (child, _) => { result.Add(child); return true; }, 0);
        return result;
    }
    private static string ClassName(nint window)
    {
        var text = new StringBuilder(256); GetClassNameW(window, text, text.Capacity); return text.ToString();
    }
    private static string WindowText(nint window)
    {
        var text = new StringBuilder(1024); GetWindowTextW(window, text, text.Capacity); return text.ToString();
    }
    private static string[] Texts(nint window) => Children(window).Select(WindowText).ToArray();
    private static void Throws<T>(Action action, string message) where T : Exception
    {
        try { action(); }
        catch (T) { assertions++; return; }
        throw new InvalidOperationException(message);
    }
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
    private static Button OnClick(this Button button, Action action) { button.Click += action; return button; }
    private delegate bool EnumChild(nint hwnd, nint parameter);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(nint parent, EnumChild callback, nint parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern nint FindWindowW(string? name, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(nint window, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassNameW(nint window, StringBuilder text, int count);
    [DllImport("user32.dll")] private static extern bool IsWindow(nint window);
    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] private static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] private static extern nint SendMessageW(nint window, uint message, nint first, nint second);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)] private static extern nint ReplaceSelection(nint window, uint message, nint first, string text);
    [DllImport("xui", EntryPoint = "xui_content_handle_count")] private static extern int ContentHandleCount(ulong window, out uint count);
    [DllImport("xui", EntryPoint = "xui_text_copy")] private static extern int TextCopy(ulong control, [Out] byte[]? bytes, uint capacity, out uint count);
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct Point { public int X, Y; }
    [DllImport("user32")] private static extern bool GetWindowRect(nint hwnd, out Rect rect);
    [DllImport("user32")] private static extern bool ScreenToClient(nint hwnd, ref Point point);
    [DllImport("user32")] private static extern uint GetDpiForWindow(nint hwnd);
}

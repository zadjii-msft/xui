using System.Reflection;
using PortableDemo;
using PortableMutation;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static string Resource(string name)
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)
            ?? throw new InvalidOperationException($"Missing resource {name}.");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }

    private static int CallbackCount(Window window) =>
        ((System.Collections.IDictionary)typeof(Window).GetField("subscriptions",
            BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(window)!).Count;

    private static void AppendOwnership()
    {
        using var window = new Window("Append ownership");
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var content = surface.BeginUpdate();
        var root = window.Stack();
        var retained = window.Button("Retained");
        retained.Click += () => { };
        var lateSubscribed = window.Button("Retained, subscribed during append");
        root.Add(retained).Add(lateSubscribed);
        content.Commit(root);
        uint attached = HandleCount(window);
        int callbacks = CallbackCount(window);
        bool rolledBackPost = false;
        int retainedEvents = 0;
        for (int i = 0; i < 12; i++)
        {
            TextInput abandoned;
            using (content.BeginAppend())
            {
                abandoned = window.TextInput("Abandoned append");
                abandoned.Changed += _ => { };
                if (i == 0) lateSubscribed.Click += () => retainedEvents++;
                Check(content.Post(() => rolledBackPost = true), "The append post was rejected.");
            }
            Check(HandleCount(window) == attached && CallbackCount(window) == callbacks + 1,
                "Append rollback leaked native handles or managed subscriptions.");
            Throws<XuiException>(() => _ = abandoned.Text);
        }
        using (var append = content.BeginAppend())
        {
            var extra = window.Stack();
            var button = window.Button("Added");
            button.Click += () => { };
            extra.Add(button);
            append.Complete();
            root.Insert(1, extra);
            Throws<XuiException>(() => content.ReleaseElement(button));
            root.Remove(extra);
            content.ReleaseElement(button);
            content.ReleaseElement(extra);
        }
        Check(HandleCount(window) == attached && CallbackCount(window) == callbacks + 1,
            "Detached element release leaked callbacks or handles.");
        Check(window.Post(() => { lateSubscribed.Invoke(); window.Close(); }), "The append fixture close was rejected.");
        window.Run();
        Check(!rolledBackPost, "Rolled-back append work executed.");
        Check(retainedEvents == 1, "Append rollback retired a subscription on a pre-existing native element.");
        content.Dispose();
        Check(HandleCount(window) == baseline, "Retiring mutated content leaked native resources.");

        using var closing = new Window("Closing append ownership");
        var closingSurface = Surface(closing);
        uint closingBaseline = HandleCount(closing);
        using var closingContent = closingSurface.BeginUpdate();
        closingContent.Commit(closing.Stack().Add(closing.Label("Closing fixture")));
        bool releasedWhileClosing = false;
        Check(closing.Post(() =>
        {
            using var append = closingContent.BeginAppend();
            closing.TextInput("Unfinished append").Changed += _ => { };
            closing.Close();
            closingContent.Dispose();
            releasedWhileClosing = HandleCount(closing) == closingBaseline;
        }), "Closing append cleanup was not posted.");
        closing.Run();
        Check(releasedWhileClosing, "Window close stranded an unfinished append arena.");
    }

    private static void NativeMutationScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var board = new MutationBoard(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var driver = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                Interlocked.Add(ref assertions, MutationScenarioRunner.Run(Resource("MutationScenarios.json"), driver));
                driver.Click("reset-rows");
                uint empty = driver.Ui(() => HandleCount(window));
                for (int iteration = 0; iteration < 8; iteration++)
                {
                    driver.Click("add-row");
                    driver.Click("add-row");
                    driver.Change("row-1-input", "retained draft");
                    nint first = driver.Ui(() =>
                    {
                        var input = driver.Input("row-1-input");
                        var portableInput = (P.TextInput)driver.Model("row-1-input");
                        Check(host.TryFocus(portableInput), "A keyed portable input did not accept native focus.");
                        host.SetSelection(portableInput, new(2, 5));
                        nint edit = GetFocus();
                        uint before = HandleCount(window);
                        board.Rows = board.Rows.Reverse().ToArray();
                        Check(GetFocus() == edit && input.Selection == new TextSelection(2, 5) &&
                            host.HasFocus(portableInput) && host.GetSelection(portableInput) == new P.TextSelection(2, 5) &&
                            HandleCount(window) == before, "Move recreated an editor, selection, or native handle.");
                        Throws<P.KeyedUpdateException>(() => board.Rows = [board.Rows[0], board.Rows[0]]);
                        Check(GetFocus() == edit && HandleCount(window) == before, "Duplicate rejection changed native state.");
                        Throws<P.KeyedUpdateException>(() => board.Rows = [.. board.Rows,
                            P.KeyedItem.Create<MutationRow>("bad-factory", _ => throw new InvalidOperationException("factory fixture"))]);
                        Check(host.IsAttached && GetFocus() == edit, "Factory rejection detached live content.");
                        if (iteration == 0)
                        {
                            var rows = board.Rows;
                            bool factoryRan = false;
                            SendMessage(edit, 0x010d, 0, 0);
                            try
                            {
                                RejectComposition(() => board.Rows = [.. rows,
                                    P.KeyedItem.Create("during-ime", h => { factoryRan = true; return new MutationRow(h, "during-ime"); })]);
                                RejectComposition(() => board.Rows = rows[..1]);
                                RejectComposition(() => board.Rows = rows.Reverse().ToArray());
                                board.Rows = [.. rows];
                                Check(!factoryRan && host.IsAttached && GetFocus() == edit && HandleCount(window) == before,
                                    "IME preflight ran a factory, changed native content, or rejected a property-only update.");
                            }
                            finally { SendMessage(edit, 0x010e, 0, 0); }
                        }
                        return edit;
                    });
                    Check(driver.Order().SequenceEqual(["row-2", "row-1"]), "Native row geometry did not follow semantic order.");
                    if (iteration == 0)
                    {
                        driver.Ui(() =>
                        {
                            driver.Input("row-2-input").Focus();
                            Check(PostMessage(GetFocus(), 0x0100, 0x09, 1), "The reordered Tab fixture was rejected.");
                        });
                        driver.Wait(() => driver.Native("row-2-increment").Focused);
                        driver.Ui(() => Check(PostMessage(GetFocus(), 0x0100, 0x09, 1), "The second reordered Tab was rejected."));
                        driver.Wait(() => driver.Input("row-1-input").Focused);
                    }
                    driver.Ui(() =>
                    {
                        var row = board.Rows.Single(item => item.Key == "row-1");
                        var other = board.Rows.Single(item => item.Key == "row-2");
                        var portableInput = (P.TextInput)driver.Model("row-1-input");
                        board.Rows = [row, other];
                        driver.Input("row-1-input").Focus();
                        Check(GetFocus() == first, "A second move lost retained native identity.");
                        SendText(first, 0x000c, 0, "event on removed row");
                        board.Rows = [other];
                        Check(!IsWindow(first), "Removing the focused row left its native editor alive.");
                        Throws<ObjectDisposedException>(() => host.GetSelection(portableInput));
                    });
                    driver.Ui(() => Check(host.IsAttached && !backend.FindControls("row-1-input").Any(),
                        "A removed input callback resurrected its peer."));
                    driver.Click("reset-rows");
                    driver.Ui(() => Check(HandleCount(window) == empty, "Dynamic row cycles leaked native handles."));
                }

                driver.Ui(() =>
                {
                    board.Rows = [P.KeyedItem.Create("typed", h => new MutationRow(h, "typed"))];
                    var previous = (TextInput)backend.FindControls("typed-input").Single();
                    previous.Focus();
                    var oldEdit = GetFocus();
                    board.Rows = [P.KeyedItem.Create("typed", h => new MutationBanner(h, "typed-banner"))];
                    Check(!IsWindow(oldEdit) && !backend.FindControls("typed-input").Any() &&
                        backend.FindControls("typed-banner").Count == 1, "Changing component type retained an obsolete editor.");
                    board.Rows = [];
                    try
                    {
                        board.Rows = [P.KeyedItem.Create("bad-native", h =>
                            new MutationRow(h, "bad-native") { Entry = new string('x', 1_048_577) })];
                        throw new InvalidOperationException("The invalid native append unexpectedly succeeded.");
                    }
                    catch (P.KeyedUpdateException error)
                    {
                        Check(error.ModelCommitted && !host.IsAttached && HandleCount(window) == baseline,
                            "Partial native append failure did not detach the complete arena.");
                    }
                    board.Rows = [];
                    backend = new WindowsBackend(surface, dispatcher);
                    host.Attach(backend);
                    Check(HandleCount(window) == empty, "Failed append retained hidden native allocations.");
                    host.Detach();
                    Check(HandleCount(window) == baseline, "Mutable attachment leaked after detach.");
                });
            }
            finally { driver.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }

    private static void RejectComposition(Action action)
    {
        try { action(); }
        catch (P.KeyedUpdateException error)
        {
            Check(!error.ModelCommitted && error.InnerException is XuiException { Status: 7 },
                "Composition mutation was not rejected before model commit.");
            return;
        }
        throw new InvalidOperationException("Structural mutation was accepted during native composition.");
    }

    private static void DynamicApplicationScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var board = DynamicTaskBoard.Create(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var driver = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                Interlocked.Add(ref assertions, ApplicationScenarioRunner.Run(Resource("DynamicTaskScenarios.json"), "dynamic-task-board", driver));
                driver.Ui(() =>
                {
                    var input = driver.Input("dynamic-task-2-title");
                    input.Focus();
                    input.Selection = new(1, 4);
                    nint edit = GetFocus();
                    board.RestoreState(board.GetState().Reverse());
                    Check(GetFocus() == edit && input.Selection == new TextSelection(1, 4),
                        "Shared task reordering lost the native editing session.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(edit), "Shared dynamic app teardown leaked native content.");
                });
            }
            finally { driver.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }
}

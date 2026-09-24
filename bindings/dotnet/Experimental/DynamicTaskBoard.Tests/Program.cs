using System.Collections.Immutable;
using System.Text.Json;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static int assertions;
    private static void Main()
    {
        ModelChecks();
        GeneratedChecks();
        FailureChecks();
        Console.WriteLine($"Dynamic task board: {assertions} assertions passed.");
    }
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void ModelChecks()
    {
        var empty = new DynamicTaskBoardState();
        Assert(empty.Items.Length == 0 && empty.NextId == 1 && empty.VisibleItems.Length == 0 && !empty.CanAdd, "A raw model starts empty.");
        var seed = DynamicTaskBoardState.Seed();
        Assert(seed.Items.Select(i => i.Id).SequenceEqual([1L, 2L, 3L]) && seed.NextId == 4 && seed.Summary == "3 tasks / 1 done", "Literal seed order and counts.");
        Assert(seed.WithFilter(DynamicTaskFilter.Open).VisibleItems.Select(i => i.Key).SequenceEqual(["dynamic-task-2", "dynamic-task-3"]), "Open projection.");
        Assert(seed.WithFilter(DynamicTaskFilter.Done).VisibleItems.Select(i => i.Key).SequenceEqual(["dynamic-task-1"]), "Completed projection.");
        var added = seed.WithFilter(DynamicTaskFilter.Done).WithDraft("  Zo\u00eb \u674e  ").AddDraft();
        Assert(added.Items.Length == 4 && added.Items[3] == new DynamicTaskItem(4, "  Zo\u00eb \u674e  ", false), "Add preserves exact Unicode draft.");
        Assert(added.Filter == DynamicTaskFilter.All && added.DraftTitle == "" && added.NextId == 5, "Add shows new row and advances stable ID.");
        Assert(seed.Items.Length == 3 && seed.NextId == 4 && seed.DraftTitle == "", "Old snapshots immutable.");
        var reversed = added.Reverse();
        Assert(reversed.Items.Select(i => i.Id).SequenceEqual([4L, 3L, 2L, 1L]), "Literal reverse order.");
        var removed = added.Remove("dynamic-task-4").WithDraft("Next task").AddDraft();
        Assert(removed.Items[3].Id == 5 && removed.NextId == 6, "Removed identities are not reused.");
        var renamed = seed.Rename("dynamic-task-2", "Release checklist").Toggle("dynamic-task-2");
        Assert(renamed.Items[1].Title == "Release checklist" && renamed.Items[1].Completed && renamed.Summary == "3 tasks / 2 done", "Rename and complete by key.");
        var blank = seed.Rename("dynamic-task-2", "  ");
        Assert(!blank.Items[1].CanToggle && blank.Items[1].Validation == "Enter a title before changing status.", "Blank row draft retained with explicit validation.");
        Throws<InvalidOperationException>(() => blank.Toggle("dynamic-task-2"));
        Throws<InvalidOperationException>(() => seed.AddDraft());
        Throws<KeyNotFoundException>(() => seed.Remove("missing"));
        Throws<KeyNotFoundException>(() => seed.Rename("dynamic-task-999", "x"));
        Throws<ArgumentOutOfRangeException>(() => _ = new DynamicTaskItem(0, "bad", false));
        Throws<ArgumentNullException>(() => _ = new DynamicTaskItem(1, null!, false));
        Throws<ArgumentException>(() => _ = new DynamicTaskItem(1, "a\0b", false));
        Throws<ArgumentException>(() => _ = new DynamicTaskBoardState(default, 1, "", DynamicTaskFilter.All));
        Throws<ArgumentException>(() => _ = new DynamicTaskBoardState([new(1, "a", false), new(1, "b", true)], 2, "", DynamicTaskFilter.All));
        Throws<ArgumentOutOfRangeException>(() => _ = new DynamicTaskBoardState([new(4, "a", false)], 4, "", DynamicTaskFilter.All));
        Throws<ArgumentOutOfRangeException>(() => seed.WithFilter((DynamicTaskFilter)99));
        var exhausted = new DynamicTaskBoardState([], long.MaxValue, "Task", DynamicTaskFilter.All);
        Assert(!exhausted.CanAdd && exhausted.Validation == "No task IDs remain in this board.", "ID exhaustion doesn't overflow.");
        Throws<InvalidOperationException>(() => exhausted.AddDraft());
        var finalId = new DynamicTaskBoardState([], long.MaxValue - 1, "Last task", DynamicTaskFilter.All).AddDraft();
        Assert(finalId.Items[0].Id == long.MaxValue - 1 && finalId.NextId == long.MaxValue && !finalId.WithDraft("Next").CanAdd, "Final safe ID boundary.");

        Assert(!JsonSerializer.IsReflectionEnabledByDefault, "State serialization never uses reflection.");
        var saved = added.Rename("dynamic-task-2", "").WithFilter(DynamicTaskFilter.Open).WithDraft(" draft ");
        string json = DynamicTaskBoardCodec.Serialize(saved);
        var restored = DynamicTaskBoardCodec.Restore(json);
        Assert(restored.Items.SequenceEqual(saved.Items) && restored.NextId == 5 && restored.DraftTitle == " draft " &&
            restored.Filter == DynamicTaskFilter.Open, "Immutable state and invalid drafts restore exactly.");
        Assert(DynamicTaskBoardCodec.Serialize(restored) == json && !json.Contains("VisibleItems", StringComparison.Ordinal) &&
            !json.Contains("Key", StringComparison.Ordinal) && !json.Contains("Summary", StringComparison.Ordinal), "Derived views aren't serialized.");
        Throws<JsonException>(() => DynamicTaskBoardCodec.Restore("null"));
        Throws<JsonException>(() => DynamicTaskBoardCodec.Restore("{}"));
        Throws<JsonException>(() => DynamicTaskBoardCodec.Restore("{\"Items\":[],\"NextId\":1,\"DraftTitle\":\"\"}"));
        Throws<JsonException>(() => DynamicTaskBoardCodec.Restore("{\"Items\":[],\"NextId\":1,\"DraftTitle\":\"\",\"Filter\":0,\"extra\":true}"));
        Throws<ArgumentException>(() => DynamicTaskBoardCodec.Restore("{\"Items\":[{\"Id\":1,\"Title\":\"a\",\"Completed\":false},{\"Id\":1,\"Title\":\"b\",\"Completed\":false}],\"NextId\":2,\"DraftTitle\":\"\",\"Filter\":0}"));
    }

    private static void GeneratedChecks()
    {
        using var host = new Host(new Dispatcher());
        var app = DynamicTaskBoard.Create(host);
        var backend = new Backend();
        host.Attach(backend);
        Assert(app.RowsView.Children.Count == 3 && app.SummaryLabel.Text == "3 tasks / 1 done", "Seeded shared factory builds real keyed rows.");
        Assert(app.Root.Children.Count == 2 && app.Root.Children[0] is Label { AutomationId: "dynamic-title" } &&
            app.Root.Children[1] is ScrollView { AutomationId: "dynamic-content", Flex: 1 },
            "Only the title is fixed above the flexible scroll viewport.");
        var scroll = (ScrollView)app.Root.Children[1];
        var scrollElements = ActiveElements(scroll).ToHashSet();
        Assert(ActiveElements(app.Root).OfType<Control>().Where(control => control.AutomationId != "dynamic-title")
            .All(control => scrollElements.Contains(control)), "All inputs, actions, summary, and keyed rows are scrollable.");
        int count = ApplicationScenarioRunner.Run(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "DynamicTaskScenarios.json")),
            "dynamic-task-board", new Driver(backend));
        assertions += count;
        Assert(count >= 40, "Dynamic corpus has at least forty literal expectations.");
        Console.WriteLine($"Dynamic corpus: {count} literal fixture expectations.");
        Assert(DynamicTaskBoardCodec.Serialize(app.GetState()) == DynamicTaskBoardCodec.Serialize(DynamicTaskBoardState.Seed()), "Corpus finishes at exact seed.");

        var row1 = backend.Find("dynamic-task-1-title");
        var row2 = backend.Find("dynamic-task-2-title");
        var row3 = backend.Find("dynamic-task-3-title");
        var allInputs = backend.Peers.Where(p => !p.Disposed && p.Element is TextInput).ToArray();
        foreach (var input in allInputs)
        {
            input.Updates.Clear();
            var model = (TextInput)input.Element;
            Assert(model.CaptionVisible && model.FixedSize is null && model.PreferredSize is null && model.Help.Length > 0, "Every dynamic editor has natural captioned sizing.");
        }
        int changes = 0;
        ((TextInput)row2.Element).Changed += _ => changes++;
        Assert(row2.Events.Change("  Edited task \u674e  ") && changes == 1, "Row editor updates immutable parent state.");
        Assert(app.GetState().Items[1].Title == "  Edited task \u674e  " && allInputs.All(p => p.Updates.Count == 0), "Native edits aren't rewritten by reconciliation.");
        Assert(row2.Events.Change("  Edited task \u674e  ") && changes == 1, "Identical native event is silent.");
        var originalRoots = app.RowsView.Children.ToArray();
        var nativeRows = backend.Peers.Single(p => ReferenceEquals(p.Element, app.RowsView));
        var originalNativeChildren = nativeRows.Children.ToArray();
        backend.Find("dynamic-reverse").Events.Click();
        Assert(app.GetState().Items.Select(i => i.Id).SequenceEqual([3L, 2L, 1L]), "Generated reverse handler changes true row order.");
        Assert(app.RowsView.Children.SequenceEqual(originalRoots.Reverse()), "Same row roots move, not rebuild.");
        Assert(nativeRows.Children.SequenceEqual(originalNativeChildren.Reverse()), "Native child order changes with model order.");
        Assert(ReferenceEquals(row1, backend.Find("dynamic-task-1-title")) && ReferenceEquals(row2, backend.Find("dynamic-task-2-title")) &&
            ReferenceEquals(row3, backend.Find("dynamic-task-3-title")), "Every surviving native input peer retains identity.");
        Assert(allInputs.All(p => p.Updates.Count == 0), "Reordering does not reset editor text or composition.");

        backend.Find("dynamic-filter-open").Events.Click();
        Assert(row1.Disposed && !row1.Events.Change("stale"), "Filtering out a row disposes its view and invalidates callbacks.");
        Assert(!row2.Disposed && ReferenceEquals(row2, backend.Find("dynamic-task-2-title")), "Other filtered rows survive.");
        backend.Find("dynamic-filter-all").Events.Click();
        Assert(!ReferenceEquals(row1, backend.Find("dynamic-task-1-title")), "Filtered-in row gets a fresh view.");
        Assert(app.GetState().Items.Single(i => i.Id == 2).Title == "  Edited task \u674e  ", "All row edits survive projection changes.");
        backend.Find("dynamic-filter-open").Events.Click();
        var toggle = backend.Find("dynamic-task-2-toggle");
        Assert(toggle.Events.Click() && toggle.Disposed && row2.Disposed, "Completion can safely remove its own active filtered row.");
        Assert(!toggle.Events.Click() && !row2.Events.Change("retired"), "Self-removed event sinks stay inert.");
        Assert(app.GetState().Items.Single(i => i.Id == 2).Completed, "Completion persisted in board data.");
        backend.Find("dynamic-task-3-remove").Events.Click();
        Assert(app.RowsView.Children.Count == 0 && ((Control)backend.Find("dynamic-empty").Element).Visible, "Removing last visible row shows explicit empty state.");
        Assert(app.GetState().Items.Select(i => i.Id).SequenceEqual([2L, 1L]), "Hidden tasks weren't deleted.");
        backend.Find("dynamic-filter-all").Events.Click();
        for (int i = 0; i < 10; i++)
        {
            backend.Find("dynamic-filter-open").Events.Click();
            backend.Find("dynamic-filter-all").Events.Click();
        }
        Assert(backend.Peers.Count(p => !p.Disposed) == ActiveElements(host.Root!).Count(), "Repeated filtering leaves exactly the live tree's peers.");

        string saved = DynamicTaskBoardCodec.Serialize(app.GetState());
        var currentInput = backend.Find("dynamic-task-2-title");
        host.Detach();
        Assert(backend.Disposed && backend.Peers.All(p => p.Disposed), "Detach releases dynamic and static peers.");
        Assert(!currentInput.Events.Change("detached"), "Detached input callback rejected.");
        var replacement = new Backend();
        host.Attach(replacement);
        Assert(((TextInput)replacement.Find("dynamic-task-2-title").Element).Text == "  Edited task \u674e  ", "Reattachment retains task draft.");
        using var restoredHost = new Host(new Dispatcher());
        var restoredApp = DynamicTaskBoard.Create(restoredHost, DynamicTaskBoardCodec.Restore(saved));
        var restoredBackend = new Backend();
        restoredHost.Attach(restoredBackend);
        Assert(DynamicTaskBoardCodec.Serialize(restoredApp.GetState()) == saved &&
            ((TextInput)restoredBackend.Find("dynamic-task-2-title").Element).Text == "  Edited task \u674e  ", "Fresh host restoration reproduces keyed state.");
        app.RestoreState(new DynamicTaskBoardState());
        Assert(app.RowsView.Children.Count == 0 && app.GetState().Items.Length == 0 &&
            app.SummaryLabel.Text == "0 tasks / 0 done", "Restoring an empty board removes every task.");
        Assert(!replacement.Find("dynamic-reverse").Events.Click() && !replacement.Find("dynamic-add").Events.Click(), "Empty-board invalid actions are disabled.");
        replacement.Find("dynamic-draft").Events.Change("First real task");
        replacement.Find("dynamic-draft").Events.Submit();
        Assert(app.GetState().Items.Length == 1 && app.GetState().NextId == 2 &&
            ((TextInput)replacement.Find("dynamic-task-1-title").Element).Text == "First real task", "Empty collection can add its first keyed task.");
        Assert(!replacement.Find("dynamic-reverse").Events.Click(), "Single-item reversal is disabled.");
        host.Dispose();
        Assert(replacement.Peers.All(p => p.Disposed), "Terminal disposal cleans keyed rows.");
        Throws<ObjectDisposedException>(() => app.GetState());
        Throws<ObjectDisposedException>(() => app.RestoreState(DynamicTaskBoardState.Seed()));
    }

    private static IEnumerable<Element> ActiveElements(Element root)
    {
        yield return root;
        foreach (var child in root.Children)
            foreach (var element in ActiveElements(child)) yield return element;
    }

    private static void FailureChecks()
    {
        using var host = new Host(new Dispatcher());
        var app = DynamicTaskBoard.Create(host);
        var unsupported = new FixedBackend();
        Throws<NotSupportedException>(() => host.Attach(unsupported));
        Assert(!host.IsAttached && unsupported.Disposed && unsupported.Peers.All(p => p.Disposed), "Nonmutable backend fails explicitly and cleans partial attachment.");
        var backend = new Backend();
        host.Attach(backend);
        var originalState = app.GetState();
        var originalRows = app.Rows;
        var input = backend.Find("dynamic-task-2-title");
        backend.RejectMove = true;
        var rejected = Throws<KeyedUpdateException>(() => backend.Find("dynamic-reverse").Events.Click());
        Assert(!rejected.ModelCommitted && ReferenceEquals(app.GetState(), originalState) && ReferenceEquals(app.Rows, originalRows), "Precommit move rejection preserves descriptor and application snapshots.");
        Assert(host.IsAttached && ReferenceEquals(input, backend.Find("dynamic-task-2-title")), "Precommit rejection preserves attached input identity.");
        backend.RejectMove = false;
        backend.Find("dynamic-reverse").Events.Click();
        Assert(app.GetState().Items.Select(i => i.Id).SequenceEqual([3L, 2L, 1L]), "Explicit retry succeeds after movement is allowed.");
        backend.Find("dynamic-draft").Events.Change("Committed despite backend failure");
        backend.FailInsert = true;
        var committed = Throws<KeyedUpdateException>(() => backend.Find("dynamic-add").Events.Click());
        Assert(committed.ModelCommitted && !host.IsAttached && backend.Disposed, "Postcommit native failure detaches and is surfaced.");
        Assert(app.GetState().Items.Select(i => i.Id).SequenceEqual([3L, 2L, 1L, 4L]) &&
            app.GetState().NextId == 5 && app.GetState().DraftTitle == "" && app.Rows.Length == 4, "Postcommit application snapshot follows committed model.");
        Assert(backend.Peers.All(p => p.Disposed) && !input.Events.Change("old"), "Failed backend peers and sinks are retired.");
        var recovered = new Backend();
        host.Attach(recovered);
        Assert(((TextInput)recovered.Find("dynamic-task-4-title").Element).Text == "Committed despite backend failure" &&
            app.SummaryLabel.Text == "4 tasks / 1 done", "Recovery displays committed task and summary without silent rollback.");
    }
}

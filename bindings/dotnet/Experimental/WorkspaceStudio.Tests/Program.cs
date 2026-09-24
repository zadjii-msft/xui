using System.Globalization;
using System.Text.Json;
using PortableDemo;

internal static partial class Program
{
    private static int assertions;
    private static void Main()
    {
        ModelChecks();
        CodecChecks();
        AnalysisChecks();
        ControllerChecks();
        CatalogChecks();
        EditorChecks();
        WorkspaceChecks();
        OperationsChecks();
        BackChecks();
        OperationsDrawerChecks();
        ShortHeightChecks();
        Console.WriteLine($"Workspace studio: {assertions} assertions passed.");
    }
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T error) { assertions++; return error; }
        throw new InvalidOperationException("Expected " + typeof(T).Name);
    }
    private static void ModelChecks()
    {
        Assert(StudioCatalog.Entries.Length == 10000 && StudioCatalog.Entries[0].Key == "doc-00001" &&
            StudioCatalog.Entries[9999].Key == "doc-10000", "Catalog has exactly 10,000 stable generated identities.");
        Assert(StudioCatalog.Get("doc-00001").NativeId == 1 && StudioCatalog.Get("doc-10000").NativeId == 10000 &&
            StudioCatalog.Entries.Select(entry => entry.NativeId).Distinct().Count() == 10000,
            "Native navigation/tab identifiers map stably to documents, never filtered row positions.");
        Assert(StudioCatalog.Get("doc-00001").Title == "Keyboard navigation 00001" &&
            StudioCatalog.Get("doc-00002").Title == "Release checklist 00002" &&
            StudioCatalog.Get("doc-00001").Category == StudioCategory.Engineering, "Literal catalog examples.");
        foreach (string key in new[] { "", "doc-1", "doc-00000", "doc-10001", "DOC-00001", "doc-0000a" })
            Throws<ArgumentException>(() => StudioCatalog.Get(key));
        var seed = WorkspaceStudioSession.Seed();
        Assert(seed.OpenTabs.SequenceEqual(["doc-00001", "doc-00002"]) && seed.ActiveDocument == "doc-00001" &&
            seed.SelectedDocument == "doc-00001" && seed.ChangedCount == 0 && seed.Summary == "2 open / 0 local drafts", "Literal initial document tabs.");
        Assert(seed.VisibleKeys().Length == 10000 && seed.WithCategory(StudioCategory.Engineering).VisibleKeys().Length == 3334 &&
            seed.WithCategory(StudioCategory.Design).VisibleKeys().Length == 3333 &&
            seed.WithCategory(StudioCategory.Research).VisibleKeys().Length == 3333, "Category counts are literal and independent.");
        Assert(seed.WithQuery(" keyboard ").VisibleKeys().Length == 2000 &&
            seed.WithQuery("doc-00002").VisibleKeys().SequenceEqual(["doc-00002"]), "Title and identity filtering is case-insensitive without rewriting query.");
        var edited = seed.Edit(seed.Document("doc-00001").WithTitle("  Updated plan  ").WithBody("First\r\nSecond\rThird"));
        Assert(edited.Document("doc-00001").Body == "First\nSecond\nThird" &&
            edited.Document("doc-00001").Title == "  Updated plan  " && edited.ChangedCount == 1 &&
            seed.Document("doc-00001").Title == "Keyboard navigation 00001", "Immutable drafts retain whitespace and normalize only native multiline line endings.");
        Assert(edited.WithQuery("updated").VisibleKeys().SequenceEqual(["doc-00001"]) &&
            edited.Navigate(StudioSection.Drafts).VisibleKeys().SequenceEqual(["doc-00001"]), "Modified title and drafts projection use document keys.");
        var closed = edited.CloseTab("doc-00001");
        Assert(closed.ActiveDocument == "doc-00002" && closed.OpenTabs.Length == 1 && closed.ChangedCount == 1 &&
            closed.Document("doc-00001").Body == "First\nSecond\nThird", "Closing a tab keeps unsaved local data without pretending it was saved.");
        var reopened = closed.OpenDocument("doc-00001");
        Assert(reopened.OpenTabs.SequenceEqual(["doc-00002", "doc-00001"]) &&
            reopened.Document("doc-00001").Title == "  Updated plan  ", "Reopening restores same document draft.");
        Assert(reopened.OpenDocument("doc-00001").OpenTabs.Length == 2, "Opening existing document only activates it.");
        var moved = reopened.MoveTab("doc-00001", 0);
        Assert(moved.OpenTabs.SequenceEqual(["doc-00001", "doc-00002"]) && moved.ActiveDocument == "doc-00001" &&
            moved.ChangedCount == 1, "Reordering tabs preserves active identity and data.");
        Assert(moved.ActivateTab("doc-00002").ActiveDocument == "doc-00002", "Tab activation is distinct from opening.");
        var none = closed.CloseTab("doc-00002");
        Assert(none.ActiveDocument == "" && none.ActiveDraft is null && none.ChangedCount == 1, "Closing the last tab keeps drafts with explicit empty active state.");
        Assert(none.OpenDocument("doc-00001").ActiveDraft!.Body == "First\nSecond\nThird", "Empty workspace can reopen unsaved draft.");
        Assert(edited.Revert("doc-00001").ChangedCount == 0 && edited.Revert("doc-00001").Document("doc-00001") == StudioCatalog.Original("doc-00001"),
            "Revert explicitly removes local delta and restores generated original.");
        Throws<InvalidOperationException>(() => closed.Edit(edited.Document("doc-00001")));
        Throws<InvalidOperationException>(() => closed.Revert("doc-00001"));
        Throws<InvalidOperationException>(() => seed.ActivateTab("doc-00003"));
        Throws<InvalidOperationException>(() => seed.CloseTab("doc-00003"));
        Throws<ArgumentOutOfRangeException>(() => seed.MoveTab("doc-00001", 2));
        Throws<ArgumentOutOfRangeException>(() => seed.MoveTab("doc-00001", -1));
        Assert(seed.WithQuery(new string('q', 257)).Query.Length == 257 &&
            seed.WithQuery(new string('q', 257)).QueryValidation == "Keep catalog search within 256 UTF-16 units.",
            "Overlong search draft is preserved with explicit validation instead of throwing from native typing.");
        Throws<ArgumentException>(() => seed.Document("doc-00001").WithTitle("a\nb"));
        foreach (string hardBreak in new[] { "\r", "\u0085", "\u2028", "\u2029" })
            Throws<ArgumentException>(() => seed.Document("doc-00001").WithTitle("a" + hardBreak + "b"));
        Assert(!seed.Document("doc-00001").WithTitle(new string('x', 161)).CanAnalyze,
            "Overlong native title draft is kept but explicitly blocks analysis.");
        Throws<ArgumentException>(() => seed.Document("doc-00001").WithBody(new string('x', 65537)));
        Throws<ArgumentException>(() => seed.Document("doc-00001").WithBody("\ud800"));
        Assert(!seed.Document("doc-00001").WithTitle(" ").CanAnalyze &&
            !seed.Document("doc-00001").WithBody(" \n ").CanAnalyze, "Incomplete drafts stay valid for editing but cannot be analyzed.");
        var full = seed;
        for (int index = 2; index < 16; index++) full = full.OpenDocument(StudioCatalog.Entries[index].Key);
        Assert(full.OpenTabs.Length == 16 && full.CanOpenDocument("doc-00001") && !full.CanOpenDocument("doc-00017"),
            "Large catalog is separate from the explicit sixteen-realized-editor resource bound.");
        Throws<InvalidOperationException>(() => full.OpenDocument("doc-00017"));
        Assert(full.CloseTab("doc-00005").OpenDocument("doc-00017").OpenTabs.Length == 16,
            "Closing a tab frees editor capacity without deleting other drafts.");
    }
    private static void CodecChecks()
    {
        Assert(!JsonSerializer.IsReflectionEnabledByDefault, "Studio session serialization uses generated metadata.");
        var session = WorkspaceStudioSession.Seed();
        session = session.Edit(session.Document("doc-00001").WithTitle("").WithBody(" \n incomplete \u674e "));
        session = session.CloseTab("doc-00001").Navigate(StudioSection.Drafts).WithQuery("  doc-00001  ").Interrupted(true);
        string json = WorkspaceStudioSessionCodec.Serialize(session);
        var restored = WorkspaceStudioSessionCodec.Restore(json);
        Assert(WorkspaceStudioSessionCodec.Serialize(restored) == json && restored.Document("doc-00001").Title == "" &&
            restored.Document("doc-00001").Body == " \n incomplete \u674e " && restored.ActiveDocument == "doc-00002" &&
            restored.AnalysisInterrupted && restored.Section == StudioSection.Drafts, "Session preserves incomplete and closed drafts, logical navigation, and interruption.");
        Assert(json.Length < 1000 && !json.Contains("doc-10000") && !json.Contains("NativeId") &&
            !json.Contains("PageKey") && !json.Contains("Summary"), "10,000 generated catalog entries and native/UI/task state are not serialized.");
        Assert(restored.Query == "  doc-00001  ", "Search draft text is not normalized on restoration.");
        foreach (string invalid in new[] { "null", "{}", "{", json.Replace("\"Version\":2", "\"Version\":99"),
            json.Replace("\"Version\":2", "\"Version\":2,\"Version\":2"),
            json.Replace("\"Version\":2", "\"Version\":2,\"Host\":{}"),
            json.Replace("\"Section\":1", "\"Section\":99"),
            json.Replace("\"ActiveDocument\":\"doc-00002\"", "\"ActiveDocument\":\"doc-00003\"") })
            Throws<JsonException>(() => WorkspaceStudioSessionCodec.Restore(invalid));
        Throws<JsonException>(() => new WorkspaceStudioSession(1, StudioSection.Library, StudioCategory.All, "",
            ["doc-00001", "doc-00001"], "doc-00001", "", [], false));
        Throws<JsonException>(() => new WorkspaceStudioSession(1, StudioSection.Library, StudioCategory.All, "",
            [], "doc-00001", "", [], false));
        Throws<JsonException>(() => new WorkspaceStudioSession(1, StudioSection.Library, StudioCategory.All, "",
            ["doc-00001"], "doc-00001", "", [StudioCatalog.Original("doc-00001"), StudioCatalog.Original("doc-00001")], false));
    }
    private static void AnalysisChecks()
    {
        var document = new StudioDocumentDraft("doc-00001", "Test plan", "One two\n- three four\n\n- five");
        var result = StudioAnalysis.Calculate(document);
        Assert(result == new StudioAnalysis("doc-00001", 28, 7, 4, 2) &&
            result.Summary == "7 words / 4 lines / 2 checklist items", "Analysis uses literal expected counts.");
        Throws<InvalidOperationException>(() => StudioAnalysis.Calculate(document.WithBody("")));
        var originalCulture = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("ar-SA");
            Assert(result.Summary == "7 words / 4 lines / 2 checklist items" &&
                StudioCatalog.Get("doc-00001").NativeId == 1, "Model identities and counts stay invariant.");
        }
        finally { CultureInfo.CurrentCulture = originalCulture; }
        var service = new LocalStudioAnalysisService();
        Assert(service.AnalyzeAsync(document, CancellationToken.None).GetAwaiter().GetResult() == result,
            "Real cancellable local asynchronous analysis matches deterministic model results.");
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Throws<OperationCanceledException>(() => service.AnalyzeAsync(document, cancellation.Token).GetAwaiter().GetResult());
    }
}

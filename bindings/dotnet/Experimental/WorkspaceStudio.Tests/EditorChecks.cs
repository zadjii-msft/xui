using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void EditorChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var service = new AnalysisService();
        var errors = new List<Exception>();
        var controller = new WorkspaceStudioController(host, service, errors.Add);
        var editor = new StudioDocumentEditor(host, "doc-00001", controller);
        controller.Attach(editor.Lifetime, state =>
        {
            editor.Draft = state.Session.Document("doc-00001");
            editor.HasChanges = state.Session.HasDraft("doc-00001");
            editor.Analyzing = state.Analyzing && state.Session.ActiveDocument == "doc-00001";
        });
        var backend = new CatalogBackend(dispatcher);
        host.Attach(backend);
        var title = backend.Find("doc-00001-title");
        var body = backend.Find("doc-00001-body");
        int edits = 0;
        editor.BodyInput.Changed += _ => edits++;
        Assert(editor.TitleInput.CaptionVisible && editor.TitleInput.FixedSize is null &&
            editor.TitleInput.PreferredSize is null && editor.BodyInput.Flex == 1 &&
            editor.BodyInput.MaximumLength == 65536, "Document uses a natural captioned title and a bounded multiline editor filling available space.");
        Assert(body.Events.Change("One two\r\n- three four\r\r- five"), "Native document input accepted.");
        Assert(editor.BodyInput.Text == "One two\n- three four\n\n- five" &&
            controller.State.Session.Document("doc-00001").Body == "One two\n- three four\n\n- five" &&
            edits == 1 && body.Updates.Count == 0, "Native document input is retained without programmatic rewriting.");
        var peers = backend.Peers.ToArray();
        controller.Analyze();
        Assert(editor.Analyzing && !((Control)backend.Find("doc-00001-analyze").Element).Enabled &&
            body.Events.Change("Updated content\n- item"), "Owned analysis does not disable the user's native editing.");
        Assert(service.Pending[0].Token.IsCancellationRequested && !editor.Analyzing && body.Updates.Count == 0 &&
            peers.SequenceEqual(backend.Peers), "Editing cancels stale analysis without replacing or resetting native editor peers.");
        service.Pending[0].Completion.SetResult(StudioAnalysis.Calculate(service.Pending[0].Draft));
        dispatcher.Until(() => controller.LastOperation.IsCompleted);
        string oversized = new('x', 161);
        Assert(title.Events.Change(oversized) && editor.TitleInput.Text == oversized &&
            editor.Draft.Title == oversized && !editor.Draft.CanAnalyze, "Incomplete overlong title stays native-editable with explicit validation.");
        Assert(title.Updates.Count == 0 && ((Label)backend.Find("doc-00001-validation").Element).Text ==
            "Keep the document title within 160 UTF-16 units before analyzing.", "Title correction is requested without coercing native input.");
        Throws<ArgumentException>(() => body.Events.Change(new string('x', 65537)));
        Assert(editor.BodyInput.Text == "Updated content\n- item", "Rejected oversized native body does not silently replace the draft.");
        backend.Find("doc-00001-revert").Events.Click();
        Assert(editor.Draft == StudioCatalog.Original("doc-00001") && !editor.HasChanges && edits == 2,
            "Explicit revert restores generated sample with silent native programmatic updates.");
        host.Dispose();
        Assert(editor.Lifetime.Token.IsCancellationRequested && backend.Peers.All(peer => peer.Disposed) &&
            !body.Events.Change("late") && errors.Count == 0, "Editor lifetime and handlers retire explicitly.");
    }
}

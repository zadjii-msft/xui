using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed partial class WorkspaceStudio
{
    // Measured Android 1x chrome uses about 539 logical units; leave room for a 96-unit document body.
    public const float ShortViewportHeight = 640;
    private enum CompactPane { Catalog, Document, Details }
    private Host? owner;
    private StudioCatalogViewport? catalog;
    private IDisposable? viewportObservation;
    private WidthMode mode = WidthMode.Expanded;
    private CompactPane compactPane = CompactPane.Document;
    private Size? pendingSize;
    private CompactPane? pendingPane;
    private ThemeMode themeMode = ThemeMode.System;
    private bool retired;
    private Dictionary<string, StudioDocumentEditor> editors = new(StringComparer.Ordinal);
    private StudioOperationsDashboard? operationsPage;
    private IStudioOperationsService? operationsService;
    private Action<Exception>? reportUnhandled;
    private bool enableOperationsDrawer;
    private readonly List<Task> retiredOperations = [];
    private Host Owner => owner ?? throw new InvalidOperationException("Create the workspace with WorkspaceStudio.Create.");
    public StudioCatalogViewport Catalog => catalog ?? throw new InvalidOperationException("Workspace catalog is not initialized.");
    public WidthMode LayoutMode => mode;
    public StudioOperationsDashboard? OperationsPage { get { Owner.VerifyAccess(); return operationsPage; } }
    public Task LastOperation
    {
        get
        {
            Owner.VerifyAccess();
            return Task.WhenAll(retiredOperations.Append(Controller.LastOperation)
                .Append(operationsPage?.Controller.LastOperation ?? Task.CompletedTask));
        }
    }
    public WorkspaceStudioSession CaptureSession()
    {
        var session = Controller.CaptureSession();
        return session.Interrupted(session.AnalysisInterrupted || operationsPage?.Controller.State.Busy == true || !LastOperation.IsCompleted);
    }

    public static WorkspaceStudio Create(Host host, IStudioAnalysisService service, Action<Exception> reportUnhandled,
        WorkspaceStudioSession? session = null, IStudioOperationsService? operationsService = null,
        bool enableOperationsDrawer = false)
    {
        var controller = new WorkspaceStudioController(host, service, reportUnhandled, session);
        try
        {
            var app = new WorkspaceStudio(host, controller)
            {
                owner = host,
                operationsService = operationsService ?? new LocalStudioOperationsService(),
                reportUnhandled = reportUnhandled
            };
            app.enableOperationsDrawer = enableOperationsDrawer;
            app.Lifetime.Own(new Retirement(app));
            app.Sections = (
            [
                PageItem.Create(1, "Library", h => new StudioSectionPanel(h, "Document library", "Browse 10,000 local sample documents. Open a document to edit.")),
                PageItem.Create(2, "Drafts", h => new StudioSectionPanel(h, "Local drafts", "Closed tabs keep modified drafts here. Nothing is saved to disk.")),
                PageItem.Create(3, "Insights", h => new StudioSectionPanel(h, "Workspace insights", "Analyze the active document locally, with cancellation and no upload."))
            ], (ulong)controller.State.Session.Section + 1);
            app.catalog = new(host, app.CatalogScroll, app.CatalogView, controller, app.Lifetime, value => app.CatalogStatus = value);
            controller.Attach(app.Lifetime, app.RefreshWorkspace);
            app.SearchInput.InteractionChanged += _ => app.TryApplyPendingLayout();
            return app;
        }
        catch (Exception failure)
        {
            try { controller.Dispose(); }
            catch (Exception cleanup) { throw new AggregateException(failure, cleanup); }
            throw;
        }
    }

    public void AttachView()
    {
        Owner.VerifyMutation();
        if (viewportObservation is not null) throw new InvalidOperationException("Studio view is already attached.");
        ApplyTheme(themeMode);
        Catalog.Attach();
        try { viewportObservation = Owner.ObserveViewport(ApplyViewport); }
        catch (Exception error)
        {
            try { Owner.Detach(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    public void PrepareForAttachment()
    {
        Owner.VerifyMutation();
        if (Owner.IsAttached) throw new InvalidOperationException("Detach the backend before preparing a studio attachment.");
        viewportObservation = null;
        backViewportReady = false;
        pendingSize = null;
        pendingPane = null;
        Catalog.PrepareForAttachment();
        RefreshWorkspace(Controller.State);
        operationsPage?.PrepareForAttachment();
    }

    private void RefreshWorkspace(WorkspaceStudioState next)
    {
        string previousActive = Snapshot.Session.ActiveDocument;
        long previousOpenVersion = Snapshot.DocumentOpenVersion;
        var previousSection = Snapshot.Session.Section;
        var nextEditors = new Dictionary<string, StudioDocumentEditor>(StringComparer.Ordinal);
        StudioOperationsDashboard? nextOperations = null;
        retiredOperations.RemoveAll(task => task.IsCompletedSuccessfully);
        if (operationsPage is not null && !next.Session.OpenTabs.Contains(StudioTabs.OperationsKey))
            retiredOperations.Add(operationsPage.Controller.LastOperation);
        var items = next.Session.OpenTabs.Select(key => key == StudioTabs.OperationsKey
            ? PageItem.Create(StudioTabs.OperationsId, "Operations",
                host => nextOperations = StudioOperationsDashboard.Create(host, Controller, operationsService!, reportUnhandled!, mode, enableOperationsDrawer),
                dashboard => { nextOperations = dashboard; dashboard.AcceptWorkspace(next); })
            : PageItem.Create(
            StudioCatalog.Get(key).NativeId,
            TabTitle(next.Session, key),
            host =>
            {
                var editor = new StudioDocumentEditor(host, key, Controller)
                {
                    Draft = next.Session.Document(key),
                    HasChanges = next.Session.HasDraft(key),
                    Analyzing = next.Analyzing && next.Session.ActiveDocument == key,
                    ShortHeight = ShortHeight
                };
                nextEditors[key] = editor;
                return editor;
            },
            editor =>
            {
                nextEditors[key] = editor;
                editor.Draft = next.Session.Document(key);
                editor.HasChanges = next.Session.HasDraft(key);
                editor.Analyzing = next.Analyzing && next.Session.ActiveDocument == key;
                editor.ShortHeight = ShortHeight;
            })).ToArray();
        try
        {
            Documents = (items, next.Session.ActiveDocument == "" ? null : StudioTabs.NativeId(next.Session.ActiveDocument));
        }
        catch (KeyedUpdateException error) when (error.ModelCommitted)
        {
            Snapshot = next;
            throw;
        }
        editors = nextEditors;
        operationsPage = nextOperations;
        operationsPage?.ApplyWidthMode(mode);
        Sections = (Sections.Items, (ulong)next.Session.Section + 1);
        Snapshot = next;
        Catalog.UpdateSource(next);
        if (next.Session.ActiveDocument != "" &&
            (next.Session.ActiveDocument != previousActive || next.DocumentOpenVersion != previousOpenVersion))
            ShowCompactPane(CompactPane.Document);
        else if (next.Session.Section != previousSection)
            ShowCompactPane(next.Session.Section == StudioSection.Insights ? CompactPane.Details : CompactPane.Catalog);
        TryApplyPendingLayout();
    }
    private static string TabTitle(WorkspaceStudioSession session, string key)
    {
        return session.Document(key).DisplayTitle + (session.HasDraft(key) ? " *" : "");
    }

    private void ApplyViewport(Size size)
    {
        backViewportReady = size.Width > 0 && size.Height > 0;
        var next = WidthBreakpoints.Default.Select(size.Width);
        bool shortHeight = size.Height < ShortViewportHeight;
        if (next == mode && shortHeight == ShortHeight && pendingSize is null) return;
        if ((shortHeight && !ShortHeight && HasFocusedSecondaryCommand()) ||
            (!shortHeight && ShortHeight && ShortCancelButton.Visible && Owner.HasFocus(ShortCancelButton)))
        {
            pendingSize = size;
            LayoutPending = true;
            LayoutStatus = "Short-height layout deferred while a secondary command has native focus. Move focus, then apply the layout.";
            return;
        }
        if (next == WidthMode.Compact && compactPane != CompactPane.Catalog &&
            Owner.HasFocus(SearchInput))
        {
            pendingSize = size;
            LayoutPending = true;
            LayoutStatus = "Layout change deferred while library search has focus.";
            return;
        }
        try
        {
            ApplyHeightLayout(shortHeight);
            ApplyLayout(next, compactPane);
        }
        catch (KeyedUpdateException error) when (!error.ModelCommitted)
        {
            pendingSize = size;
            LayoutPending = true;
            LayoutStatus = "Layout change deferred by native editing. Finish editing, then apply the pending layout.";
            return;
        }
        pendingSize = null;
        LayoutPending = pendingPane is not null;
    }
    private bool HasFocusedSecondaryCommand()
    {
        if (DocumentsShown &&
            ((TabLeftButton.Visible && Owner.HasFocus(TabLeftButton)) ||
             (TabRightButton.Visible && Owner.HasFocus(TabRightButton)))) return true;
        if (DocumentsShown && editors.TryGetValue(Controller.State.Session.ActiveDocument, out var editor))
            return (editor.AnalyzeButton.Visible && Owner.HasFocus(editor.AnalyzeButton)) ||
                (editor.RevertButton.Visible && Owner.HasFocus(editor.RevertButton));
        return false;
    }
    private void ApplyHeightLayout(bool shortHeight)
    {
        if (shortHeight == ShortHeight) return;
        try { SectionPages.Visible = !shortHeight; }
        catch (KeyedUpdateException error) when (error.ModelCommitted)
        {
            CommitHeightLayout(shortHeight);
            throw;
        }
        CommitHeightLayout(shortHeight);
    }
    private void CommitHeightLayout(bool shortHeight)
    {
        ShortHeight = shortHeight;
        foreach (var editor in editors.Values) editor.ShortHeight = shortHeight;
    }
    private void TryApplyPendingLayout()
    {
        if (retired || !Owner.IsAttached) return;
        if (pendingSize is { } size) ApplyViewport(size);
        if (pendingSize is null && pendingPane is { } pane) ShowCompactPane(pane);
    }
    private void ShowCompactPane(CompactPane pane)
    {
        if (mode != WidthMode.Compact)
        {
            ApplyLayout(mode, pane);
            pendingPane = null;
            LayoutPending = pendingSize is not null;
            return;
        }
        if (pane != CompactPane.Catalog && Owner.HasFocus(SearchInput))
        {
            if (SearchInput.Interaction is not { IsComposing: false })
            {
                DeferPane(pane, "Navigation deferred while library search is editing. Finish native composition, then apply the layout.");
                return;
            }
            if (!Owner.TryFocus(ThemeButton)) throw new InvalidOperationException("Cannot move focus away from the hidden search pane.");
        }
        try { ApplyLayout(mode, pane); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted)
        {
            DeferPane(pane, "Navigation deferred by native editing. Finish the interaction, then apply the layout.");
            return;
        }
        pendingPane = null;
        LayoutPending = pendingSize is not null;
    }
    private void DeferPane(CompactPane pane, string message)
    {
        pendingPane = pane;
        LayoutPending = true;
        LayoutStatus = message;
    }
    private void ApplyLayout(WidthMode next, CompactPane pane)
    {
        bool catalogVisible = next != WidthMode.Compact || pane == CompactPane.Catalog;
        bool documentVisible = next != WidthMode.Compact || pane == CompactPane.Document;
        bool detailsVisible = next == WidthMode.Expanded || (pane == CompactPane.Details &&
            (next == WidthMode.Compact || ShortHeight));
        operationsPage?.ApplyWidthMode(next);
        try { DocumentPages.Visible = documentVisible; }
        catch (KeyedUpdateException error) when (error.ModelCommitted)
        {
            CommitLayout(next, pane, catalogVisible, documentVisible, detailsVisible);
            throw;
        }
        CommitLayout(next, pane, catalogVisible, documentVisible, detailsVisible);
    }
    private void CommitLayout(WidthMode next, CompactPane pane, bool catalogVisible, bool documentVisible, bool detailsVisible)
    {
        mode = next;
        compactPane = pane;
        CatalogShown = catalogVisible;
        DocumentsShown = documentVisible;
        DetailsShown = detailsVisible;
        NavigationExpanded = next == WidthMode.Expanded;
        Columns = next switch
        {
            WidthMode.Expanded =>
            [
                new(TrackSizing.Fixed, 184), new(TrackSizing.Fixed, 240),
                new(TrackSizing.Star, 1), new(TrackSizing.Fixed, 224)
            ],
            WidthMode.Medium =>
            [
                new(TrackSizing.Fixed, 48), new(TrackSizing.Fixed, 200),
                new(TrackSizing.Star, 1), new(TrackSizing.Fixed, detailsVisible ? 224 : 0)
            ],
            _ =>
            [
                new(TrackSizing.Fixed, 48),
                catalogVisible ? new(TrackSizing.Star, 1) : new(TrackSizing.Fixed, 0),
                documentVisible ? new(TrackSizing.Star, 1) : new(TrackSizing.Fixed, 0),
                detailsVisible ? new(TrackSizing.Star, 1) : new(TrackSizing.Fixed, 0)
            ]
        };
        LayoutStatus = next + " workspace / " + (next == WidthMode.Compact ? pane.ToString() : "retained editor panes") +
            (ShortHeight ? " / short-height chrome" : "");
    }

    private static string DocumentKey(ulong id) => StudioTabs.Key(id);
    private void SelectSection(ulong id)
    {
        if (id is < 1 or > 3) throw new ArgumentOutOfRangeException(nameof(id));
        Controller.Navigate((StudioSection)(id - 1));
        ShowCompactPane(id == 3 ? CompactPane.Details : CompactPane.Catalog);
    }
    private void ActivateSection(ulong id) => SelectSection(id);
    private void SelectDocument(ulong id) => Controller.ActivateTab(DocumentKey(id));
    private void ActivateDocument(ulong id)
    {
        if (Controller.State.Session.ActiveDocument != DocumentKey(id)) Controller.ActivateTab(DocumentKey(id));
        ShowCompactPane(CompactPane.Document);
        if (id == StudioTabs.OperationsId)
        {
            if (operationsPage is null)
                throw new InvalidOperationException("The operations dashboard is not available.");
            // While cancellation settles there may be no enabled content target; retain focus on the tab strip.
            var target = operationsPage.ActivationFocusTarget ?? DocumentTabs;
            if (!Owner.TryFocus(target))
                throw new InvalidOperationException("The operations dashboard could not receive native focus.");
            return;
        }
        if (!editors.TryGetValue(DocumentKey(id), out var editor) || !Owner.TryFocus(editor.BodyInput))
            throw new InvalidOperationException("The active document editor could not receive native focus.");
    }
    private void RequestClose(ulong id) => Controller.CloseTab(DocumentKey(id));
    private void SetQuery(string query) => Controller.SetQuery(query);
    private void AnalyzeActive() => Controller.Analyze();
    private void OpenOperations() => Controller.OpenOperations();
    private void CancelAnalysis() => Controller.CancelAnalysis();
    private void RevertActive()
    {
        var session = Controller.State.Session;
        if (session.ActiveDraft is null) throw new InvalidOperationException("Select a document to revert.");
        Controller.RevertDocument(session.ActiveDocument);
    }
    private void RetryLayout() => TryApplyPendingLayout();
    private void MoveTabLeft()
    {
        var state = Controller.State.Session;
        if (state.ActiveTabIndex > 0) Controller.MoveTab(state.ActiveDocument, state.ActiveTabIndex - 1);
    }
    private void MoveTabRight()
    {
        var state = Controller.State.Session;
        if (state.ActiveTabIndex >= 0 && state.ActiveTabIndex < state.OpenTabs.Length - 1)
            Controller.MoveTab(state.ActiveDocument, state.ActiveTabIndex + 1);
    }
    private void CycleTheme()
    {
        var next = themeMode switch { ThemeMode.System => ThemeMode.Light, ThemeMode.Light => ThemeMode.Dark, _ => ThemeMode.System };
        ApplyTheme(next);
    }
    private void ApplyTheme(ThemeMode next)
    {
        Owner.Theme = new(next, new ThemeResources(
            foreground: new ThemeColor(0x172033, 0xeff2f7),
            background: new ThemeColor(0xf4f7fb, 0x111827),
            accent: new ThemeColor(0x335de0, 0x88aaff)));
        themeMode = next;
        ThemeCaption = "Theme: " + next;
    }

    private sealed class Retirement(WorkspaceStudio app) : IDisposable
    {
        public void Dispose()
        {
            app.retired = true;
            app.viewportObservation = null;
            app.pendingSize = null;
            app.pendingPane = null;
            app.editors.Clear();
            app.operationsPage = null;
        }
    }
}

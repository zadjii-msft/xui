using System;
using System.Collections.Generic;
using System.Linq;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed partial class StudioOperationsDashboard
{
    private StudioOperationsReport? presentedReport;
    private Dictionary<string, StudioWorkloadRow> rows = new(StringComparer.Ordinal);
    private StudioOperationsMetrics? metrics;
    private bool? compactMetrics;
    private StudioOperationsControls? controls;
    private StudioOperationsDrawer? drawer;

    public SingleChoice ScopeInput => controls?.ScopeInput ?? throw new InvalidOperationException("Operations controls are not initialized.");
    public Reveal? ControlsReveal => drawer?.Drawer;
    internal Control? ActivationFocusTarget
    {
        get
        {
            if (drawer is not null && !drawer.Drawer.Open)
                return DrawerToggleButton.Visible && DrawerToggleButton.Enabled ? DrawerToggleButton : null;
            if (ScopeInput.Visible && ScopeInput.Enabled) return ScopeInput;
            var cancel = controls!.CancelButton;
            return cancel.Visible && cancel.Enabled ? cancel : null;
        }
    }

    public static StudioOperationsDashboard Create(Host host, WorkspaceStudioController workspace,
        IStudioOperationsService service, Action<Exception> reportUnhandled, WidthMode initialMode = WidthMode.Expanded,
        bool enableControlsDrawer = false)
    {
        var controller = new StudioOperationsController(host, workspace, service, reportUnhandled);
        try
        {
            var page = new StudioOperationsDashboard(host, controller);
            page.InitializeControls(enableControlsDrawer);
            controller.Attach(page.Lifetime, page.Refresh);
            page.ApplyWidthMode(initialMode);
            return page;
        }
        catch (Exception failure)
        {
            try { controller.Dispose(); }
            catch (Exception cleanup) { throw new AggregateException(failure, cleanup); }
            throw;
        }
    }
    private void InitializeControls(bool enableDrawer)
    {
        DrawerEnabled = enableDrawer;
        StudioOperationsControls CreateControls(Host host)
        {
            controls = new StudioOperationsControls(host, Controller);
            return controls;
        }
        Controls = enableDrawer
            ? [KeyedItem.Create("controls-drawer", host => drawer = new StudioOperationsDrawer(host, CreateControls))]
            : [KeyedItem.Create("controls", CreateControls)];
    }
    /// <summary>Requests native controls expansion. False keeps the current state and reports a close veto.</summary>
    /// <remarks>Requires explicit factory opt-in. Does not move focus, dismiss native UI, or run an animation clock.</remarks>
    public bool TrySetControlsOpen(bool open)
    {
        if (drawer is null) throw new InvalidOperationException("Native controls motion was not opted in for this workspace.");
        if (!open && Snapshot.Busy)
        {
            DrawerFeedback = "Finish or cancel the scan before hiding its controls.";
            return false;
        }
        try
        {
            if (!drawer.Drawer.TrySetOpen(open))
            {
                DrawerFeedback = "Controls remain open. Finish native editing or close the open choice popup, then try again.";
                return false;
            }
        }
        catch (KeyedUpdateException error) when (error.ModelCommitted)
        {
            DrawerOpen = drawer.Drawer.Open;
            throw;
        }
        DrawerOpen = drawer.Drawer.Open;
        DrawerFeedback = open ? "Scan controls expanded." : "Scan controls collapsed. Snapshot results are unchanged.";
        return true;
    }
    public void AcceptWorkspace(WorkspaceStudioState next) => Controller.UpdateWorkspace(next);
    // Grid placement is fixed: replace only this read-only group, never the dashboard's native inputs.
    public void ApplyWidthMode(WidthMode mode)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        bool compact = mode == WidthMode.Compact;
        if (compactMetrics == compact) return;
        var rowTracks = Enumerable.Range(0, compact ? 11 : 5)
            .Select(index => index % 2 == 0 ? new GridTrack(TrackSizing.Automatic, 1) : new GridTrack(TrackSizing.Fixed, 8))
            .ToArray();
        GridTrack[] columnTracks = compact
            ? [new(TrackSizing.Star, 1)]
            : [new(TrackSizing.Star, 1), new(TrackSizing.Fixed, 16), new(TrackSizing.Star, 1)];
        StudioOperationsMetrics? next = null;
        try
        {
            Metrics = [KeyedItem.Create(compact ? "compact-metrics" : "wide-metrics",
                host => next = new StudioOperationsMetrics(host, compact, rowTracks, columnTracks) { Report = Snapshot.Report },
                view => { next = view; view.Report = Snapshot.Report; })];
        }
        catch (KeyedUpdateException error) when (error.ModelCommitted)
        {
            metrics = next;
            compactMetrics = compact;
            throw;
        }
        metrics = next;
        compactMetrics = compact;
    }
    public void PrepareForAttachment()
    {
        presentedReport = null;
        Refresh(Controller.State);
    }
    private void Refresh(StudioOperationsState state)
    {
        if (!ReferenceEquals(state.Report, presentedReport))
        {
            var nextRows = new Dictionary<string, StudioWorkloadRow>(StringComparer.Ordinal);
            var descriptors = state.Report?.Workload.Select(item => KeyedItem.Create(item.Key,
                host =>
                {
                    var row = new StudioWorkloadRow(host, item.Key, Controller) { Item = item, Busy = state.Busy };
                    nextRows[item.Key] = row;
                    return row;
                },
                row => { nextRows[item.Key] = row; row.Item = item; row.Busy = state.Busy; })).ToArray() ?? [];
            try { Results = descriptors; }
            catch (KeyedUpdateException error) when (error.ModelCommitted)
            {
                rows.Clear();
                presentedReport = state.Report;
                Snapshot = state;
                throw;
            }
            rows = nextRows;
            presentedReport = state.Report;
        }
        foreach (var row in rows.Values) row.Busy = state.Busy;
        controls?.Refresh(state);
        if (metrics is not null) metrics.Report = state.Report;
        Snapshot = state;
    }
}

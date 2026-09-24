using System;
using System.Globalization;
using System.Linq;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed class StudioCatalogViewport : IDisposable
{
    public const float RowHeight = 128;
    private readonly Host host;
    private readonly ScrollView scroll;
    private readonly WorkspaceStudioController workspace;
    private readonly VirtualizationController<StudioCatalogRow> rows;
    private Action<string>? showStatus;
    private ISettledVirtualViewportLease? lease;
    private long catalogVersion;
    private float? restoreOffset;
    private bool disposed;

    public int MountedCount => rows.Mounted.Count;
    public float Offset => rows.Offset;
    public long SourceVersion => rows.SourceVersion;
    public bool IsReady { get; private set; }

    public StudioCatalogViewport(Host host, ScrollView scroll, KeyedStack container,
        WorkspaceStudioController workspace, ComponentLifetime owner, Action<string> showStatus)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(scroll);
        ArgumentNullException.ThrowIfNull(workspace);
        ArgumentNullException.ThrowIfNull(owner);
        ArgumentNullException.ThrowIfNull(showStatus);
        this.host = host;
        this.scroll = scroll;
        this.workspace = workspace;
        this.showStatus = showStatus;
        rows = new(host, container, RowHeight, 2,
            (rowHost, key) => new StudioCatalogRow(rowHost, key, workspace),
            UpdateRow);
        owner.Own(this);
        UpdateSource(workspace.State);
    }

    public void UpdateSource(WorkspaceStudioState state)
    {
        Verify();
        if (catalogVersion != state.CatalogVersion)
        {
            long version = rows.QueueKeys(workspace.VisibleKeys);
            catalogVersion = state.CatalogVersion;
            lease?.SetExtent(rows.RequestedCount, version);
        }
        foreach (var mounted in rows.Mounted) UpdateRow(mounted.Value, mounted.Key);
        ShowStatus();
    }
    private void UpdateRow(StudioCatalogRow row, string key)
    {
        var session = workspace.State.Session;
        row.Document = session.Document(key);
        row.Category = StudioCatalog.Get(key).Category.ToString();
        row.Changed = session.HasDraft(key);
        row.CanOpen = session.CanOpenDocument(key);
    }
    public void Attach()
    {
        Verify();
        if (!host.IsAttached) throw new InvalidOperationException("Attach the backend before binding the studio catalog.");
        if (lease is not null) throw new InvalidOperationException("Studio catalog already has a viewport attachment.");
        if (rows.Count != 0 || rows.Mounted.Count != 0)
            throw new InvalidOperationException("Prepare the catalog while detached before reattaching a backend.");
        var candidate = host.BeginVirtualViewport(scroll, rows.RequestedCount, RowHeight, rows.RequestedSourceVersion, OnRequested);
        if (candidate is not ISettledVirtualViewportLease settled)
        {
            candidate.Dispose();
            throw new NotSupportedException("Studio catalog requires native post-prune viewport completion.");
        }
        lease = settled;
        if (restoreOffset is float target) { restoreOffset = null; lease.RequestOffset(target); }
    }
    public void PrepareForAttachment()
    {
        Verify();
        if (host.IsAttached) throw new InvalidOperationException("Detach the backend before preparing studio catalog reattachment.");
        restoreOffset = rows.Offset;
        rows.PrepareForViewportAttachment();
        lease = null;
        IsReady = false;
        ShowStatus();
    }
    private void OnRequested(VirtualViewportRequest request)
    {
        Verify();
        var current = lease ?? throw new InvalidOperationException("Catalog viewport was not connected.");
        try
        {
            var begin = current.TryBeginUpdate(request.Epoch);
            if (!Enum.IsDefined(begin)) throw new InvalidOperationException("Unknown native viewport preparation result.");
            if (begin != VirtualViewportUpdateResult.Ready) { ShowStatus(); return; }
            foreach (var mounted in rows.Mounted.ToArray())
                rows.ObserveInteraction(mounted.Key, new(host.HasFocus(mounted.Value.OpenButton), false));
            if (rows.PrepareViewport(request) != VirtualizationUpdate.Applied)
            {
                rows.CancelPreparedViewport(request.Epoch);
                current.Cancel(request.Epoch);
                ShowStatus();
                return;
            }
            foreach (var mounted in rows.Mounted)
                host.SetVirtualItemInfo(mounted.Value.Root, rows.GetPreparedItemInfo(mounted.Key));
            if (current.TryCommit(request.Epoch) != VirtualViewportCommitResult.Committed)
                throw new InvalidOperationException("Native catalog viewport did not commit its reserved epoch.");
            if (!rows.CommitPreparedViewport(request.Epoch))
                throw new InvalidOperationException("Catalog model did not accept the committed native viewport.");
            IsReady = true;
            ShowStatus();
            current.FlushCommitted(request.Epoch);
        }
        catch (Exception error)
        {
            IsReady = false;
            lease = null;
            try { host.Detach(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }
    public void Reveal(string key)
    {
        Verify();
        var current = lease ?? throw new InvalidOperationException("The catalog viewport is not attached.");
        if (!rows.TryGetRevealOffset(key, rows.RequestedSourceVersion, rows.Offset, rows.ViewportHeight, out float target))
            throw new InvalidOperationException("The selected document is not in the current catalog projection.");
        current.RequestOffset(target);
    }
    private void ShowStatus() => showStatus?.Invoke(rows.Mounted.Count.ToString(CultureInfo.InvariantCulture) +
        " realized / " + rows.RequestedCount.ToString(CultureInfo.InvariantCulture) + " documents" +
        (rows.IsDeferred ? " / viewport pending" : ""));
    private void Verify()
    {
        host.VerifyMutation();
        ObjectDisposedException.ThrowIf(disposed, this);
    }
    public void Dispose()
    {
        if (disposed) return;
        host.VerifyAccess();
        disposed = true;
        showStatus = null;
        lease = null;
        rows.Dispose();
    }
}

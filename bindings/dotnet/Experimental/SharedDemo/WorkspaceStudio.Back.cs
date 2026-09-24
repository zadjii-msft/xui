using System;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed partial class WorkspaceStudio
{
    private bool backViewportReady;
    private bool backBlocked;
    private string backStatus = "";

    public bool BackBlocked { get { Owner.VerifyAccess(); return backBlocked; } }
    public string BackStatus { get { Owner.VerifyAccess(); return backStatus; } }

    /// <summary>Consumes an internal Back step, or an explicitly blocked step. False delegates Back to the host.</summary>
    /// <remarks>
    /// Call on the UI thread after native popup/IME handling. This never closes tabs, discards drafts,
    /// cancels work, or forces composition to end. Report exceptions and consume Back rather than falling through.
    /// Compact documents return to their section landing pane; Insights lands on Details, other sections on Catalog.
    /// Details and non-Library catalog sections return to Library. Medium/Expanded Back returns non-Library
    /// sections to Library. Only Library's root returns false. True with BackBlocked means retry after native
    /// editing or viewport readiness, not permission to close the host. Browser history entries remain host-owned.
    /// </remarks>
    public bool TryNavigateBack()
    {
        Owner.VerifyComponentMutation(Root);
        if (!Owner.IsAttached || viewportObservation is null)
            throw new InvalidOperationException("Attach the Studio view before requesting Back.");
        if (backBlocked && LayoutStatus == backStatus && !LayoutPending)
            LayoutStatus = mode + " workspace / " + (mode == WidthMode.Compact ? compactPane.ToString() : "retained editor panes");
        backBlocked = false;
        backStatus = "";
        BackNotice = false;
        if (!backViewportReady)
            return BlockBack("Back is waiting for the initial workspace layout. Try again when the view is ready.");
        if (LayoutPending)
            return BlockBack("Back is waiting for a pending layout change. Finish native editing and apply the layout first.");
        if (CatalogShown && Owner.HasFocus(SearchInput) && SearchInput.Interaction is not { IsComposing: false })
            return BlockBack("Back is waiting for the search editor. Finish native composition, then try Back again.");

        bool leavingPane = mode == WidthMode.Compact && compactPane != CompactPane.Catalog;
        if (!leavingPane && Controller.State.Session.Section == StudioSection.Library)
            return false;
        try
        {
            if (mode == WidthMode.Compact && compactPane == CompactPane.Document)
            {
                var destination = Controller.State.Session.Section == StudioSection.Insights
                    ? CompactPane.Details : CompactPane.Catalog;
                ApplyLayout(mode, destination);
            }
            else
            {
                Controller.Navigate(StudioSection.Library);
            }
        }
        catch (KeyedUpdateException error) when (!error.ModelCommitted)
        {
            return BlockBack("Back was blocked by native page readiness or editing. Finish the interaction, then try Back again.");
        }

        backStatus = "Back navigated within the workspace. Tabs and local drafts were kept.";
        if (leavingPane && !Owner.TryFocus(Navigation))
        {
            backStatus = "Back changed the workspace pane, but native navigation focus was not accepted.";
            BackNotice = true;
            LayoutStatus = backStatus;
            throw new InvalidOperationException(backStatus);
        }
        return true;
    }

    private bool BlockBack(string message)
    {
        backBlocked = true;
        BackNotice = true;
        backStatus = message;
        LayoutStatus = message;
        return true;
    }
}

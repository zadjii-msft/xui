namespace Xui.Experimental.Portable;

public readonly record struct TextInteraction(bool HasFocus, bool IsComposing);

public interface ITextInteractionEvents : IControlEvents
{
    bool InteractionChanged(TextInteraction interaction);
}

public readonly record struct VirtualViewportRect(float Offset, float Width, float Height, float Extent)
{
    public void Validate()
    {
        Values.Length(Offset);
        Values.Length(Width);
        Values.Length(Height);
        Values.Length(Extent);
    }
}

public readonly record struct VirtualViewportRequest(
    long Epoch, long CommittedSourceVersion, long RequestedSourceVersion,
    VirtualViewportRect Committed, VirtualViewportRect Requested, bool IsBlocked)
{
    public void Validate()
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(Epoch);
        ArgumentOutOfRangeException.ThrowIfNegative(CommittedSourceVersion);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(RequestedSourceVersion);
        if (RequestedSourceVersion < CommittedSourceVersion)
            throw new ArgumentOutOfRangeException(nameof(RequestedSourceVersion), "A source version cannot move backwards.");
        Committed.Validate();
        Requested.Validate();
    }
}

public enum VirtualViewportCommitResult { Committed, Superseded, Blocked }
public enum VirtualViewportUpdateResult { Ready, Superseded, Blocked }

/// <summary>
/// An experimental, explicitly enabled native viewport with precommit visibility control.
/// Native accessibility can observe valid intermediate removals/additions; global OS-tree
/// snapshot atomicity is not promised.
/// </summary>
public interface IVirtualViewportPeer : IElementPeer
{
    IVirtualViewportLease BeginVirtualViewport(
        int itemCount, float rowHeight, long sourceVersion, Action<VirtualViewportRequest> requested);
}

/// <summary>
/// Attachment-owned native viewport. Requests are immutable, UI-thread, coalesced snapshots;
/// callbacks must never run synchronously inside any lease operation.
/// </summary>
public interface IVirtualViewportLease : IDisposable
{
    /// <summary>Changes the requested logical extent, not the committed viewport or staged child measurement.</summary>
    void SetExtent(int itemCount, long sourceVersion);

    /// <summary>Requests a clamped, native-snapped offset without exposing the requested viewport.</summary>
    void RequestOffset(float offset);

    /// <summary>
    /// Reserves this epoch and holds logical offset and viewport-growth publication across
    /// synchronous UI-thread row staging. New native intent remains queued for the next epoch.
    /// Staging must not yield; native layout batching is used where available, not simulated
    /// as an atomic paint or operating-system accessibility transaction.
    /// </summary>
    VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch);

    /// <summary>
    /// Publishes exactly the reserved epoch and source. After destructive row staging this must
    /// commit or throw; an error requires host detachment, never a fabricated native rollback.
    /// </summary>
    VirtualViewportCommitResult TryCommit(long expectedEpoch);

    /// <summary>
    /// Abandons preparation before model commit without losing intent or automatically retrying
    /// until input/unblock. Once row staging commits, failure requires detachment instead.
    /// </summary>
    void Cancel(long expectedEpoch);
}

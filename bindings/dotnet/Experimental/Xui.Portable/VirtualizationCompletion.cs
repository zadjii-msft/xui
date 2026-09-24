namespace Xui.Experimental.Portable;

/// <summary>Optional synchronous completion of native geometry after committed rows are pruned.</summary>
public interface ISettledVirtualViewportLease : IVirtualViewportLease
{
    /// <summary>
    /// Completes pending native collection, measurement, arrangement, and row geometry for
    /// the current committed epoch. Requires a live lease with no reserved update.
    /// Newer requested intent remains queued, and authored callbacks must not run inline.
    /// This does not promise presentation at vsync or an atomic operating-system accessibility snapshot.
    /// </summary>
    void FlushCommitted(long expectedEpoch);
}

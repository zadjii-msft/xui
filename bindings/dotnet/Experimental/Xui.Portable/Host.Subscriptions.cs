namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    private int subscriptionCleanupDepth;
    private bool cleanupAllowsSurvivors;
    private ISet<Element>? cleanupElements;

    internal void VerifyEventRemoval(Element element)
    {
        VerifyThread();
        if (disposed || element.Disposed) return;
        if (subscriptionCleanupDepth != 0)
        {
            if (cleanupElements?.Contains(element) == true) return;
            if (cleanupAllowsSurvivors)
            {
                if (building is not null) VerifyBuildElement(element);
                return;
            }
            throw new InvalidOperationException("Candidate rollback cannot remove subscriptions from a surviving element.");
        }
        VerifyElementMutation(element);
    }
}

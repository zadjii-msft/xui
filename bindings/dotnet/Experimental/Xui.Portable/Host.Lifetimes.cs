namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    private readonly HashSet<Element> componentRoots = [];
    private readonly Dictionary<IDisposable, ComponentLifetime> ownedResources = new(ReferenceEqualityComparer.Instance);

    public ComponentLifetime GetComponentLifetime(Element componentRoot)
    {
        VerifyThread();
        ArgumentNullException.ThrowIfNull(componentRoot);
        if (componentRoot.Owner != this) throw new InvalidOperationException("The component belongs to another host.");
        if (componentRoot.ComponentLifetime is { } existing) return existing;
        VerifyComponent(componentRoot);
        if (!componentRoots.Contains(componentRoot))
            throw new InvalidOperationException("A lifetime requires a completed component root.");
        VerifyComponentMutation(componentRoot);
        var lifetime = new ComponentLifetime(this, componentRoot);
        componentRoot.ComponentLifetime = lifetime;
        return lifetime;
    }

    internal void RegisterOwnedResource(IDisposable resource, ComponentLifetime owner)
    {
        if (!ownedResources.TryAdd(resource, owner))
            throw new InvalidOperationException("This resource is already owned by a component in this host.");
    }

    internal void ReleaseOwnedResource(IDisposable resource) => ownedResources.Remove(resource);

    private void RetireComponents(IEnumerable<Element> retiring, List<Exception> failures, bool rollback = false)
    {
        var candidates = retiring.ToHashSet();
        var visited = new HashSet<Element>();
        void Visit(Element element)
        {
            if (!visited.Add(element)) return;
            foreach (var child in element.Children)
                if (candidates.Contains(child)) Visit(child);
            componentRoots.Remove(element);
            element.ComponentLifetime?.Retire(failures);
        }
        var previousElements = cleanupElements;
        bool previousAllowsSurvivors = cleanupAllowsSurvivors;
        subscriptionCleanupDepth++;
        cleanupElements = candidates;
        cleanupAllowsSurvivors = !rollback;
        try { foreach (var element in candidates) Visit(element); }
        finally
        {
            subscriptionCleanupDepth--;
            cleanupElements = previousElements;
            cleanupAllowsSurvivors = previousAllowsSurvivors;
        }
    }

    private void RollbackElements(int start, List<Exception> failures)
    {
        bool previous = transitioning;
        transitioning = true;
        try
        {
            var abandoned = elements.Skip(start).ToArray();
            RetireComponents(abandoned, failures, rollback: true);
            foreach (var element in abandoned.Reverse()) element.Release();
            elements.RemoveRange(start, elements.Count - start);
        }
        finally { transitioning = previous; }
    }

    private void UnwindBuildScopes(BuildScope? parent, List<Exception> failures)
    {
        while (building != parent && building is not null)
        {
            try { building.Dispose(); }
            catch (Exception error) { failures.Add(error); }
        }
    }
}

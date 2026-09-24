namespace Xui.Experimental.Portable;

public sealed class ComponentLifetime
{
    private readonly Host host;
    private readonly Element root;
    private readonly CancellationTokenSource cancellation = new();
    private readonly List<IDisposable> resources = [];
    private bool retired;

    public CancellationToken Token { get; }

    internal ComponentLifetime(Host host, Element root)
    {
        this.host = host;
        this.root = root;
        Token = cancellation.Token;
    }

    public T Own<T>(T resource) where T : IDisposable
    {
        host.VerifyAccess();
        ObjectDisposedException.ThrowIf(retired, this);
        host.VerifyComponentMutation(root);
        IDisposable owned = resource;
        ArgumentNullException.ThrowIfNull(owned);
        host.RegisterOwnedResource(owned, this);
        resources.Add(owned);
        return resource;
    }

    internal void Retire(List<Exception> failures)
    {
        if (retired) return;
        retired = true;
        try { cancellation.Cancel(); }
        catch (Exception error) { failures.Add(error); }
        finally { cancellation.Dispose(); }
        for (int i = resources.Count - 1; i >= 0; i--)
        {
            try { resources[i].Dispose(); }
            catch (Exception error) { failures.Add(error); }
            finally { host.ReleaseOwnedResource(resources[i]); }
        }
        resources.Clear();
    }
}

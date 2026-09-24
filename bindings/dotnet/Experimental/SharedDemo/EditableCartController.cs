using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed record EditableCartState
{
    public EditableCartDraft Draft { get; init; } = new();
    public CartRoute Route { get; init; } = new(CartPage.Browse, "");
    public string PageKey { get; init; } = "";
    public bool CanGoBack { get; init; }
    public bool Busy { get; init; }
    public bool CancelRequested { get; init; }
    public bool Interrupted { get; init; }
    public string Status { get; init; } = "Local catalog only. Nothing is ordered or charged.";
    public string Error { get; init; } = "";
    public string QuoteSummary { get; init; } = "";
}

public sealed class EditableCartController : IDisposable
{
    private readonly Host host;
    private readonly ICartQuoteService quotes;
    private readonly Action<Exception> reportUnhandled;
    private readonly NavigationStack<CartRoute> navigation;
    private readonly UiWorkScope work;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private EditableCartState state;
    private Action<EditableCartState>? render;
    private CancellationToken lifetime;
    private CancellationTokenSource? active;
    private volatile bool disposed;
    public Task LastOperation { get; private set; } = Task.CompletedTask;
    public EditableCartState State { get { Verify(); return state; } }

    public EditableCartController(Host host, ICartQuoteService quotes, Action<Exception> reportUnhandled,
        EditableCartSession? session = null)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(quotes);
        ArgumentNullException.ThrowIfNull(reportUnhandled);
        host.VerifyMutation();
        this.host = host;
        this.quotes = quotes;
        this.reportUnhandled = reportUnhandled;
        navigation = new(host);
        work = new(host);
        foreach (var route in session?.Routes ?? [new CartRoute(CartPage.Browse, "")])
            navigation.Push(route.Page.ToString(), route);
        state = new()
        {
            Draft = session?.Draft ?? new(),
            Route = navigation.Current!.State,
            PageKey = navigation.Current.Id.ToString("N"),
            CanGoBack = navigation.CanGoBack,
            Interrupted = session?.Interrupted ?? false,
            Status = session is null ? "Local catalog only. Nothing is ordered or charged." :
                session.Interrupted ? "Quote interrupted. Request a new local quote." : "Cart draft restored. No order was placed."
        };
    }
    public void Attach(ComponentLifetime owner, Action<EditableCartState> refresh)
    {
        Verify();
        ArgumentNullException.ThrowIfNull(owner);
        ArgumentNullException.ThrowIfNull(refresh);
        if (render is not null) throw new InvalidOperationException("Cart controller is already attached.");
        owner.Own(this);
        lifetime = owner.Token;
        render = refresh;
        render(state);
    }
    public EditableCartSession CaptureSession()
    {
        Verify();
        return new(1, state.Draft, [.. navigation.Entries.Select(entry => entry.State)],
            state.Interrupted || state.Busy || !LastOperation.IsCompleted);
    }
    public void OpenProduct(string productId)
    {
        VerifyIdle();
        if (state.Route.Page != CartPage.Browse) throw new InvalidOperationException("Open products from the catalog.");
        Push(new(CartPage.Detail, productId));
    }
    public void ShowCart()
    {
        VerifyIdle();
        if (state.Route.Page == CartPage.Edit) throw new InvalidOperationException("Already editing the cart.");
        Push(new(CartPage.Edit, ""));
    }
    private void Push(CartRoute route)
    {
        var entry = navigation.Push(route.Page.ToString(), route);
        try { Publish(state with { Route = route, PageKey = entry.Id.ToString("N"), CanGoBack = true }); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted) { navigation.Back(); throw; }
    }
    public void Back()
    {
        VerifyIdle();
        if (!navigation.CanGoBack) throw new InvalidOperationException("No previous cart page.");
        var entry = navigation.Entries[^2];
        try { Publish(state with { Route = entry.State, PageKey = entry.Id.ToString("N"), CanGoBack = navigation.Entries.Count > 2 }); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted) { throw; }
        catch { navigation.Back(); throw; }
        navigation.Back();
    }
    public void AddProduct(string id) => Edit(state.Draft.Add(id), "Item added. No order placed.");
    public void RemoveLine(string key) => Edit(state.Draft.Remove(key), "Item removed. Request a new quote.");
    public void ChangeQuantity(string key, string text) => Edit(state.Draft.ChangeQuantity(key, text), "Quantity edited. Request a new quote.");
    public void SetCustomerName(string text) => Edit(state.Draft.WithName(text), "Name edited. Request a new quote.");
    public void SetDiscount(string text) => Edit(state.Draft.WithDiscount(text), "Discount edited. Request a new quote.");
    public void Reverse() => Edit(state.Draft.Reverse(), "Cart order reversed. Request a new quote.");
    public void Sort() => Edit(state.Draft.Sort(), "Cart sorted by product. Request a new quote.");
    private void Edit(EditableCartDraft draft, string status)
    {
        VerifyIdle();
        Publish(state with { Draft = draft, Status = status, Error = "", QuoteSummary = "" });
    }
    public void Reset()
    {
        VerifyIdle();
        while (navigation.CanGoBack) Back();
        Edit(new(), "Cart cleared. No order was placed.");
    }
    public void Quote()
    {
        VerifyIdle();
        if (!state.Draft.CanQuote) throw new InvalidOperationException(state.Draft.Validation);
        if (render is null) throw new InvalidOperationException("Cart controller is not attached.");
        var draft = state.Draft;
        var request = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        active = request;
        try { Publish(state with { Busy = true, CancelRequested = false, Status = "Calculating local quote...", Error = "", QuoteSummary = "" }); }
        catch { active = null; request.Dispose(); throw; }
        Task<CartQuote>? production = null;
        var delivery = work.RunAsync(token => production = quotes.QuoteAsync(draft, token),
            quote =>
            {
                ArgumentNullException.ThrowIfNull(quote);
                if (quote != CartQuote.Calculate(draft)) throw new InvalidOperationException("Quote does not match the current local catalog.");
                active = null;
                try
                {
                    Publish(state with
                    {
                        Busy = false, CancelRequested = false, Interrupted = false,
                        Status = "Local quote ready. No order was placed.",
                        QuoteSummary = "Quote for " + draft.CustomerName + ": " + CartCatalog.Money(quote.Total) + "\nNo checkout or payment."
                    });
                }
                catch (Exception error) { reportUnhandled(error); throw; }
            }, request.Token);
        LastOperation = Observe(delivery, () => production, request);
    }
    public void Cancel()
    {
        Verify();
        if (active is null || !state.Busy || state.CancelRequested) throw new InvalidOperationException("No cancellable quote.");
        Publish(state with { CancelRequested = true, Status = "Cancel requested. Waiting for quote..." });
        active.Cancel();
    }
    private async Task Observe(Task delivery, Func<Task<CartQuote>?> production, CancellationTokenSource request)
    {
        Exception? failure = null;
        bool canceled = false;
        try
        {
            try { await delivery.ConfigureAwait(false); }
            catch (OperationCanceledException) when (request.IsCancellationRequested) { canceled = true; }
            catch (Exception error) { failure = error; }
            if (canceled && production() is { } pending)
            {
                try { await pending.ConfigureAwait(false); }
                catch (OperationCanceledException) when (request.IsCancellationRequested) { }
                catch (Exception error) { failure = error; }
            }
            if ((!canceled && failure is null) || disposed || lifetime.IsCancellationRequested) return;
            try
            {
                await host.DispatchAsync(() =>
                {
                    if (disposed || lifetime.IsCancellationRequested) return;
                    active = null;
                    Publish(state with
                    {
                        Busy = false, CancelRequested = false,
                        Status = failure is null ? "Quote canceled. Cart edits kept." : "Quote failed. Cart edits kept.",
                        Error = failure?.Message.Replace('\0', ' ') ?? ""
                    });
                }).ConfigureAwait(false);
            }
            catch (Exception error)
            {
                if (!disposed && !lifetime.IsCancellationRequested)
                    reportUnhandled(failure is null ? error : new AggregateException(failure, error));
            }
        }
        finally { Interlocked.CompareExchange(ref active, null, request); request.Dispose(); }
    }
    private void Publish(EditableCartState next)
    {
        var previous = state;
        state = next;
        try { render?.Invoke(next); }
        catch (KeyedUpdateException error) when (!error.ModelCommitted) { state = previous; throw; }
    }
    private void Verify() { host.VerifyMutation(); ObjectDisposedException.ThrowIf(disposed, this); }
    private void VerifyIdle()
    {
        Verify();
        if (state.Busy) throw new InvalidOperationException("Wait for the quote operation to settle.");
    }
    public void Dispose()
    {
        if (Environment.CurrentManagedThreadId != thread) throw new InvalidOperationException("Dispose cart on its UI thread.");
        if (disposed) return;
        disposed = true;
        render = null;
        var errors = new List<Exception>();
        try { active?.Cancel(); } catch (Exception error) { errors.Add(error); }
        foreach (var resource in new IDisposable[] { work, navigation })
            try { resource.Dispose(); } catch (Exception error) { errors.Add(error); }
        if (errors.Count != 0) throw new AggregateException("Cart cleanup failed.", errors);
    }
}

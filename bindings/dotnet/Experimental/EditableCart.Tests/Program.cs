using System.Globalization;
using System.Text.Json;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static int assertions;
    private static void Main()
    {
        ModelChecks();
        GeneratedChecks();
        QuoteChecks();
        FailureChecks();
        Console.WriteLine($"Editable cart: {assertions} assertions passed.");
    }
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T error) { assertions++; return error; }
        throw new InvalidOperationException("Expected " + typeof(T).Name);
    }
    private static void ModelChecks()
    {
        var empty = new EditableCartDraft();
        Assert(!empty.CanQuote && empty.Lines.IsEmpty && empty.TotalText == "Estimate: $0.00", "Empty cart is explicit.");
        var draft = empty.Add("coffee").Add("tea").Add("cocoa").WithName("  Ada  ").WithDiscount(" sAvE10 ")
            .ChangeQuantity("cart-line-1", "3").ChangeQuantity("cart-line-2", "2");
        Assert(draft.Subtotal == 63.50m && draft.Discount == 6.35m && draft.Total == 57.15m && draft.CanQuote,
            "Literal cart prices and discount match the preserved fixed catalog.");
        Assert(CartQuote.Calculate(draft) == new CartQuote(63.50m, 6.35m, 57.15m), "Local quote exact values.");
        Assert(draft.Reverse().Lines.Select(line => line.Id).SequenceEqual([3L, 2L, 1L]) &&
            draft.Sort().Lines.Select(line => line.Id).SequenceEqual([3L, 1L, 2L]), "Reverse and sort retain stable identities.");
        var replacement = draft.Remove("cart-line-2").Add("tea");
        Assert(replacement.Lines[^1].Id == 4 && replacement.NextId == 5 && draft.Lines[1].Id == 2, "Removed identities are not reused and snapshots immutable.");
        foreach (string invalid in new[] { "", " ", "0", "10", "-1", "+1", "1.5", "1e0", "99999999999999999999" })
        {
            var bad = draft.ChangeQuantity("cart-line-1", invalid);
            Assert(bad.Lines[0].QuantityText == invalid && !bad.CanQuote && bad.SubtotalText == "Subtotal: --" &&
                bad.TotalText == "Estimate: --", "Invalid quantity remains editable, never a fabricated zero total.");
            Throws<InvalidOperationException>(() => CartQuote.Calculate(bad));
        }
        Assert(draft.ChangeQuantity("cart-line-1", " 09 ").Lines[0].Total == 112.50m, "Permitted integer input is not normalized.");
        Assert(!draft.WithDiscount("INVALID").CanQuote && draft.WithDiscount("INVALID").TotalText == "Estimate: --", "Invalid coupon cannot produce an actionable quote.");
        Throws<ArgumentException>(() => draft.Add("unknown"));
        Throws<ArgumentException>(() => draft.Remove("missing"));
        Throws<ArgumentException>(() => draft.ChangeQuantity("cart-line-1", "bad\0"));
        Throws<ArgumentException>(() => _ = new EditableCartDraft([new(1, "coffee", "1"), new(1, "tea", "1")], 2, "", ""));
        Throws<ArgumentOutOfRangeException>(() => _ = new EditableCartDraft([new(2, "coffee", "1")], 2, "", ""));
        Throws<InvalidOperationException>(() => new EditableCartDraft([], long.MaxValue, "", "").Add("coffee"));
        var session = new EditableCartSession(1, draft.ChangeQuantity("cart-line-2", ""),
            [new(CartPage.Browse, ""), new(CartPage.Detail, "tea"), new(CartPage.Edit, "")], true);
        string json = EditableCartSessionCodec.Serialize(session);
        var restored = EditableCartSessionCodec.Restore(json);
        Assert(!JsonSerializer.IsReflectionEnabledByDefault && EditableCartSessionCodec.Serialize(restored) == json &&
            restored.Draft.Lines[1].QuantityText == "" && restored.Interrupted, "Reflection-free restoration preserves invalid drafts and logical routes.");
        Assert(!json.Contains("PageKey") && !json.Contains("Subtotal") && !json.Contains("QuoteSummary"), "Session contains data, not UI or ephemeral quote result.");
        foreach (string invalid in new[] { "null", "{}", "{", json.Replace("\"Version\":1", "\"Version\":2"),
            json.Replace("\"Version\":1", "\"Version\":1,\"Version\":1"), json.Replace("\"Interrupted\":true", "\"extra\":0,\"Interrupted\":true") })
            Throws<JsonException>(() => EditableCartSessionCodec.Restore(invalid));
        Throws<JsonException>(() => _ = new EditableCartSession(1, draft, [new(CartPage.Edit, "")], false));
        var culture = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR");
            Assert(draft.TotalText == "Estimate: $57.15" && draft.DiscountText == "Discount: -$6.35", "USD calculations are culture-invariant.");
        }
        finally { CultureInfo.CurrentCulture = culture; }
    }
    private static void GeneratedChecks()
    {
        using var h = new Harness();
        Assert(h.Quotes.Calls == 0 && h.Controller.State.Route.Page == CartPage.Browse, "Startup is a real browse page with no quote work.");
        int count = ApplicationScenarioRunner.Run(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "EditableCartScenarios.json")), "editable-cart", new Driver(h));
        assertions += count;
        Assert(count >= 35, "Cart shared corpus contains literal UI expectations.");
        Console.WriteLine($"Cart corpus: {count} literal expectations.");
        h.Controller.Reset();
        h.Controller.OpenProduct("coffee");
        h.Controller.AddProduct("coffee");
        h.Controller.ShowCart();
        var input = h.Backend.Find("cart-line-1-quantity");
        var editor = h.App.EditorPage!;
        var editorLifetime = editor.Lifetime;
        Assert(((TextInput)input.Element).CaptionVisible && input.Element.FixedSize is null && input.Element.PreferredSize is null,
            "Quantity editor uses natural captioned sizing.");
        input.Updates.Clear();
        Assert(input.Events.Change(" 3 "), "Native quantity edit accepted.");
        Assert(h.Controller.State.Draft.Lines[0].QuantityText == " 3 " && input.Updates.Count == 0, "Quantity edit keeps exact text and never echoes.");
        h.Controller.AddProduct("tea");
        h.Controller.AddProduct("cocoa");
        h.Controller.ChangeQuantity("cart-line-2", "2");
        h.Controller.Reverse();
        Assert(ReferenceEquals(input, h.Backend.Find("cart-line-1-quantity")) && input.Updates.Count == 0, "Add, other-row edit and reorder preserve focused input identity.");
        Assert(editor.RowsView.Children.Select(row => ((Control)row.Children[1]).AutomationId)
            .SequenceEqual(["cart-line-3-quantity", "cart-line-2-quantity", "cart-line-1-quantity"]), "Actual child order reversed.");
        h.Controller.Sort();
        Assert(editor.RowsView.Children.Select(row => ((Control)row.Children[1]).AutomationId)
            .SequenceEqual(["cart-line-3-quantity", "cart-line-1-quantity", "cart-line-2-quantity"]), "Actual child order sorted without replacing input.");
        h.Controller.RemoveLine("cart-line-2");
        Assert(ReferenceEquals(input, h.Backend.Find("cart-line-1-quantity")), "Removing another line retains editor.");
        h.Controller.AddProduct("coffee");
        Assert(h.Controller.State.Draft.Lines[^1].Id == 4 &&
            ReferenceEquals(input, h.Backend.Find("cart-line-1-quantity")), "Duplicate products get distinct editable stable-key lines.");
        h.Controller.RemoveLine("cart-line-4");
        var removed = h.Backend.Find("cart-line-3-remove");
        removed.Events.Click();
        Assert(removed.Disposed && !removed.Events.Click(), "Removing a row invalidates its own callback.");
        h.Controller.Back();
        Assert(editorLifetime.Token.IsCancellationRequested && input.Disposed && !input.Events.Change("retired"),
            "Navigation retires editor component without losing data.");
        Assert(h.Controller.State.Route == new CartRoute(CartPage.Detail, "coffee"), "Back from cart returns to product detail.");
        h.Controller.ShowCart();
        Assert(((TextInput)h.Backend.Find("cart-line-1-quantity").Element).Text == " 3 ", "Returning to edit restores data into a new view.");
        var session = h.Controller.CaptureSession();
        string oldKey = h.Controller.State.PageKey;
        h.Host.Dispose();
        using var restored = new Harness(EditableCartSessionCodec.Restore(EditableCartSessionCodec.Serialize(session)));
        Assert(restored.Controller.State.PageKey != oldKey && restored.Controller.State.Route.Page == CartPage.Edit &&
            restored.Controller.State.Draft.Lines[0].QuantityText == " 3 " && restored.Quotes.Calls == 0,
            "New host restores invalid/raw draft and route with fresh identities and no quote execution.");
        restored.Controller.Back();
        restored.Controller.Back();
        Assert(restored.Controller.State.Route.Page == CartPage.Browse && !restored.Controller.State.CanGoBack, "Restored route history behaves correctly.");
    }
    private static EditableCartSession ValidSession() => new(1,
        new EditableCartDraft().Add("coffee").Add("tea").WithName("Ada").WithDiscount("SAVE10"),
        [new(CartPage.Browse, ""), new(CartPage.Edit, "")], false);
    private static void QuoteChecks()
    {
        using var h = new Harness(ValidSession());
        var pending = new TaskCompletionSource<CartQuote>(TaskCreationOptions.RunContinuationsAsynchronously);
        h.Quotes.Produce = (_, _) => pending.Task;
        h.Controller.Quote();
        Assert(h.Controller.State.Busy && h.Controller.State.QuoteSummary == "", "Real async quote is busy, never fake success.");
        Assert(!h.Backend.Find("cart-line-1-quantity").Events.Change("9"), "Busy quote locks draft edits.");
        Throws<InvalidOperationException>(() => h.Controller.Back());
        var session = h.Controller.CaptureSession();
        Assert(session.Interrupted, "In-flight quote becomes an interruption marker.");
        h.Controller.Cancel();
        Assert(h.Quotes.Token.IsCancellationRequested && h.Controller.State.Busy && h.Controller.State.CancelRequested, "Cancel waits for producer acknowledgement.");
        pending.SetResult(new(20.50m, 2.05m, 18.45m));
        h.Finish();
        Assert(!h.Controller.State.Busy && h.Controller.State.Status == "Quote canceled. Cart edits kept." &&
            h.Controller.State.QuoteSummary == "", "Canceled late result cannot present a quote.");
        h.Quotes.Produce = (_, _) => Task.FromException<CartQuote>(new InvalidOperationException("Quote service unavailable."));
        h.Controller.Quote();
        h.Finish();
        Assert(h.Controller.State.Error == "Quote service unavailable." && h.Controller.State.Status == "Quote failed. Cart edits kept.", "Quote error visible without draft loss.");
        h.Quotes.Produce = (_, _) => Task.FromResult(new CartQuote(1, 0, 1));
        h.Controller.Quote(); h.Finish();
        Assert(h.Controller.State.Error == "Quote does not match the current local catalog.", "Invalid provider quote explicitly rejected.");
        h.Quotes.Produce = null;
        h.Controller.Quote(); h.Finish();
        Assert(h.Controller.State.QuoteSummary == "Quote for Ada: $18.45\nNo checkout or payment." &&
            h.Controller.State.Status == "Local quote ready. No order was placed.", "Successful quote exact literal output.");
        h.Controller.ChangeQuantity("cart-line-1", "2");
        Assert(h.Controller.State.QuoteSummary == "", "Every edit invalidates stale quote.");
        pending = new(TaskCreationOptions.RunContinuationsAsynchronously);
        h.Quotes.Produce = (_, _) => pending.Task;
        h.Controller.Quote();
        Task closing = h.Controller.LastOperation;
        var token = h.App.Lifetime.Token;
        h.Host.Dispose();
        pending.SetResult(new(33, 3.30m, 29.70m));
        h.Dispatcher.Complete(closing);
        Assert(token.IsCancellationRequested && h.Quotes.Token.IsCancellationRequested && h.Errors.Count == 0 &&
            h.Backend.Peers.All(peer => peer.Disposed), "Root disposal cancels and observes late work without UI resurrection.");
        using var restored = new Harness(session);
        Assert(!restored.Controller.State.Busy && restored.Controller.State.Status == "Quote interrupted. Request a new local quote." &&
            restored.Quotes.Calls == 0, "Interrupted session restores idle without fake quote retry.");
        var real = new LocalCartQuoteService();
        var quote = real.QuoteAsync(ValidSession().Draft, CancellationToken.None).GetAwaiter().GetResult();
        Assert(quote == new CartQuote(20.50m, 2.05m, 18.45m), "Real local asynchronous quote provider matches literals.");
        using var canceled = new CancellationTokenSource();
        canceled.Cancel();
        Throws<OperationCanceledException>(() => real.QuoteAsync(ValidSession().Draft, canceled.Token).GetAwaiter().GetResult());
    }
    private static void FailureChecks()
    {
        using var h = new Harness();
        h.Backend.RejectMutation = true;
        var state = h.Controller.State;
        var error = Throws<KeyedUpdateException>(() => h.Controller.OpenProduct("coffee"));
        Assert(!error.ModelCommitted && h.Controller.State == state && !h.Controller.State.CanGoBack, "Rejected detail navigation preserves state and history.");
        h.Backend.RejectMutation = false;
        h.Controller.OpenProduct("coffee"); h.Controller.AddProduct("coffee"); h.Controller.ShowCart();
        var input = h.Backend.Find("cart-line-1-quantity");
        h.Backend.RejectMutation = true;
        state = h.Controller.State;
        error = Throws<KeyedUpdateException>(() => h.Controller.AddProduct("tea"));
        Assert(!error.ModelCommitted && h.Controller.State == state && ReferenceEquals(input, h.Backend.Find("cart-line-1-quantity")),
            "Rejected nested keyed insertion preserves cart data and retained editor.");
        error = Throws<KeyedUpdateException>(() => h.Controller.Back());
        Assert(!error.ModelCommitted && h.Controller.State.Route.Page == CartPage.Edit, "Rejected Back keeps current cart route.");
        h.Backend.RejectMutation = false;
        h.Backend.FailInsert = true;
        error = Throws<KeyedUpdateException>(() => h.Controller.AddProduct("tea"));
        Assert(error.ModelCommitted && !h.Host.IsAttached && h.Controller.State.Draft.Lines.Length == 2 &&
            h.App.EditorPage!.Rows.Length == 2, "Postcommit nested failure keeps matching app and row data while detached.");
        var recovered = new Backend();
        h.Host.Attach(recovered);
        Assert(((TextInput)recovered.Find("cart-line-2-quantity").Element).Text == "1", "Reattach presents committed line after failure.");
        recovered.FailInsert = true;
        error = Throws<KeyedUpdateException>(() => h.Controller.Back());
        Assert(error.ModelCommitted && h.Controller.State.Route.Page == CartPage.Detail &&
            !h.Host.IsAttached && h.App.EditorPage is null, "Postcommit page replacement retains coherent route and releases retired editor reference.");
        recovered = new Backend();
        h.Host.Attach(recovered);
        Assert(((Label)recovered.Find("cart-product-name").Element).Text == "Coffee", "Reattach shows committed detail page.");
        h.Controller.ShowCart();
        h.Controller.RemoveLine("cart-line-1"); h.Controller.RemoveLine("cart-line-2");
        Assert(h.App.EditorPage!.RowsView.Children.Count == 0 &&
            ((Control)recovered.Find("cart-empty").Element).Visible, "Removing every line produces an actual empty keyed collection.");
        h.Controller.AddProduct("tea");
        Assert(h.Controller.State.Draft.Lines[0].Id == 3 && recovered.Exists("cart-line-3-quantity"), "Empty cart can add a new identity without recycling removed IDs.");
    }
}

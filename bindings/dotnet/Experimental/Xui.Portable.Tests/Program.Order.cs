using System.Globalization;
using System.Text.Json;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void OrderModelChecks()
    {
        var empty = new OrderState();
        Assert(empty == new OrderState() && !empty.Reviewing && !empty.CanReview, "Order starts as an immutable empty value.");
        Assert(empty.SubtotalText == "Subtotal: $0.00" && empty.DiscountText == "Discount: -$0.00" && empty.TotalText == "Total: $0.00", "Empty money text.");
        Assert(empty.Validation == "Choose at least one item. Enter your name. Enter a valid email address.", "Every initial validation problem is visible.");
        var order = empty with
        {
            CustomerName = "  Zo\u00eb \u674e  ",
            Email = " zo\u00eb@ex\u00e4mple.test ",
            DiscountCode = " sAvE10 ",
            CoffeeQuantity = 3,
            TeaQuantity = 2,
            CocoaQuantity = 1
        };
        Assert(empty.CustomerName == "" && empty.CoffeeQuantity == 0 && !empty.HasItems, "Record copies never mutate a saved snapshot.");
        Assert(order.CanReview && order.Subtotal == 63.50m && order.Discount == 6.35m && order.Total == 57.15m, "Literal full order totals.");
        Assert(order.CustomerName == "  Zo\u00eb \u674e  " && order.Email == " zo\u00eb@ex\u00e4mple.test " && order.DiscountCode == " sAvE10 ", "Validation never normalizes typed values.");
        Assert(order.Validation == "Ready to review. No payment will be taken.", "Valid order can review.");
        Assert(order.ReviewSummary == "", "No hidden stale summary in edit mode.");
        var reviewing = order with { Reviewing = true };
        Assert(!order.Reviewing && reviewing.Reviewing && reviewing.Total == order.Total, "Review changes no order data.");
        Assert(reviewing.Validation == "Review only: no order has been placed.", "Review is explicitly local.");
        Assert(reviewing.ReviewSummary == "Local review for   Zo\u00eb \u674e  \nEmail:  zo\u00eb@ex\u00e4mple.test \nCoffee: 3\nTea: 2\nCocoa: 1\nTotal: $57.15\nNo order placed.", "Review preserves details and uses invariant totals.");

        foreach (string invalid in new[] { "", " ", "ada", "ada@", "@example.test", "ada@localhost", "ada@@example.test",
            "ada @example.test", "ada@exam ple.test", "ada@.test", "ada@example.", "ada@example..test",
            "ada@-example.test", "ada@example-.test", "ada@exam_ple.test", ".ada@example.test", "ada.@example.test",
            "a..da@example.test", "<ada>@example.test", "Ada <ada@example.test>", "ada\nother@example.test" })
        {
            Assert(!(order with { Email = invalid }).EmailIsValid, $"Rejected invalid email '{invalid}'.");
        }
        foreach (string valid in new[] { "ada@example.test", "ada+order@example.test", "first.last@sub.example.test",
            "  ada@example.test  ", "zo\u00eb@ex\u00e4mple.test", "\u674e@example.test" })
        {
            Assert((order with { Email = valid }).EmailIsValid, $"Accepted supported email '{valid}'.");
        }
        foreach (string invalid in new[] { "", " \t " })
            Assert(!(order with { CustomerName = invalid }).NameIsValid, "Whitespace-only names are invalid.");
        foreach (string optional in new[] { "", "  ", "\t" })
        {
            var noCoupon = order with { DiscountCode = optional };
            Assert(noCoupon.CanReview && !noCoupon.HasDiscount && noCoupon.Discount == 0m && noCoupon.Total == 63.50m, "Whitespace coupon is optional, not rewritten.");
        }
        var badCoupon = order with { DiscountCode = " SAVE20 " };
        Assert(!badCoupon.CanReview && badCoupon.Discount == 0m && badCoupon.Total == 63.50m, "Invalid coupon grants no discount.");
        Assert(badCoupon.Validation == "Use SAVE10 or leave the discount code empty.", "Invalid coupon explains correction.");
        var cents = order with { CoffeeQuantity = 1, TeaQuantity = 1, CocoaQuantity = 0 };
        Assert(cents.Subtotal == 20.50m && cents.Discount == 2.05m && cents.Total == 18.45m, "Discount retains exact hundredths.");
        var maximum = order with { CoffeeQuantity = 9, TeaQuantity = 9, CocoaQuantity = 9 };
        Assert(maximum.Subtotal == 274.50m && maximum.Discount == 27.45m && maximum.Total == 247.05m, "Literal maximum order totals.");
        foreach (int invalid in new[] { -1, 10, int.MinValue, int.MaxValue })
        {
            Throws<ArgumentOutOfRangeException>(() => _ = order with { CoffeeQuantity = invalid });
            Throws<ArgumentOutOfRangeException>(() => _ = order with { TeaQuantity = invalid });
            Throws<ArgumentOutOfRangeException>(() => _ = order with { CocoaQuantity = invalid });
        }
        Throws<ArgumentNullException>(() => _ = order with { CustomerName = null! });
        Throws<ArgumentNullException>(() => _ = order with { Email = null! });
        Throws<ArgumentNullException>(() => _ = order with { DiscountCode = null! });
        Throws<ArgumentException>(() => _ = order with { CustomerName = "a\0b" });
        Throws<ArgumentException>(() => _ = order with { Email = "a\0b" });
        Throws<ArgumentException>(() => _ = order with { DiscountCode = "a\0b" });
        var culture = CultureInfo.CurrentCulture;
        try
        {
            foreach (string name in new[] { "fr-FR", "tr-TR", "ar-SA" })
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo(name);
                Assert(order.SubtotalText == "Subtotal: $63.50" && order.DiscountText == "Discount: -$6.35" && order.TotalText == "Total: $57.15", "USD formatting is culture-independent.");
                Assert(order.CoffeeQuantityText == "Coffee: 3" && order.HasDiscount, "Quantity and coupon matching are culture-independent.");
            }
        }
        finally { CultureInfo.CurrentCulture = culture; }
    }

    private static void OrderScenarioChecks()
    {
        using var host = new Host(new Dispatcher());
        var order = new OrderBuilder(host);
        var backend = new Backend();
        host.Attach(backend);
        var peers = backend.Peers.ToArray();
        string json = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "OrderScenarios.json"));
        int count = OrderScenarioRunner.Run(json, new OrderDriver(backend));
        Assert(count > 150, "The shared corpus checks all fixed-catalog behavior with literal expectations.");
        assertions += count;
        Assert(backend.Peers.SequenceEqual(peers), "All corpus scenarios retain the same peer objects.");
        Assert(order.State.Reviewing && order.State.Total == 57.15m, "Corpus finishes at the rich review checkpoint.");
        Assert(order.CustomerNameInput.AutomationId == "customer-name" && order.EmailInput.AutomationId == "email" &&
            order.DiscountInput.AutomationId == "discount-code", "Stable input refs and IDs.");
        var controls = backend.Peers.Select(p => p.Element).OfType<Control>().ToArray();
        Assert(controls.All(c => c.AutomationId.Length > 0) && controls.Select(c => c.AutomationId).Distinct().Count() == controls.Length, "Authored IDs are present and unique.");
        foreach (var input in new[] { order.CustomerNameInput, order.EmailInput, order.DiscountInput })
        {
            Assert(input.CaptionVisible && input.Placeholder.Length > 0 && input.Help.Length > 0, "All retained inputs have useful captions, placeholders, and help.");
            Assert(input.PreferredSize is null && input.FixedSize is null, "Order inputs leave room for native captions, editor padding, and scaled fonts.");
        }
        Console.WriteLine($"Order corpus: {count} literal property expectations passed.");
    }

    private static void OrderRetainedStateChecks()
    {
        using var host = new Host(new Dispatcher());
        var order = new OrderBuilder(host);
        var backend = new Backend();
        host.Attach(backend);
        var peers = backend.Peers.ToArray();
        var inputs = new[] { order.CustomerNameInput, order.EmailInput, order.DiscountInput };
        var inputPeers = inputs.Select(i => backend.Find(i.AutomationId)).ToArray();
        int changes = 0, submits = 0;
        foreach (var input in inputs)
        {
            input.Changed += _ => changes++;
            input.Submitted += () => submits++;
        }
        var driver = new OrderDriver(backend);
        driver.Change("customer-name", "  Zo\u00eb \u674e  ");
        driver.Change("email", "zoe@example.test");
        driver.Change("discount-code", " sAvE10 ");
        Assert(changes == 3 && submits == 0, "Each actual edit invokes one change and no submit.");
        Assert(inputPeers.All(p => p.Updates.Count == 0), "User edits are never written back into any native editor.");
        driver.Click("coffee-more");
        driver.Click("tea-more");
        driver.Click("cocoa-more");
        driver.Submit("email");
        Assert(order.State.Reviewing && submits == 1 && changes == 3, "Unrelated quantity and review changes preserve input events.");
        Assert(inputPeers.All(p => p.Updates.Count == 0), "Unrelated state changes do not reset editor selection or composition.");
        var saved = order.State;
        driver.Change("email", saved.Email);
        Assert(ReferenceEquals(saved, order.State) && order.State.Reviewing && changes == 3, "Identical native text echoes do not leave review.");

        foreach (string id in new[] { "coffee-less", "coffee-more", "tea-less", "tea-more", "cocoa-less", "cocoa-more" })
        {
            order.State = saved;
            driver.Click(id);
            Assert(!order.State.Reviewing && order.State != saved, $"Every quantity edit exits review: {id}.");
        }
        foreach ((string id, string value) in new[] { ("customer-name", "Grace"), ("email", "grace@example.test"), ("discount-code", "SAVE10") })
        {
            order.State = saved;
            driver.Change(id, value);
            Assert(!order.State.Reviewing, $"Every detail edit exits review: {id}.");
        }
        order.State = saved;
        int changesBefore = changes, submitsBefore = submits;
        foreach (var peer in inputPeers) peer.Updates.Clear();
        int[] echoes = inputPeers.Select(p => p.SuppressedEchoes).ToArray();
        driver.Click("reset");
        Assert(order.State == new OrderState() && inputs.All(i => i.Text == ""), "Reset restores the complete initial snapshot.");
        Assert(changes == changesBefore && submits == submitsBefore, "Reset is silent for all three native input handlers.");
        for (int i = 0; i < inputPeers.Length; i++)
        {
            Assert(inputPeers[i].Updates.SequenceEqual([ElementProperty.Text]), "Reset writes each populated input exactly once.");
            Assert(inputPeers[i].SuppressedEchoes == echoes[i] + 1, "Reset suppresses synchronous native setter echoes.");
        }
        Assert(!order.State.Reviewing && order.ReviewSummaryLabel.Text == "" && !order.ReviewSummaryLabel.Visible, "Silent reset cannot recreate a review.");
        Assert(backend.Peers.SequenceEqual(peers), "Reset keeps every native peer.");
        order.State = saved;
        Assert(order.State == saved && order.State.Reviewing && inputs[0].Text == saved.CustomerName &&
            inputs[1].Text == saved.Email && inputs[2].Text == saved.DiscountCode, "Public immutable state restores all fields and review.");
        Assert(changes == changesBefore && submits == submitsBefore, "State restoration is silent.");
        int updates = backend.Peers.Sum(p => p.Updates.Count);
        order.State = saved with { };
        Assert(backend.Peers.Sum(p => p.Updates.Count) == updates, "Equal snapshots cause no peer updates.");
        Assert(ReferenceEquals(inputs[0], order.CustomerNameInput) && ReferenceEquals(inputs[1], order.EmailInput) &&
            ReferenceEquals(inputs[2], order.DiscountInput), "State replacement keeps typed editor refs.");

        order.CustomerNameInput.Visible = false;
        Assert(!inputPeers[0].Events.Change("hidden") && !inputPeers[0].Events.Submit(), "Hidden editor callbacks are inert.");
        order.CustomerNameInput.Visible = true;
        var scroll = (ScrollView)backend.Find("order-content").Element;
        scroll.Enabled = false;
        Assert(!inputPeers[1].Events.Change("disabled") && !backend.Find("reset").Events.Click(), "Disabled ancestor blocks all order callbacks.");
        scroll.Enabled = true;
        Assert(order.State == saved, "Rejected events never mutate the order snapshot.");
        host.Detach();
        Assert(backend.Disposed && peers.All(p => p.Disposed), "Order attachment cleanup releases all peers.");
        Assert(!inputPeers[0].Events.Change("stale") && !inputPeers[1].Events.Submit() && !backend.Find("reset").Events.Click(), "Detached events are stale.");
        var replacement = new Backend();
        host.Attach(replacement);
        Assert(order.State == saved && ((TextInput)replacement.Find("email").Element).Text == saved.Email, "Reattachment preserves the snapshot.");
        Assert(!inputPeers[0].Events.Submit() && !backend.Find("coffee-more").Events.Click(), "Old event sinks remain stale after reattachment.");
        new OrderDriver(replacement).Click("edit");
        Assert(!order.State.Reviewing && order.State with { Reviewing = true } == saved, "Live replacement events reach only the current order.");
        host.Dispose();
        Assert(!replacement.Find("reset").Events.Click() && replacement.Peers.All(p => p.Disposed), "Disposal removes every order handler.");
        Throws<ObjectDisposedException>(() => order.State = new());
    }

    private static void OrderRunnerChecks()
    {
        using var host = new Host(new Dispatcher());
        _ = new OrderBuilder(host);
        var backend = new Backend();
        host.Attach(backend);
        var driver = new OrderDriver(backend);
        const string start = """{"version":1,"scenarios":[{"name":"runner","steps":[{"action":"click","id":"reset"},""";
        const string end = "]}]}";
        foreach (string badStep in new[]
        {
            """{"action":"expect","id":"total","text":"wrong"}""",
            """{"action":"expect","id":"review","enabled":true}""",
            """{"action":"expect","id":"review-summary","visible":true}""",
            """{"action":"expect","id":"total"}""",
            """{"action":"expect","id":"total","text":null}""",
            """{"action":"expect","id":"total","text":"Total: $0.00","ignored":true}""",
            """{"action":"expect","id":"total","text":"Total: $0.00","text":"Total: $0.00"}""",
            """{"action":"change","id":"email","value":null}""",
            """{"action":"change","id":"email","value":0}""",
            """{"action":"click","id":"unknown"}""",
            """{"action":"click","id":"email"}""",
            """{"action":"skip","id":"reset"}""",
            """{"action":"submit","id":""}"""
        })
        {
            var error = Throws<InvalidOperationException>(() => OrderScenarioRunner.Run(start + badStep + end, driver));
            Assert(error.Message.StartsWith("Order scenario 'runner', step 2:", StringComparison.Ordinal) && error.InnerException is not null, "Runner preserves driver/schema failure with scenario and step context.");
        }
        var mismatch = Throws<InvalidOperationException>(() => OrderScenarioRunner.Run(start + """{"action":"expect","id":"total","text":"wrong"}""" + end, driver));
        Assert(mismatch.Message.Contains("'total' text: expected 'wrong', got 'Total: $0.00'."), "Expectation failure contains ID and literal expected/actual values.");
        foreach (string badCorpus in new[]
        {
            """{"version":2,"scenarios":[]}""",
            """{"version":1,"scenarios":[]}""",
            """{"version":1,"scenarios":[{"name":"missing-reset","steps":[{"action":"click","id":"review"}]}]}""",
            """{"version":1,"scenarios":[{"name":"empty","steps":[]}]}""",
            """{"version":1,"scenarios":[{"name":"same","steps":[{"action":"click","id":"reset"}]},{"name":"same","steps":[{"action":"click","id":"reset"}]}]}"""
        })
            Throws<InvalidOperationException>(() => OrderScenarioRunner.Run(badCorpus, driver));
        Throws<JsonException>(() => OrderScenarioRunner.Run("{", driver));
        Throws<ArgumentNullException>(() => OrderScenarioRunner.Run("{}", null!));
        int count = OrderScenarioRunner.Run(start + """{"action":"expect","id":"customer-name","text":"","enabled":true,"visible":true}""" + end, driver);
        Assert(count == 3, "Runner counts each checked property, not only steps.");
    }

    private sealed class OrderDriver(Backend backend) : IOrderScenarioDriver
    {
        public void Change(string id, string value) => backend.Find(id).Events.Change(value);
        public void Click(string id) => backend.Find(id).Events.Click();
        public void Submit(string id) => backend.Find(id).Events.Submit();
        public string Text(string id) => backend.Find(id).Element switch
        {
            TextInput input => input.Text,
            Control control => control.Name,
            _ => throw new InvalidOperationException($"'{id}' is not a text-bearing control.")
        };
        public bool Enabled(string id) => ((Control)backend.Find(id).Element).Enabled;
        public bool Visible(string id) => ((Control)backend.Find(id).Element).Visible;
    }
}

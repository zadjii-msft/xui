using Android.App;
using Android.OS;
using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;
using NativeButton = Android.Widget.Button;

namespace Xui.Experimental.AndroidOrderDeviceTests;

[Activity(Label = "XUI Android order checks", MainLauncher = true, Exported = true,
    Theme = "@android:style/Theme.Material.Light.NoActionBar",
    WindowSoftInputMode = SoftInput.AdjustResize)]
public sealed partial class TestActivity : Activity
{
    private int assertions;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        RunTests();
    }

    private void Assert(bool condition, string description)
    {
        if (!condition) throw new InvalidOperationException(description);
        assertions++;
    }

    private async void RunTests()
    {
        try
        {
            if (Intent?.GetBooleanExtra("single-choice-only", false) == true)
            {
                using var choiceSurface = new OrderSurface(this);
                SetContentView(choiceSurface);
                await SingleChoiceChecks(choiceSurface, new AndroidDispatcher());
                string choiceResult = $"PASS: {assertions} Android native SingleChoice assertions.";
                Log.Info("Xui.Android.Orders", choiceResult);
                SetContentView(new TextView(this) { Text = choiceResult });
                return;
            }
            if (Intent?.GetBooleanExtra("pages-only", false) == true)
            {
                using var pagesSurface = new OrderSurface(this);
                SetContentView(pagesSurface);
                await PagesChecks(pagesSurface, new AndroidDispatcher());
                string pagesResult = $"PASS: {assertions} Android retained page assertions.";
                Log.Info("Xui.Android.Orders", pagesResult);
                SetContentView(new TextView(this) { Text = pagesResult });
                return;
            }
            if (Intent?.GetBooleanExtra("viewport-label-only", false) == true)
            {
                using var viewportSurface = new OrderSurface(this);
                SetContentView(viewportSurface);
                await ViewportLabelChecks(viewportSurface, new AndroidDispatcher());
                string viewportResult = $"PASS: {assertions} Android viewport/label assertions.";
                Log.Info("Xui.Android.Orders", viewportResult);
                SetContentView(new TextView(this) { Text = viewportResult });
                return;
            }
            if (Intent?.GetBooleanExtra("studio-row-only", false) == true)
            {
                StudioRowGeometryChecks(new AndroidDispatcher());
                string rowResult = $"PASS: {assertions} Android Studio row geometry assertions.";
                Log.Info("Xui.Android.Orders", rowResult);
                SetContentView(new TextView(this) { Text = rowResult });
                return;
            }
            if (Intent?.GetBooleanExtra("presentation-only", false) == true)
            {
                using var presentationSurface = new OrderSurface(this);
                SetContentView(presentationSurface);
                await PresentationChecks(presentationSurface, new AndroidDispatcher());
                string presentationResult = $"PASS: {assertions} Android presentation assertions.";
                Log.Info("Xui.Android.Orders", presentationResult);
                SetContentView(new TextView(this) { Text = presentationResult });
                return;
            }
            if (Intent?.GetBooleanExtra("forms-only", false) == true)
            {
                using var formsSurface = new OrderSurface(this);
                SetContentView(formsSurface);
                await FormsChecks(formsSurface, new AndroidDispatcher());
                string formsResult = $"PASS: {assertions} Android Forms assertions.";
                Log.Info("Xui.Android.Orders", formsResult);
                SetContentView(new TextView(this) { Text = formsResult });
                return;
            }
#if DEBUG
            Log.Info("Xui.Android.Orders", "Native fixture build: Debug.");
#else
            Log.Info("Xui.Android.Orders", "Native fixture build: Release.");
#endif
            if (Intent?.GetBooleanExtra("layout-only", false) == true)
            {
                using var layoutSurface = new OrderSurface(this);
                SetContentView(layoutSurface);
                AdvancedLayoutChecks(layoutSurface, new AndroidDispatcher());
                string layoutResult = $"PASS: {assertions} Android native layout assertions.";
                Log.Info("Xui.Android.Orders", layoutResult);
                SetContentView(new TextView(this) { Text = layoutResult });
                return;
            }
            bool performanceOnly = Intent?.GetBooleanExtra("perf-only", false) == true;
            if (performanceOnly || Intent?.GetBooleanExtra("virtual-only", false) == true)
            {
                using var virtualSurface = new OrderSurface(this);
                SetContentView(virtualSurface);
                if (performanceOnly) await VirtualPerformanceChecks(virtualSurface, new AndroidDispatcher());
                else await VirtualViewportChecks(virtualSurface, new AndroidDispatcher());
                string virtualResult = $"PASS: {assertions} Android virtual viewport assertions.";
                Log.Info("Xui.Android.Orders", virtualResult);
                SetContentView(new TextView(this) { Text = virtualResult });
                return;
            }
            await ServiceChecks();
            InputMeasurementChecks();
            using var surface = new OrderSurface(this);
            SetContentView(surface);
            var dispatcher = new AndroidDispatcher();
            using var host = new Host(dispatcher);
            var order = new OrderBuilder(host);
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var driver = new NativeDriver(backend);
            using var stream = typeof(TestActivity).Assembly.GetManifestResourceStream("OrderScenarios.json")
                ?? throw new InvalidOperationException("The shared order scenario resource is missing.");
            using var reader = new StreamReader(stream);
            int sharedAssertions = OrderScenarioRunner.Run(reader.ReadToEnd(), driver);

            driver.Click("reset");
            var empty = order.State;
            int inactiveEvents = 0;
            order.ReviewButton.Click += () => inactiveEvents++;
            order.EditButton.Click += () => inactiveEvents++;
            Assert(!driver.Enabled("review") && !driver.Enabled("edit"), "Empty orders disable Review and Edit.");
            driver.Click("review");
            driver.Click("edit");
            Assert(inactiveEvents == 0 && order.State == empty, "Disabled native buttons cannot invoke authored callbacks.");
            order.CoffeeMore.Visible = false;
            driver.Click("coffee-more");
            Assert(!driver.Visible("coffee-more") && order.State == empty, "Hidden native buttons cannot change state.");
            order.CoffeeMore.Visible = true;

            var inputs = new (string Id, TextInput Model, string Value)[]
            {
                ("customer-name", order.CustomerNameInput, "Ada"),
                ("email", order.EmailInput, "ada@example.com"),
                ("discount-code", order.DiscountInput, "SAVE10")
            };
            int changes = 0;
            foreach (var field in inputs) field.Model.Changed += _ => changes++;
            foreach (var field in inputs) driver.Change(field.Id, field.Value);
            Assert(changes == inputs.Length, "Each native editor change reaches authored C# once.");

            foreach (var field in inputs)
            {
                var input = (EditText)backend.FindViews(field.Id).Single();
                input.RequestFocus();
                input.SetSelection(1, 2);
                using var editable = input.EditableText!;
                BaseInputConnection.SetComposingSpans(editable);
                int start = BaseInputConnection.GetComposingSpanStart(editable);
                int end = BaseInputConnection.GetComposingSpanEnd(editable);
                Assert(start >= 0 && end > start, $"{field.Id} has a native composing range.");
                driver.Click("coffee-more");
                Assert(ReferenceEquals(input, backend.FindViews(field.Id).Single()) &&
                    input.Text == field.Value && input.HasFocus &&
                    input.SelectionStart == 1 && input.SelectionEnd == 2,
                    $"{field.Id} retains native identity, text, focus and selection during a quantity update.");
                Assert(BaseInputConnection.GetComposingSpanStart(editable) == start &&
                    BaseInputConnection.GetComposingSpanEnd(editable) == end,
                    $"{field.Id} retains composition during a quantity update.");
                BaseInputConnection.RemoveComposingSpans(editable);
            }
            Assert(changes == inputs.Length, "Unrelated quantity updates do not reassign native input text.");
            driver.Click("reset");
            Assert(changes == inputs.Length && inputs.All(field => driver.Text(field.Id) == ""),
                "Reset clears every retained editor without native change echoes.");

            var state = order.State;
            order.EmailInput.Submitted += () => inactiveEvents++;
            order.DiscountInput.Submitted += () => inactiveEvents++;
            order.EmailInput.Enabled = false;
            driver.Submit("email");
            Assert(inactiveEvents == 0 && order.State == state, "A disabled editor cannot invoke authored submit callbacks.");
            order.EmailInput.Enabled = true;
            order.DiscountInput.Visible = false;
            driver.Submit("discount-code");
            Assert(inactiveEvents == 0 && order.State == state, "A hidden editor cannot invoke authored submit callbacks.");
            order.DiscountInput.Visible = true;

            await Task.Run(async () => await host.DispatchAsync(() => driver.Click("tea-more")));
            Assert(order.State.TeaQuantity == 1 && dispatcher.CheckAccess(), "Native order dispatch returns to the UI thread.");
            var oldButton = (NativeButton)backend.FindViews("tea-more").Single();
            var oldInput = backend.FindViews("email").Single();
            var saved = order.State;
            host.Detach();
            Assert(surface.ChildCount == 0, "Detach unmounts the order tree.");
            bool disposed = false;
            try { oldButton.PerformClick(); }
            catch (ObjectDisposedException) { disposed = true; }
            Assert(disposed || order.State == saved, "Old native order widgets cannot reach detached handlers.");
            var replacement = new AndroidBackend(surface, dispatcher);
            host.Attach(replacement);
            Assert(order.State == saved && !ReferenceEquals(oldInput, replacement.FindViews("email").Single()),
                "Reattachment retains authored state and creates new editors.");
            new NativeDriver(replacement).Click("tea-more");
            Assert(order.State.TeaQuantity == 2, "One native click after reattachment produces one change.");
            host.Dispose();
            Assert(surface.ChildCount == 0, "Disposal releases the order surface.");
            await LifetimeStress(surface, dispatcher);
            GalleryChecks(surface, dispatcher);
            DynamicTaskChecks(surface, dispatcher);
            ValueControlChecks(surface, dispatcher);
            await FormsChecks(surface, dispatcher);
            await PresentationChecks(surface, dispatcher);
            await PagesChecks(surface, dispatcher);
            await ViewportLabelChecks(surface, dispatcher);
            await SingleChoiceChecks(surface, dispatcher);
            StudioRowGeometryChecks(dispatcher);
            await DynamicChecks(surface, dispatcher);
            InputApiChecks(surface, dispatcher);
            await InteractionChecks(surface, dispatcher);
            AdvancedLayoutChecks(surface, dispatcher);
            await ProfileChecks(surface, dispatcher);
            await VirtualViewportChecks(surface, dispatcher);

            string result = $"PASS: {sharedAssertions} shared order expectations; {assertions} Android order assertions.";
            Log.Info("Xui.Android.Orders", result);
            SetContentView(new TextView(this) { Text = result });
        }
        catch (Exception error)
        {
            Log.Error("Xui.Android.Orders", $"FAIL: {error}");
            throw;
        }
    }

    private void InputMeasurementChecks()
    {
        foreach (float fontScale in new[] { 1f, 2f })
        {
            using var configuration = new global::Android.Content.Res.Configuration(Resources!.Configuration!);
            configuration.FontScale = fontScale;
            using var scaledContext = CreateConfigurationContext(configuration)
                ?? throw new InvalidOperationException("Cannot create the font-scale test context.");
            using var context = new ContextThemeWrapper(scaledContext, global::Android.Resource.Style.ThemeMaterialLightNoActionBar);
            Assert(Math.Abs(context.Resources!.Configuration!.FontScale - fontScale) < 0.01f,
                "The native measurement fixture uses the requested font scale.");
            foreach (bool greeting in new[] { false, true })
            {
                using var surface = new OrderSurface(context);
                var dispatcher = new AndroidDispatcher();
                using var host = new Host(dispatcher);
                string[] ids;
                if (greeting)
                {
                    _ = new Greeting(host) { Entry = "Agjpqy" };
                    ids = ["name"];
                }
                else
                {
                    _ = new OrderBuilder(host)
                    {
                        State = new OrderState { CustomerName = "Agjpqy", Email = "agjpqy@example.test", DiscountCode = "SAVE10" }
                    };
                    ids = ["customer-name", "email", "discount-code"];
                }
                var backend = new AndroidBackend(surface, dispatcher);
                host.Attach(backend);
                float density = context.Resources!.DisplayMetrics!.Density;
                int width = (int)Math.Round(320 * density);
                int height = (int)Math.Round(480 * density);
                surface.Measure(View.MeasureSpec.MakeMeasureSpec(width, MeasureSpecMode.Exactly),
                    View.MeasureSpec.MakeMeasureSpec(height, MeasureSpecMode.Exactly));
                surface.Layout(0, 0, width, height);
                foreach (string id in ids)
                {
                    var input = (EditText)backend.FindViews(id).Single();
                    var wrapper = (ViewGroup)input.Parent!;
                    var caption = (TextView)wrapper.GetChildAt(0)!;
                    var layout = input.Layout ?? throw new InvalidOperationException($"'{id}' has no native text layout.");
                    int required = layout.Height + input.CompoundPaddingTop + input.CompoundPaddingBottom;
                    Log.Info("Xui.Android.Orders",
                        $"Input geometry: {id}, fontScale={fontScale}, editor={input.Height}, required={required}, caption={caption.Height}, outer={wrapper.Height}.");
                    Assert(input.Height >= required,
                        $"'{id}' clips native text at font scale {fontScale}: editor {input.Height}px requires {required}px including native padding.");
                    Assert(input.Top >= caption.Bottom && input.Bottom <= wrapper.Height,
                        $"'{id}' keeps its caption and complete editor within the allocated element.");
                }
            }
        }
    }

    private async Task LifetimeStress(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        const int hostCount = 12;
        const int attachmentsPerHost = 8;
        int totalPeers = 0;
        int totalViews = 0;
        int totalClicks = 0;
        int totalChanges = 0;
        for (int generation = 0; generation < hostCount; generation++)
        {
            using var host = new Host(dispatcher);
            var order = new OrderBuilder(host);
            int clicks = 0;
            int changes = 0;
            order.CoffeeMore.Click += () => clicks++;
            order.CustomerNameInput.Changed += _ => changes++;
            var retiredButtons = new List<IControlEvents>();
            for (int attachment = 0; attachment < attachmentsPerHost; attachment++)
            {
                order.State = new OrderState();
                var backend = new CountedBackend(surface, dispatcher, order.CoffeeMore, order.CustomerNameInput);
                host.Attach(backend);
                var views = CaptureViews(surface.GetChildAt(0)!);
                Assert(surface.ChildCount == 1 && views.All(view => view.Handle != IntPtr.Zero),
                    "Each stress attachment mounts one tree with live native handles.");
                Assert(retiredButtons.All(events => !events.Click()),
                    "Previously retired event generations remain inactive after reattachment.");
                var input = (EditText)backend.Native.FindViews("customer-name").Single();
                input.Text = $"Customer {generation}-{attachment}";
                ((NativeButton)backend.Native.FindViews("coffee-more").Single()).PerformClick();
                Assert(changes == attachment + 1 && clicks == attachment + 1 && order.State.CoffeeQuantity == 1,
                    "Every stress attachment delivers exactly one change and one click without listener multiplication.");
                var saved = order.State;
                host.Detach();
                Assert(surface.ChildCount == 0 && backend.BackendDisposed &&
                    backend.DisposalOrder.SequenceEqual(Enumerable.Range(0, backend.Created).Reverse()),
                    "Stress teardown unmounts before disposing every peer once in reverse creation order.");
                Assert(views.All(view => view.Handle == IntPtr.Zero),
                    "Stress teardown releases every captured native view handle.");
                Assert(!backend.ButtonEvents.Click() && !backend.InputEvents.Change("late") &&
                    !backend.InputEvents.Submit() && order.State == saved &&
                    clicks == attachment + 1 && changes == attachment + 1,
                    "Retired native event sinks cannot mutate the model or reach authored callbacks.");
                host.Detach();
                Assert(backend.DisposalOrder.Count == backend.Created && surface.ChildCount == 0,
                    "Repeated detach does not dispose native resources twice.");
                retiredButtons.Add(backend.ButtonEvents);
                totalPeers += backend.Created;
                totalViews += views.Count;
            }
            bool ran = false;
            Task queued = host.DispatchAsync(() => ran = true);
            host.Dispose();
            bool faulted = false;
            try { await queued; }
            catch (ObjectDisposedException) { faulted = true; }
            Assert(faulted && !ran, "Accepted work queued before terminal disposal faults without running.");
            host.Dispose();
            Assert(surface.ChildCount == 0 && retiredButtons.All(events => !events.Click()),
                "Terminal disposal is idempotent and leaves all old generations inactive.");
            totalClicks += clicks;
            totalChanges += changes;
        }
        Log.Info("Xui.Android.Orders",
            $"Lifetime stress: {hostCount} hosts, {hostCount * attachmentsPerHost} attachments, " +
            $"{totalPeers} peers and {totalViews} native view handles released, " +
            $"{totalClicks} clicks, {totalChanges} changes, {hostCount} queued tasks faulted.");
    }

    private sealed class CountedBackend : IBackend
    {
        private readonly OrderSurface surface;
        private readonly Element button;
        private readonly Element input;
        private IControlEvents? buttonEvents;
        private IControlEvents? inputEvents;
        internal AndroidBackend Native { get; }
        internal int Created { get; private set; }
        internal bool BackendDisposed { get; private set; }
        internal List<int> DisposalOrder { get; } = [];
        internal IControlEvents ButtonEvents => buttonEvents ?? throw new InvalidOperationException("Button sink was not created.");
        internal IControlEvents InputEvents => inputEvents ?? throw new InvalidOperationException("Input sink was not created.");

        internal CountedBackend(OrderSurface surface, AndroidDispatcher dispatcher, Element button, Element input)
        {
            this.surface = surface;
            this.button = button;
            this.input = input;
            Native = new AndroidBackend(surface, dispatcher);
        }

        public IElementPeer Create(Element element, IControlEvents events)
        {
            if (element == button) buttonEvents = events;
            if (element == input) inputEvents = events;
            return new CountedPeer(this, Native.Create(element, events), Created++);
        }

        public void Mount(IElementPeer root) => Native.Mount(((CountedPeer)root).Inner);

        public void Dispose()
        {
            Native.Dispose();
            BackendDisposed = true;
        }

        private sealed class CountedPeer(CountedBackend owner, IElementPeer inner, int index) : IElementPeer
        {
            internal IElementPeer Inner => inner;
            public void AddChild(IElementPeer child) => inner.AddChild(((CountedPeer)child).Inner);
            public void Update(ElementProperty property) => inner.Update(property);
            public void Dispose()
            {
                if (!owner.BackendDisposed || owner.surface.ChildCount != 0)
                    throw new InvalidOperationException("A peer was released before its backend unmounted.");
                inner.Dispose();
                owner.DisposalOrder.Add(index);
            }
        }
    }

    private static List<View> CaptureViews(View root)
    {
        var views = new List<View>();
        void Visit(View view)
        {
            views.Add(view);
            if (view is ViewGroup group)
                for (int child = 0; child < group.ChildCount; child++) Visit(group.GetChildAt(child)!);
        }
        Visit(root);
        return views;
    }

    private sealed class NativeDriver(AndroidBackend backend) : IOrderScenarioDriver, IApplicationScenarioDriver
    {
        private View Find(string id) => backend.FindViews(id).Single();

        public void Change(string id, string value)
        {
            var input = (EditText)Find(id);
            if (!input.Enabled || !Visible(id))
                throw new InvalidOperationException($"Cannot edit inactive native input '{id}'.");
            input.Text = value;
        }

        public void Click(string id) => ((NativeButton)Find(id)).PerformClick();
        public void Submit(string id) => ((EditText)Find(id)).OnEditorAction(ImeAction.Done);
        public string Text(string id) => ((TextView)Find(id)).Text ?? "";
        public bool Enabled(string id) => Find(id).Enabled;

        public bool Visible(string id)
        {
            var matches = backend.FindViews(id);
            if (matches.Count == 0) return false;
            for (View? view = matches.Single(); view is not null; view = view.Parent as View)
                if (view.Visibility != ViewStates.Visible) return false;
            return true;
        }
    }
}

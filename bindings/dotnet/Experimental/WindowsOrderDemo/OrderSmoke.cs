using System.ComponentModel;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Text;
using PortableDemo;
using Xui;

internal sealed class OrderSmoke : IOrderScenarioDriver
{
    private readonly Application application;
    private readonly Window window;
    private readonly OrderBuilder demo;
    private readonly Dictionary<string, Control> controls;
    private readonly Dictionary<string, nint> peers = new(StringComparer.Ordinal);
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(10);

    private OrderSmoke(Application application, Window window, OrderBuilder demo)
    {
        this.application = application;
        this.window = window;
        this.demo = demo;
        controls = new(StringComparer.Ordinal)
        {
            ["customer-name"] = demo.CustomerNameInput, ["email"] = demo.EmailInput,
            ["discount-code"] = demo.DiscountInput,
            ["coffee-less"] = demo.CoffeeLess, ["coffee-more"] = demo.CoffeeMore,
            ["coffee-quantity"] = demo.CoffeeQuantityLabel,
            ["tea-less"] = demo.TeaLess, ["tea-more"] = demo.TeaMore,
            ["tea-quantity"] = demo.TeaQuantityLabel,
            ["cocoa-less"] = demo.CocoaLess, ["cocoa-more"] = demo.CocoaMore,
            ["cocoa-quantity"] = demo.CocoaQuantityLabel,
            ["subtotal"] = demo.SubtotalLabel, ["discount-total"] = demo.DiscountLabel,
            ["total"] = demo.TotalLabel, ["validation"] = demo.ValidationLabel,
            ["review-summary"] = demo.ReviewSummaryLabel,
            ["review"] = demo.ReviewButton, ["edit"] = demo.EditButton, ["reset"] = demo.ResetButton
        };
    }

    internal static void Run()
    {
        using var application = new Application();
        using var window = application.CreateWindow("XUI order conformance", 560, 760);
        window.SetShowActivated(false);
        var demo = new OrderBuilder(window);
        var driver = new OrderSmoke(application, window, demo);
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("OrderScenarios.json")
            ?? throw new InvalidOperationException("The shared order scenarios are missing.");
        using var reader = new StreamReader(stream);
        string json = reader.ReadToEnd();
        application.Show(window);
        var work = Task.Run(() =>
        {
            try
            {
                driver.CapturePeers();
                int assertions = OrderScenarioRunner.Run(json, driver);
                assertions += driver.LayoutAndKeyboard();
                assertions += driver.InputRetention();
                driver.Ui(() => Check(window.CallbackStatus == 0, "A native callback failed."));
                return assertions;
            }
            finally
            {
                driver.Ui(() => { if (window.State == WindowState.Open) window.Close(); });
            }
        });
        Exception? loopError = null;
        try { application.Run(); }
        catch (Exception error) { loopError = error; }
        int count;
        try { count = work.GetAwaiter().GetResult(); }
        catch (Exception error)
        {
            if (loopError is not null) throw new AggregateException(loopError, error);
            throw;
        }
        if (loopError is not null) ExceptionDispatchInfo.Capture(loopError).Throw();
        Check(window.State == WindowState.Closed, "The smoke window did not close.");
        Check(driver.peers.Values.All(peer => !IsWindow(peer)), "A native order peer survived window closure.");
        Console.WriteLine($"Windows order shared scenarios and native input: {count} assertions passed.");
    }

    private T Ui<T>(Func<T> action)
    {
        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!application.Post(() =>
        {
            try { completion.SetResult(action()); }
            catch (Exception error) { completion.SetException(error); }
        })) throw new InvalidOperationException("The native UI dispatcher rejected an order test action.");
        return completion.Task.WaitAsync(Timeout).GetAwaiter().GetResult();
    }

    private void Ui(Action action) => Ui(() => { action(); return true; });
    private void WaitForUi(Func<bool> condition, string message)
    {
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
        while (!Ui(condition))
        {
            if (elapsed.Elapsed >= Timeout) throw new TimeoutException(message);
            Thread.Sleep(10);
        }
    }
    private Control Control(string id) => controls.TryGetValue(id, out var control)
        ? control : throw new ArgumentException($"Unknown order control '{id}'.", nameof(id));
    private TextInput Input(string id) => Control(id) as TextInput
        ?? throw new ArgumentException($"'{id}' is not an order input.", nameof(id));

    private void CapturePeers()
    {
        foreach (string id in new[] { "customer-name", "email", "discount-code" })
        {
            Ui(() =>
            {
                var input = Input(id);
                input.Focus();
                nint peer = GetFocus();
                Check(peer != 0 && input.Focused && ClassName(peer) == "Edit", $"Native input '{id}' did not receive focus.");
                peers.Add(id, peer);
            });
        }
        Ui(() =>
        {
            nint root = GetAncestor(peers["customer-name"], 2);
            foreach (var (id, control) in controls)
            {
                if (control is not Button) continue;
                var matches = new List<nint>();
                EnumWindow callback = (peer, _) =>
                {
                    var caption = new StringBuilder(256);
                    GetWindowTextW(peer, caption, caption.Capacity);
                    if (ClassName(peer) == "Xui.Control.1" && caption.ToString() == control.Text) matches.Add(peer);
                    return true;
                };
                if (!EnumChildWindows(root, callback, 0)) throw new Win32Exception(Marshal.GetLastWin32Error());
                Check(matches.Count == 1, $"Expected one native peer for '{id}', found {matches.Count}.");
                peers.Add(id, matches[0]);
            }
        });
    }

    public void Change(string id, string value) => Ui(() =>
    {
        var input = Input(id);
        input.Focus();
        Check(GetFocus() == peers[id], $"Input '{id}' lost its native peer.");
        Check(SendTextW(peers[id], 0x000c, 0, value) != 0, $"Native text replacement failed for '{id}'.");
        Check(input.Text == value && window.CallbackStatus == 0, $"Native change failed for '{id}'.");
    });

    public void Click(string id)
    {
        Ui(() =>
        {
            if (Control(id) is not Button) throw new ArgumentException($"'{id}' is not an order button.");
            if (IsWindowEnabled(peers[id])) Control(id).Focus();
        });
        Ui(() =>
        {
            if (!GetClientRect(peers[id], out var bounds)) throw new Win32Exception(Marshal.GetLastWin32Error());
            int point = ((Math.Max(1, bounds.Bottom) / 2) << 16) | (Math.Max(1, bounds.Right) / 2);
            SendMessageW(peers[id], 0x0201, 1, point);
            SendMessageW(peers[id], 0x0202, 0, point);
            Check(window.CallbackStatus == 0, $"Native click failed for '{id}'.");
        });
    }

    public void Submit(string id)
    {
        var submitted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void Complete() => submitted.TrySetResult();
        Ui(() =>
        {
            var input = Input(id);
            input.Focus();
            input.Submitted += Complete;
        });
        try
        {
            Ui(() =>
            {
                if (!PostMessageW(peers[id], 0x0100, 0x0d, 1))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            });
            submitted.Task.WaitAsync(Timeout).GetAwaiter().GetResult();
        }
        finally { Ui(() => Input(id).Submitted -= Complete); }
    }

    public string Text(string id) => Ui(() => Control(id).Text);
    public bool Enabled(string id) => Ui(() => IsWindowEnabled(peers[id]));
    public bool Visible(string id) => Ui(() => VisibleOnUi(Control(id)));

    private int LayoutAndKeyboard()
    {
        var focused = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void Complete() => focused.TrySetResult();
        Ui(() =>
        {
            demo.CustomerNameInput.Focus();
            demo.EmailInput.FocusEntered += Complete;
        });
        try
        {
            Ui(() =>
            {
                if (!PostMessageW(peers["customer-name"], 0x0100, 0x09, 1))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            });
            focused.Task.WaitAsync(Timeout).GetAwaiter().GetResult();
            Ui(() => Check(demo.EmailInput.Focused && GetFocus() == peers["email"],
                "Native Tab did not move from customer name to email."));
        }
        finally { Ui(() => demo.EmailInput.FocusEntered -= Complete); }

        Ui(() =>
        {
            nint root = GetAncestor(peers["customer-name"], 2);
            if (!SetWindowPos(root, 0, 0, 0, 320, 420, 0x0016) || !UpdateWindow(root))
                throw new Win32Exception(Marshal.GetLastWin32Error());
        });
        // WM_SIZE schedules layout separately from painting; wait for the new allocation.
        WaitForUi(() =>
        {
            nint root = GetAncestor(peers["customer-name"], 2);
            if (!GetClientRect(root, out var client)) throw new Win32Exception(Marshal.GetLastWin32Error());
            uint dpi = GetDpiForWindow(root);
            return dpi > 0 && Math.Abs(demo.Root.GetBounds().Width - client.Right * 96f / dpi) < 0.1f;
        }, "The native order layout did not adopt the resized client width.");
        Ui(() =>
        {
            nint root = GetAncestor(peers["customer-name"], 2);
            if (!GetClientRect(root, out var client)) throw new Win32Exception(Marshal.GetLastWin32Error());
            uint dpi = GetDpiForWindow(root);
            Check(dpi > 0 && client.Right <= 320 && client.Bottom <= 420, "The narrow native viewport was not applied.");
            float width = client.Right * 96f / dpi;
            foreach (var (id, control) in controls)
            {
                var bounds = control.GetBounds();
                Check(bounds.Width >= 0 && bounds.Width <= width + 1,
                    $"Order control '{id}' width {bounds.Width} exceeded the narrow viewport {width}.");
            }
            Check(demo.ReviewSummaryLabel.GetBounds().Height > 0 && VisibleOnUi(demo.ReviewSummaryLabel),
                "The reviewed summary lost its native layout after resize.");
        });
        Click("edit");
        Ui(() => Check(!demo.State.Reviewing && !VisibleOnUi(demo.ReviewSummaryLabel),
            "The narrow scroll region did not reveal and activate Edit."));
        return 4;
    }

    private static bool VisibleOnUi(Control control)
    {
        var value = new FeatureValue { Size = (uint)Marshal.SizeOf<FeatureValue>(), Version = 0x00010001 };
        int status = FeatureGet(control.Id, 37, ref value);
        if (status != 0) throw new XuiException(status, "Cannot read native control visibility.");
        return value.First != 0;
    }

    private int InputRetention()
    {
        Click("reset");
        Change("customer-name", "Ada \u03bb \ud83d\ude00");
        Change("email", "ada@example.com");
        Change("discount-code", "save10");
        int assertions = 0;
        Ui(() =>
        {
            foreach (string id in new[] { "customer-name", "email", "discount-code" })
            {
                var input = Input(id);
                string text = input.Text;
                input.Focus();
                input.Selection = new TextSelection(1, 3);
                int changes = 0;
                void Changed(string _) => changes++;
                input.Changed += Changed;
                try
                {
                    demo.State = demo.State with { CocoaQuantity = demo.State.CocoaQuantity + 1 };
                    Check(GetFocus() == peers[id] && input.Selection == new TextSelection(1, 3) &&
                        input.Text == text && changes == 0, $"Unrelated order update disturbed '{id}'.");
                    assertions++;
                    demo.ResetButton.Invoke();
                    Check(input.Text == "" && changes == 0 && GetFocus() == peers[id] &&
                        input.Selection == new TextSelection(0, 0), $"Programmatic reset disturbed or echoed '{id}'.");
                    assertions++;
                    demo.State = new OrderState { CustomerName = "Ada \u03bb \ud83d\ude00", Email = "ada@example.com", DiscountCode = "save10" };
                }
                finally { input.Changed -= Changed; }
            }
            Check(peers.Values.All(IsWindow), "An order state update replaced native widgets.");
            assertions++;
        });
        return assertions;
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static string ClassName(nint peer)
    {
        var name = new StringBuilder(256);
        if (GetClassNameW(peer, name, name.Capacity) == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
        return name.ToString();
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct FeatureValue
    {
        public uint Size, Version;
        public double A, B, C, D;
        public ulong First, Second;
        public nint TextData;
        public uint TextLength, TextReserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }
    private delegate bool EnumWindow(nint window, nint context);
    [DllImport("xui", EntryPoint = "xui_feature_get", CallingConvention = CallingConvention.Cdecl)]
    private static extern int FeatureGet(ulong control, uint property, ref FeatureValue value);
    [DllImport("user32.dll", ExactSpelling = true)] private static extern nint GetFocus();
    [DllImport("user32.dll", ExactSpelling = true)] private static extern nint GetAncestor(nint window, uint flags);
    [DllImport("user32.dll", ExactSpelling = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindow(nint window);
    [DllImport("user32.dll", ExactSpelling = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowEnabled(nint window);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetClientRect(nint window, out Rect rectangle);
    [DllImport("user32.dll", ExactSpelling = true)] private static extern uint GetDpiForWindow(nint window);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowPos(nint window, nint after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UpdateWindow(nint window);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumChildWindows(nint window, EnumWindow callback, nint context);
    [DllImport("user32.dll", ExactSpelling = true, CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int GetClassNameW(nint window, StringBuilder name, int length);
    [DllImport("user32.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(nint window, StringBuilder text, int length);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern nint SendTextW(nint window, uint message, nuint first, string text);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint SendMessageW(nint window, uint message, nuint first, nint second);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessageW(nint window, uint message, nuint first, nint second);
}

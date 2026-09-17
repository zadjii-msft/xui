using ValueControls.Tests;
using Xui;

internal static class Program
{
    private static int assertions;

    [STAThread]
    private static int Main()
    {
        try
        {
            using var window = new Window();
            var component = new Values(window, new(100, 200));
            Assert(component.Input.Range == new NumericRange(100, 200) && component.Input.Value == 150 &&
                component.Meter.Value == 150, "Generated range initialization precedes value application.");
            Assert(component.DefaultInput.Range == new NumericRange(0, 100) && component.DefaultInput.Value == 0 &&
                component.DefaultProgress.Range == new NumericRange(0, 100) && component.DefaultProgress.Value == 0 &&
                component.DefaultProgress.State == ProgressState.Determinate, "Native defaults survive omitted arguments.");
            Assert(component.ClampedInput.Value == 100 && component.ClampedProgress.Value == 100,
                "No synthetic zero overrides the native initial range clamp.");
            Assert(component.__xuiDesignerNodeCount == 7 &&
                ReferenceEquals(component.__xuiDesignerElement(1), component.Input) &&
                ReferenceEquals(component.__xuiDesignerElement(2), component.Meter), "Opt-in mapping returns actual generated native elements.");
            component.Input.ChangeValue(175);
            Assert(component.Calls == 1 && component.Current == 175 && component.Meter.Value == 175,
                "Committed native change calls the double handler once and updates progress.");
            component.Current = 180;
            component.Direction = Axis.Vertical;
            component.Reverse = true;
            component.Status = ProgressState.Paused;
            Assert(component.Input.Value == 180 && component.Meter.Value == 180 && component.Meter.State == ProgressState.Paused &&
                component.Calls == 1, "Reactive setters remain callback-silent.");
            component.Input.ChangeValue(185);
            Assert(component.Calls == 2 && component.Meter.Value == 185, "Reactive refresh does not duplicate native subscriptions.");
            foreach (double invalid in new[] { double.NaN, double.PositiveInfinity, 99, 201 })
            {
                Fails(() => component.Input.Value = invalid);
                Fails(() => component.Meter.Value = invalid);
                Assert(component.Input.Value == 185 && component.Meter.Value == 185, "Rejected values retain prior native values.");
            }
            foreach (var invalid in new[]
            {
                new NumericRange(100, 100), new NumericRange(200, 100), new NumericRange(double.NaN, 200),
                new NumericRange(-double.MaxValue, double.MaxValue), new NumericRange(0, 100, 0),
                new NumericRange(0, 100, 1, double.PositiveInfinity)
            })
            {
                Fails(() => component.Input.Range = invalid);
                Fails(() => component.Meter.Range = invalid);
                Assert(component.Input.Range == new NumericRange(100, 200) && component.Meter.Range == new NumericRange(100, 200),
                    "Rejected native ranges leave prior state unchanged.");
                using var invalidWindow = new Window();
                Fails(() => _ = new Values(invalidWindow, invalid));
            }
            Fails(() => component.Input.Orientation = (Axis)99);
            Fails(() => component.Meter.State = (ProgressState)99);
            Assert(component.Meter.State == ProgressState.Paused, "Invalid progress enum leaves prior state unchanged.");
            Fails(() => component.Current = 201);
            Assert(component.Input.Value == 185 && component.Meter.Value == 185, "Generated setter surfaces native validation.");
            component.Current = 170;
            Assert(component.Input.Value == 170 && component.Meter.Value == 170, "Valid updates recover after a native validation error.");
            using (var invalidValueWindow = new Window())
                Fails(() => _ = new Values(invalidValueWindow, new(0, 100)));
            foreach (var theme in new[] { Theme.Light, Theme.HighContrast })
                window.SetTheme(theme);
            window.SetTheme(Theme.Light);
            window.SetVisualStyle(VisualStyle.WinUI);
            using var stop = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var closer = Task.Run(async () =>
            {
                await Task.Delay(400, stop.Token);
                if (!window.Post(() =>
                {
                    component.Input.ChangeValue(160);
                    Assert(component.Calls == 3 && component.Meter.Value == 160, "Mounted native controls retain callback bindings.");
                    window.Close();
                })) throw new InvalidOperationException("The native smoke window rejected shutdown dispatch.");
            });
            window.Run();
            closer.GetAwaiter().GetResult();
            Assert(window.CallbackStatus == 0, "Native smoke finishes without callback errors.");
            Console.WriteLine($"Generated native value-control assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    private static void Fails(Action action)
    {
        try { action(); }
        catch (XuiException) { assertions++; return; }
        throw new InvalidOperationException("Expected explicit native validation failure.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
}

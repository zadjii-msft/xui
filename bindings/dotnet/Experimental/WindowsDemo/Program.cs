internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 0)
            {
                using var window = new Xui.Window("XUI shared authoring experiment", 520, 420);
                _ = new PortableDemo.Greeting(window);
                window.Run();
            }
            else if (args is ["--smoke"]) Smoke();
            else throw new ArgumentException("Usage: WindowsDemo [--smoke]");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    private static void Smoke()
    {
        using var application = new Xui.Application();
        using var window = application.CreateWindow("XUI shared authoring smoke", 520, 420);
        window.SetShowActivated(false);
        var component = new PortableDemo.Greeting(window);
        application.Show(window);
        bool completed = false;
        if (!application.Post(() =>
        {
            try
            {
                Check(window.State == Xui.WindowState.Open, "The native window must be open.");
                Check(component.CountLabel.Text == "Count: 0", "Initial generated label.");
                var input = component.Input;
                ulong inputId = input.Id;
                component.IncrementButton.Invoke();
                Check(component.Count == 1 && component.CountLabel.Text == "Count: 1", "Native click callback updates generated state.");
                component.Entry = "Ada";
                Check(input.Text == "Ada", "Generated state updates the native editor.");
                Check(component.Message == "Type a name, then submit.", "Programmatic edits do not submit.");
                component.Message = "Hello, Ada!";
                Check(component.GreetingLabel.Text == "Hello, Ada!", "Generated message binding.");
                component.Count = 10;
                Check(component.CountLabel.Text == "Count: 10", "Subsequent state update.");
                Check(ReferenceEquals(input, component.Input) && component.Input.Id == inputId, "Input identity remains stable.");
                Check(window.CallbackStatus == 0, "Native callbacks completed without errors.");
                completed = true;
            }
            finally { window.Close(); }
        }))
        {
            window.Close();
            throw new InvalidOperationException("The smoke callback could not be posted.");
        }
        application.Run();
        Check(completed && window.State == Xui.WindowState.Closed, "The smoke run completed and closed its window.");
        Console.WriteLine("Windows shared .xui smoke passed.");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}

using Minesweeper;
using Xui;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            int? seed = args switch
            {
                [] => null,
                ["--seed", var value] when int.TryParse(value, out int parsed) => parsed,
                _ => throw new ArgumentException("Usage: Minesweeper [--seed INTEGER]")
            };
#if XUI_HOT_RELOAD
            Xui.Development.ReloadHost.Run(CreateWindow, window => Build(window, seed));
#else
            using var window = CreateWindow();
            Build(window, seed);
            window.Run();
#endif
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    private static Window CreateWindow()
    {
        var window = new Window("XUI Minesweeper", 440, 700);
        try
        {
            window.IconErrorHandler = error => throw new InvalidOperationException($"Cannot load the application icon: {error}");
            window.SetIconSource(Path.Combine(AppContext.BaseDirectory, "zoey.ico"));
            return window;
        }
        catch
        {
            window.Dispose();
            throw;
        }
    }

    private static void Build(Window window, int? seed)
    {
        var board = new Minefield(window);
        board.SetGame(seed.HasValue ? GameState.New(seed) : board.Game);
        window.Key += e =>
        {
            if ((e.Value & 0xffff) == 0x46) board.ToggleFlagMode();
            if ((e.Value & 0xffff) == 0x71) board.NewGame();
        };
    }
}

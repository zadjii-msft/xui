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

    private static Window CreateWindow() => new("XUI Minesweeper", 440, 700);

    private static void Build(Window window, int? seed)
    {
        var board = new Minefield(window);
        if (seed.HasValue) board.Game = GameState.New(seed);
        window.Key += e =>
        {
            if ((e.Value & 0xffff) == 0x46) board.ToggleFlagMode();
            if ((e.Value & 0xffff) == 0x71) board.NewGame();
        };
    }
}

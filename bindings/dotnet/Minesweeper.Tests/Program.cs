using System.Text.Json;
using Minesweeper;
using Xui;

internal static class Program
{
    private static int assertions;
    private static readonly int[] Indices = Enumerable.Range(0, GameState.CellCount).ToArray();

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    [STAThread]
    private static void Main(string[] args)
    {
        if (args is ["--layout", var seed, var first])
        {
            var game = GameState.New(int.Parse(seed)).Reveal(int.Parse(first));
            Console.WriteLine(JsonSerializer.Serialize(new
            {
                mines = Indices.Where(i => game.GetCell(i).IsMine),
                safe = Indices.Where(i => !game.GetCell(i).IsMine),
                revealed = game.RevealedCount
            }));
            return;
        }
        TestPlacement();
        TestFlags();
        TestLoss();
        TestWin();
        TestBounds();
        TestStyleDefinitions();
        if (args is ["--styles"]) TestNativeStyles();
        Console.WriteLine($"Minesweeper rules and styles: {assertions} assertions passed.");
    }

    private static void TestPlacement()
    {
        foreach (int seed in new[] { 0, 1, 17, 1729, int.MinValue, int.MaxValue })
        {
            foreach (int first in Indices)
            {
                var original = GameState.New(seed);
                Assert(original.Status == GameStatus.Ready && original.Moves == 0, "A new game is ready.");
                Assert(Indices.All(i => original.CellText(i) == "?" && !original.GetCell(i).IsMine), "Placement is deferred.");
                var game = original.Reveal(first);
                Assert(!game.GetCell(first).IsMine && game.GetCell(first).AdjacentMines == 0, "The first reveal opens a safe area.");
                Assert(Indices.Count(i => game.GetCell(i).IsMine) == GameState.MineCount, "Exactly ten distinct mines.");
                Assert(Indices.All(i => original.GetCell(i) == default), "Reveal does not mutate the prior snapshot.");
                Assert(game.Moves == 1, "A flood reveal is one move.");
                Assert(game.RevealedCount == Indices.Count(i => game.GetCell(i).Revealed && !game.GetCell(i).IsMine), "Reveal count agrees with cells.");
                var same = GameState.New(seed).Reveal(first);
                Assert(Indices.All(i => same.GetCell(i) == game.GetCell(i)), "Seeded placement and flood are deterministic.");
                foreach (int i in Indices)
                {
                    var cell = game.GetCell(i);
                    Assert(ReferenceEquals(CellStyles.Select(game, i),
                        cell.Revealed ? CellStyles.Numbers[cell.AdjacentMines] : CellStyles.Covered),
                        "Only revealed cells receive number styles; covered mines stay indistinguishable.");
                    int neighbors = Indices.Count(n => n != i &&
                        Math.Abs(n / 9 - i / 9) <= 1 && Math.Abs(n % 9 - i % 9) <= 1 && game.GetCell(n).IsMine);
                    Assert(cell.AdjacentMines == neighbors, "Neighbor counts do not wrap between rows.");
                    Assert(!cell.IsMine || !cell.Revealed, "Opening the board reveals no mines.");
                    if (cell.Revealed && cell.AdjacentMines == 0)
                        Assert(Indices.Where(n => Math.Abs(n / 9 - i / 9) <= 1 && Math.Abs(n % 9 - i % 9) <= 1)
                            .All(n => game.GetCell(n).Revealed), "Flood fills through empty neighbors.");
                }
            }
        }
    }

    private static void TestFlags()
    {
        var original = GameState.New(17);
        var flagged = original.Flag(1);
        Assert(flagged.FlagCount == 1 && flagged.Moves == 1 && flagged.Status == GameStatus.Ready, "Flagging does not place mines.");
        Assert(flagged.CellText(1) == "F" && original.CellText(1) == "?", "Flagging is immutable.");
        Assert(ReferenceEquals(CellStyles.Select(flagged, 1), CellStyles.Flagged), "A flag has its own covered-cell style.");
        Assert(ReferenceEquals(flagged, flagged.Reveal(1)), "A flag protects its square.");
        var opened = flagged.Reveal(0);
        Assert(opened.GetCell(1).Flagged && !opened.GetCell(1).Revealed && !opened.GetCell(1).IsMine, "Flood reveal respects a safe flagged neighbor.");
        var unflagged = opened.Flag(1);
        Assert(unflagged.FlagCount == 0 && unflagged.Reveal(1).GetCell(1).Revealed, "A cleared flag can be revealed.");
        var budget = original;
        for (int i = 0; i < GameState.MineCount; i++) budget = budget.Flag(i);
        Assert(budget.FlagCount == GameState.MineCount, "All available flags can be placed.");
        Assert(!budget.CanAct(10, true) && ReferenceEquals(budget, budget.Flag(10)), "Flags cannot exceed the mine budget.");
        Assert(budget.CanAct(0, true) && budget.Flag(0).FlagCount == 9, "Flags can still be cleared at the limit.");
    }

    private static GameState Playing()
    {
        for (int seed = 0; seed < 100; seed++)
        {
            var game = GameState.New(seed).Reveal(40);
            if (game.Status == GameStatus.Playing && Indices.Any(i => !game.GetCell(i).IsMine && !game.GetCell(i).Revealed))
                return game;
        }
        throw new InvalidOperationException("No test board has a covered safe cell.");
    }

    private static void TestLoss()
    {
        var game = Playing();
        int[] mines = Indices.Where(i => game.GetCell(i).IsMine).ToArray();
        int safe = Indices.First(i => !game.GetCell(i).IsMine && !game.GetCell(i).Revealed);
        Assert(game.CellText(mines[0]) == "?" && game.CellDescription(mines[0]).EndsWith("covered."), "Covered cells do not reveal mine locations.");
        game = game.Flag(mines[1]).Flag(safe);
        var lost = game.Reveal(mines[0]);
        Assert(lost.Status == GameStatus.Lost && lost.ExplodedCell == mines[0], "A mine ends the game.");
        Assert(lost.CellText(mines[0]) == "!" && lost.CellText(mines[1]) == "F" && lost.CellText(mines[2]) == "*", "Mine and correct-flag presentation.");
        Assert(lost.CellText(safe) == "X", "Wrong flags are identified after loss.");
        Assert(ReferenceEquals(CellStyles.Select(lost, mines[0]), CellStyles.Exploded) &&
            ReferenceEquals(CellStyles.Select(lost, mines[1]), CellStyles.Flagged) &&
            ReferenceEquals(CellStyles.Select(lost, mines[2]), CellStyles.Mine) &&
            ReferenceEquals(CellStyles.Select(lost, safe), CellStyles.IncorrectFlag),
            "Loss styles distinguish the hit mine, correct flags, other mines, and wrong flags.");
        Assert(Indices.All(i => !lost.CanAct(i, true) && !lost.CanAct(i, false)), "A finished board is inactive.");
        Assert(ReferenceEquals(lost, lost.Flag(safe)) && ReferenceEquals(lost, lost.Reveal(safe)), "Loss cannot mutate further.");
        Assert(game.Status == GameStatus.Playing && !game.GetCell(mines[0]).Revealed, "Loss preserves prior snapshots.");
    }

    private static void TestWin()
    {
        var game = Playing();
        int open = Indices.First(i => game.GetCell(i).Revealed);
        Assert(ReferenceEquals(game, game.Reveal(open)) && ReferenceEquals(game, game.Flag(open)), "Revealed squares are no-ops.");
        foreach (int safe in Indices.Where(i => !game.GetCell(i).IsMine).ToArray()) game = game.Reveal(safe);
        Assert(game.Status == GameStatus.Won && game.RevealedCount == GameState.SafeCount, "All safe squares win without manual flags.");
        Assert(game.FlagCount == GameState.MineCount && Indices.Where(i => game.GetCell(i).IsMine).All(i => game.CellText(i) == "F"), "Winning flags the remaining mines.");
        Assert(Indices.All(i => ReferenceEquals(CellStyles.Select(game, i), game.GetCell(i).IsMine ?
            CellStyles.WonFlag : CellStyles.Numbers[game.GetCell(i).AdjacentMines])),
            "Win styling changes mine flags without replacing readable safe-cell numbers.");
        Assert(Indices.All(i => !game.CanAct(i, false)), "Won board cannot reveal more cells.");
        var reset = GameState.New(4);
        Assert(reset.Moves == 0 && reset.RevealedCount == 0 && reset.FlagCount == 0 && reset.Status == GameStatus.Ready, "A new game clears game-over state.");
    }

    private static void TestBounds()
    {
        var game = GameState.New(0);
        foreach (int bad in new[] { -1, 81, int.MaxValue })
        {
            try { game.Reveal(bad); throw new InvalidOperationException("Invalid reveal accepted."); }
            catch (ArgumentOutOfRangeException) { assertions++; }
            try { game.Flag(bad); throw new InvalidOperationException("Invalid flag accepted."); }
            catch (ArgumentOutOfRangeException) { assertions++; }
        }
        Assert(game.CellDescription(0) == "Row 1, column 1: covered." &&
            game.CellDescription(80) == "Row 9, column 9: covered.", "Help text includes board coordinates.");
    }

    private static void TestStyleDefinitions()
    {
        Assert(CellStyles.Covered.Values.BorderThickness == new Insets(1),
            "Covered cells have a uniform border.");
        Assert(CellStyles.Covered.Rules.All(rule => rule.Values.BorderThickness is null),
            "Interaction states do not shift the cell border or text.");
        Assert(CellStyles.Cleared.Values.BorderThickness == new Insets(0) &&
            CellStyles.Cleared.Values.Background is not null, "Borderless cells retain a filled face.");
        Assert(CellStyles.Numbers.Count == 9 && ReferenceEquals(CellStyles.Numbers[0], CellStyles.Cleared),
            "The number palette includes empty squares and all eight counts.");
        var colors = new HashSet<ThemeColor>();
        foreach (var style in CellStyles.Numbers.Skip(1))
        {
            Assert(ReferenceEquals(style.BasedOn, CellStyles.Cleared) && style.Rules.Count == 0,
                "Numbers share flat geometry and do not lose their color when disabled.");
            var color = style.Values.Foreground!.Value;
            var face = CellStyles.Cleared.Values.Background!.Value;
            Assert(colors.Add(color), "Each adjacent-mine count has a distinct theme-aware color.");
            Assert(Contrast(color.Light, face.Light) >= 4.5 && Contrast(color.Dark, face.Dark) >= 4.5,
                "Number text meets 4.5:1 contrast in both themes.");
        }
    }

    private static double Contrast(uint first, uint second)
    {
        static double Luminance(uint rgb)
        {
            static double Channel(uint value)
            {
                double component = value / 255.0;
                return component <= 0.04045 ? component / 12.92 : Math.Pow((component + 0.055) / 1.055, 2.4);
            }
            return 0.2126 * Channel(rgb >> 16) + 0.7152 * Channel((rgb >> 8) & 255) + 0.0722 * Channel(rgb & 255);
        }
        double a = Luminance(first), b = Luminance(second);
        return (Math.Max(a, b) + 0.05) / (Math.Min(a, b) + 0.05);
    }

    private static void TestNativeStyles()
    {
        using var window = new Window("Minesweeper style tests", 440, 700);
        var board = new Minefield(window);
        board.SetGame(GameState.New(17));
        Assert(board.CellButtons.Count == GameState.CellCount &&
            board.CellButtons.Select(b => b.Id).Distinct().Count() == GameState.CellCount,
            "The compiled view exposes all distinct native cells in board order.");
        Label[] coordinates = [
            board.Column1, board.Column2, board.Column3, board.Column4, board.Column5,
            board.Column6, board.Column7, board.Column8, board.Column9,
            board.Row1, board.Row2, board.Row3, board.Row4, board.Row5,
            board.Row6, board.Row7, board.Row8, board.Row9
        ];
        foreach (var coordinate in coordinates)
        {
            var values = coordinate.GetControlStyleValues(StylePart.Root, effective: true);
            Assert(values.HorizontalAlignment == StyleAlignment.Center &&
                values.VerticalAlignment == StyleAlignment.Center,
                "Every compiled coordinate label centers its text on both axes.");
        }

        void CheckBoard()
        {
            for (int i = 0; i < GameState.CellCount; i++)
            {
                var button = board.CellButtons[i];
                var style = CellStyles.Select(board.Game, i);
                var values = button.EffectiveStyleValues;
                Assert(ReferenceEquals(button.Style, style) && button.Text == board.Game.CellText(i),
                    "Compiled native text and style agree with the game state.");
                Assert(values.Background is not null && values.CornerRadius == 0,
                    "Every native cell retains its square filled face.");
                if (board.Game.GetCell(i).Revealed && !board.Game.GetCell(i).IsMine)
                {
                    Assert(values.BorderThickness == new Insets(0) &&
                        values.Foreground == (style.Values.Foreground ?? CellStyles.Cleared.Values.Foreground),
                        "Revealed disabled cells compile to borderless, readable numbers.");
                }
            }
        }

        CheckBoard();
        board.ToggleFlagMode();
        board.Square00.Invoke();
        Assert(board.Game.GetCell(0).Flagged, "A native cell invocation uses its original board index.");
        board.ToggleFlagMode();
        Assert(board.Square00.EffectiveStyleValues.Foreground == CellStyles.Flagged.Values.Foreground,
            "Disabled flags retain their identifying ink.");
        board.Square44.Invoke();
        Assert(board.Game.GetCell(40).Revealed, "Native reveal still opens the requested square.");
        CheckBoard();
        ulong before = window.Button("Before unchanged refreshes").Id;
        for (int i = 0; i < 128; i++) board.SetGame(board.Game);
        Assert(window.Button("After unchanged refreshes").Id == before + 1,
            "Unchanged refreshes allocate no native style handles.");

        var playing = Playing();
        int[] mines = Indices.Where(i => playing.GetCell(i).IsMine).ToArray();
        int wrongFlag = Indices.First(i => !playing.GetCell(i).IsMine && !playing.GetCell(i).Revealed);
        board.SetGame(playing.Flag(mines[1]).Flag(wrongFlag));
        board.CellButtons[mines[0]].Invoke();
        Assert(board.Game.Status == GameStatus.Lost, "Native mine invocation still loses.");
        CheckBoard();
        Assert(board.CellButtons[mines[0]].EffectiveStyleValues.Background == CellStyles.Exploded.Values.Background &&
            board.CellButtons[wrongFlag].EffectiveStyleValues.Foreground == CellStyles.IncorrectFlag.Values.Foreground,
            "Disabled loss cells retain their error backgrounds and text colors.");
        board.NewGame();
        Assert(board.Game.Status == GameStatus.Ready && !board.Flagging, "Reset clears the game and flag mode.");
        CheckBoard();
        board.SetGame(playing);
        foreach (int safe in Indices.Where(i => !playing.GetCell(i).IsMine))
            if (board.Game.CanAct(safe, false)) board.CellButtons[safe].Invoke();
        Assert(board.Game.Status == GameStatus.Won, "Native safe-cell invocations still win.");
        CheckBoard();
        Assert(board.CellButtons[mines[0]].EffectiveStyleValues.Background == CellStyles.WonFlag.Values.Background,
            "Disabled win flags use the completed-board style.");

        // Compile every number, including the rare seven/eight cases, through the native binding.
        var number = window.Button("8").FixedSize(36, 36).SetEnabled(false);
        foreach (var theme in new[] { Theme.Light, Theme.Dark, Theme.HighContrast })
        {
            window.SetTheme(theme);
            foreach (var style in CellStyles.Numbers)
            {
                number.Style = style;
                Assert(number.EffectiveStyleValues.BorderThickness == new Insets(0) &&
                    number.EffectiveStyleValues.Foreground == (style.Values.Foreground ?? CellStyles.Cleared.Values.Foreground),
                    "All number definitions compile natively and preserve theme pairs.");
            }
            CheckBoard();
        }
    }
}

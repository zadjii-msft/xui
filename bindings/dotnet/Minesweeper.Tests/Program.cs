using System.Text.Json;
using Minesweeper;

internal static class Program
{
    private static int assertions;
    private static readonly int[] Indices = Enumerable.Range(0, GameState.CellCount).ToArray();

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

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
        Console.WriteLine($"Minesweeper rules: {assertions} assertions passed.");
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
}

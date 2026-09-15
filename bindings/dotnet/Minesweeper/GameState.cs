using System.Globalization;

namespace Minesweeper;

public enum GameStatus { Ready, Playing, Won, Lost }

public readonly record struct Cell(bool IsMine, int AdjacentMines, bool Revealed, bool Flagged);

public sealed class GameState
{
    public const int Side = 9;
    public const int MineCount = 10;
    public const int CellCount = Side * Side;
    public const int SafeCount = CellCount - MineCount;
    private static readonly string[] Numbers = [" ", "1", "2", "3", "4", "5", "6", "7", "8"];
    private readonly Cell[] cells;
    private readonly int seed;

    private GameState(Cell[] cells, int seed, GameStatus status, int revealed, int flags, int moves, int exploded = -1)
    {
        this.cells = cells;
        this.seed = seed;
        Status = status;
        RevealedCount = revealed;
        FlagCount = flags;
        Moves = moves;
        ExplodedCell = exploded;
    }

    public GameStatus Status { get; }
    public int RevealedCount { get; }
    public int FlagCount { get; }
    public int Moves { get; }
    public int ExplodedCell { get; }
    public bool Finished => Status is GameStatus.Won or GameStatus.Lost;
    public string Summary => $"Flags: {FlagCount}/{MineCount}    Safe squares: {RevealedCount}/{SafeCount}    Moves: {Moves}";
    public string StatusText => Status switch
    {
        GameStatus.Ready => "Your first reveal opens a safe area.",
        GameStatus.Playing => "Reveal every safe square to win.",
        GameStatus.Won => $"You won! All {SafeCount} safe squares are clear.",
        GameStatus.Lost => "Mine hit! Start a new game to try again.",
        _ => throw new InvalidOperationException("Unknown game status.")
    };

    public static GameState New(int? seed = null) =>
        new(new Cell[CellCount], seed ?? Random.Shared.Next(), GameStatus.Ready, 0, 0, 0);

    public Cell GetCell(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, CellCount);
        return cells[index];
    }

    public bool CanAct(int index, bool flagMode)
    {
        var cell = GetCell(index);
        return !Finished && !cell.Revealed &&
            (flagMode ? cell.Flagged || FlagCount < MineCount : !cell.Flagged);
    }

    public string CellText(int index)
    {
        var cell = GetCell(index);
        if (Status == GameStatus.Lost)
        {
            if (index == ExplodedCell) return "!";
            if (cell.IsMine) return cell.Flagged ? "F" : "*";
            if (cell.Flagged) return "X";
        }
        if (cell.Flagged) return "F";
        return cell.Revealed ? Numbers[cell.AdjacentMines] : "?";
    }

    public string CellDescription(int index)
    {
        var cell = GetCell(index);
        string description;
        if (Status == GameStatus.Lost && index == ExplodedCell) description = "exploded mine";
        else if (Status == GameStatus.Lost && cell.IsMine) description = cell.Flagged ? "correctly flagged mine" : "mine";
        else if (Status == GameStatus.Lost && cell.Flagged) description = "incorrect flag";
        else if (cell.Flagged) description = "flagged";
        else if (!cell.Revealed) description = "covered";
        else if (cell.AdjacentMines == 0) description = "empty";
        else description = $"{cell.AdjacentMines.ToString(CultureInfo.InvariantCulture)} adjacent mines";
        return $"Row {index / Side + 1}, column {index % Side + 1}: {description}.";
    }

    public GameState Flag(int index)
    {
        if (!CanAct(index, true)) return this;
        var next = (Cell[])cells.Clone();
        bool flagged = !next[index].Flagged;
        next[index] = next[index] with { Flagged = flagged };
        return new(next, seed, Status, RevealedCount, FlagCount + (flagged ? 1 : -1), Moves + 1);
    }

    public GameState Reveal(int index)
    {
        if (!CanAct(index, false)) return this;
        var next = (Cell[])cells.Clone();
        if (Status == GameStatus.Ready) PlaceMines(next, index);
        if (next[index].IsMine)
        {
            next[index] = next[index] with { Revealed = true };
            return new(next, seed, GameStatus.Lost, RevealedCount, FlagCount, Moves + 1, index);
        }

        int revealed = RevealedCount;
        var pending = new Queue<int>();
        RevealSafe(index);
        while (pending.TryDequeue(out int empty))
            foreach (int neighbor in Neighbors(empty)) RevealSafe(neighbor);

        bool won = revealed == SafeCount;
        if (won)
            for (int i = 0; i < next.Length; i++)
                if (next[i].IsMine) next[i] = next[i] with { Flagged = true };
        return new(next, seed, won ? GameStatus.Won : GameStatus.Playing,
            revealed, won ? MineCount : FlagCount, Moves + 1);

        void RevealSafe(int square)
        {
            var cell = next[square];
            if (cell.Revealed || cell.Flagged || cell.IsMine) return;
            next[square] = cell with { Revealed = true };
            revealed++;
            if (cell.AdjacentMines == 0) pending.Enqueue(square);
        }
    }

    private void PlaceMines(Cell[] next, int first)
    {
        var candidates = Enumerable.Range(0, CellCount)
            .Where(i => Math.Abs(i / Side - first / Side) > 1 || Math.Abs(i % Side - first % Side) > 1)
            .ToArray();
        var random = new Random(seed);
        for (int i = 0; i < MineCount; i++)
        {
            int choice = random.Next(i, candidates.Length);
            (candidates[i], candidates[choice]) = (candidates[choice], candidates[i]);
            int square = candidates[i];
            next[square] = next[square] with { IsMine = true };
        }
        for (int i = 0; i < CellCount; i++)
            next[i] = next[i] with { AdjacentMines = Neighbors(i).Count(n => next[n].IsMine) };
    }

    private static IEnumerable<int> Neighbors(int index)
    {
        int row = index / Side, column = index % Side;
        for (int y = Math.Max(0, row - 1); y <= Math.Min(Side - 1, row + 1); y++)
            for (int x = Math.Max(0, column - 1); x <= Math.Min(Side - 1, column + 1); x++)
                if (y != row || x != column) yield return y * Side + x;
    }
}

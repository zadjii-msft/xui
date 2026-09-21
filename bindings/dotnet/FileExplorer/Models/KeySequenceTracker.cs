namespace Xui.FileExplorer.Models;

public sealed class KeySequenceTracker
{
    private readonly List<KeyGesture> pending = [];
    private long started;
    public int PendingCount => pending.Count;
    public void Reset() => pending.Clear();
    public (bool Handled, string? Command) Match(KeyGesture stroke,
        IEnumerable<(string Id, string[] Bindings)> commands, bool allowSequence, long now)
    {
        if (now - started > 1800 || !allowSequence) pending.Clear();
        if (pending.Count != 0 && stroke.Key == 0x1b) { pending.Clear(); return (true, null); }
        var next = pending.Append(stroke).ToArray();
        foreach (var (id, bindings) in commands)
        foreach (string binding in bindings)
        {
            var keys = KeyGesture.ParseSequence(binding);
            if (keys.Length < next.Length || !keys.Take(next.Length).SequenceEqual(next)) continue;
            if (keys.Length > next.Length)
            {
                if (!allowSequence) continue;
                pending.Add(stroke);
                started = now;
                return (true, null);
            }
            pending.Clear();
            return (true, id);
        }
        pending.Clear();
        return (false, null);
    }
}

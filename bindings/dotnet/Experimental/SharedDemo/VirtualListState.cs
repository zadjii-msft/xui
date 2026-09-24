using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed class VirtualListState
{
    public IReadOnlyDictionary<string, VirtualListItem> Items { get; }

    public VirtualListState(int count = 10000)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        Items = Enumerable.Range(0, count).Select(index => new VirtualListItem(index))
            .ToDictionary(item => item.Key, StringComparer.Ordinal);
    }
}

public sealed class VirtualListItem
{
    public string Key { get; }
    public int Number { get; }
    public string Draft { get; set; }
    public TextSelection Selection { get; set; }
    public bool Removed { get; set; }

    internal VirtualListItem(int number)
    {
        Number = number;
        Key = $"task-{number:D5}";
        Draft = $"Task {number + 1:N0}";
    }
}

namespace Xui.FileExplorer;

internal sealed record ExplorerCommand(string Name, string Shortcut, Action Execute, Func<bool>? CanExecute = null,
    string? Id = null, string[]? Aliases = null)
{
    public bool Enabled => CanExecute?.Invoke() ?? true;
    public string StableId => Id ?? string.Join('-', Name.ToLowerInvariant().Split([' ', '/'], StringSplitOptions.RemoveEmptyEntries));
}

internal sealed class CommandRows(IReadOnlyList<ExplorerCommand> commands, Func<ExplorerCommand, string>? shortcut = null) : IReadOnlyImmutableSource
{
    private readonly ItemContent[] items = commands
        .Select(command => new ItemContent(command.Name, shortcut?.Invoke(command) ?? command.Shortcut, command.Enabled)).ToArray();

    public ulong Count => (ulong)items.Length;
    public ItemKey Key(ulong index) => new(index + 1);
    public ulong? Find(ItemKey key) => key.Id > 0 && key.Id <= Count ? key.Id - 1 : null;
    public ItemContent Item(ulong index, ulong column = 0) => items[checked((int)index)];
}

using Xui;

namespace Minesweeper;

public static class CellStyles
{
    private static readonly ThemeColor Ink = new(0x242424, 0xF2F2F2);
    private static readonly ThemeColor Face = new(0xF5F5F5, 0x424242);
    private static readonly ThemeColor ClearedFace = new(0xE5E5E5, 0x242424);
    private static readonly ThemeColor Edge = new(0x707070, 0xA0A0A0);
    private static readonly ThemeColor FlagInk = new(0xA61B1B, 0xFFABAB);

    public static readonly ControlStyle Covered = new(StyleTarget.Button, [new(StylePart.Root, new()
    {
        Background = Face, Foreground = Ink, BorderBrush = Edge,
        BorderThickness = new(1), CornerRadius = 0, Padding = new(0)
    })],
    [
        new(StylePart.Root, StyleState.Hovered, new() { Background = new(0xFFFFFF, 0x565656) }),
        new(StylePart.Root, StyleState.Pressed, new()
        {
            Background = ClearedFace
        }),
        new(StylePart.Root, StyleState.Disabled, new()
        {
            Background = new(0xD6D6D6, 0x333333), Foreground = new(0x555555, 0xBDBDBD)
        })
    ]);

    public static readonly ControlStyle Cleared = new(StyleTarget.Button, [new(StylePart.Root, new()
    {
        Background = ClearedFace, Foreground = Ink,
        BorderThickness = new(0), CornerRadius = 0, Padding = new(2)
    })]);

    // Revealed cells are disabled, but their numbers must remain readable.
    private static ControlStyle Number(uint light, uint dark) =>
        new(StyleTarget.Button, [new(StylePart.Root, new() { Foreground = new(light, dark) })], basedOn: Cleared);

    public static readonly IReadOnlyList<ControlStyle> Numbers = Array.AsReadOnly<ControlStyle>(
    [
        Cleared,
        Number(0x174EA6, 0x8AB4F8),
        Number(0x176B2C, 0x81C995),
        Number(0xB3261E, 0xF28B82),
        Number(0x342080, 0xB5A0EF),
        Number(0x7A281E, 0xE6A391),
        Number(0x00666B, 0x78D9DC),
        Number(0x161616, 0xF5F5F5),
        Number(0x595959, 0xB0B0B0)
    ]);

    public static readonly ControlStyle Flagged = new(StyleTarget.Button, [new(StylePart.Root, new() { Foreground = FlagInk })],
        [new(StylePart.Root, StyleState.Disabled, new() { Foreground = FlagInk })], Covered);
    public static readonly ControlStyle Mine = new(StyleTarget.Button, [new(StylePart.Root, new()
    {
        Background = new(0xD8CACA, 0x463333), Foreground = Ink
    })], basedOn: Cleared);
    public static readonly ControlStyle Exploded = new(StyleTarget.Button, [new(StylePart.Root, new()
    {
        Background = new(0xB3261E, 0xB3261E), Foreground = new(0xFFFFFF)
    })], basedOn: Cleared);
    public static readonly ControlStyle IncorrectFlag = new(StyleTarget.Button, [new(StylePart.Root, new()
    {
        Background = new(0xFFE0B2, 0x593B16), Foreground = new(0x702B00, 0xFFDFB5)
    })], basedOn: Cleared);
    public static readonly ControlStyle WonFlag = new(StyleTarget.Button, [new(StylePart.Root, new()
    {
        Background = new(0xCEEAD6, 0x244C32), Foreground = new(0x14532D, 0xB7F0CA)
    })], basedOn: Cleared);

    public static ControlStyle Select(GameState game, int index)
    {
        var cell = game.GetCell(index);
        if (game.Status == GameStatus.Lost)
        {
            if (index == game.ExplodedCell) return Exploded;
            if (cell.IsMine) return cell.Flagged ? Flagged : Mine;
            if (cell.Flagged) return IncorrectFlag;
        }
        if (cell.Flagged) return game.Status == GameStatus.Won ? WonFlag : Flagged;
        return cell.Revealed ? Numbers[cell.AdjacentMines] : Covered;
    }
}

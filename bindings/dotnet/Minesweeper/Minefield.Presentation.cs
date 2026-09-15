using Xui;

namespace Minesweeper;

public sealed partial class Minefield
{
    private Button[]? cells;

    public IReadOnlyList<Button> CellButtons => cells ??= [
        Square00, Square01, Square02, Square03, Square04, Square05, Square06, Square07, Square08,
        Square10, Square11, Square12, Square13, Square14, Square15, Square16, Square17, Square18,
        Square20, Square21, Square22, Square23, Square24, Square25, Square26, Square27, Square28,
        Square30, Square31, Square32, Square33, Square34, Square35, Square36, Square37, Square38,
        Square40, Square41, Square42, Square43, Square44, Square45, Square46, Square47, Square48,
        Square50, Square51, Square52, Square53, Square54, Square55, Square56, Square57, Square58,
        Square60, Square61, Square62, Square63, Square64, Square65, Square66, Square67, Square68,
        Square70, Square71, Square72, Square73, Square74, Square75, Square76, Square77, Square78,
        Square80, Square81, Square82, Square83, Square84, Square85, Square86, Square87, Square88
    ];

    public void SetGame(GameState game)
    {
        ArgumentNullException.ThrowIfNull(game);
        Game = game;
        for (int i = 0; i < CellButtons.Count; i++)
        {
            var button = CellButtons[i];
            var style = CellStyles.Select(game, i);
            if (!ReferenceEquals(button.Style, style)) button.Style = style;
        }
    }
}

# Minesweeper

This sample uses `.xui` for the complete interface.
It has a fixed 9-by-9 board with ten mines.
The first reveal opens an empty area.
Empty areas expand automatically.
The game counts flags, safe squares, and moves.

## Run

First, build the native library with [CONTRIBUTING](../../../CONTRIBUTING.md#build-the-native-code).
Then use the [C# sample commands](../../../CONTRIBUTING.md#c-and-declarative-samples) with `bindings\dotnet\Minesweeper` as the project.

For a repeatable initial board, append `-- --seed 17`.
The seed and the first revealed square determine the mine locations.
**New game** creates a new random board.

## Play

Click a covered square to reveal it.
Numbers show the adjacent mine count.
Reveal every safe square to win.

Press **F** or select **Flag mode** to change modes.
In flag mode, click a covered square to place or clear a flag.
The game permits ten flags.
Press **F2** or select **New game** to start again.
Use **Tab** and **Space** to play without a pointer.

The board uses ASCII symbols: `?` for covered squares, `F` for flags, and a blank for empty squares.
After a loss, `!` marks the hit mine, `*` marks other mines, and `X` marks incorrect flags.
Each square supplies its row, column, and state through native help text.
This demo does not include right-click flag placement, chording, a timer, or difficulty selection.

## Cell appearance

Covered squares have uniform one-DIP borders.
Revealed squares have flat, filled faces without borders.
All squares keep the same size and position.
Cell text is centered vertically and horizontally.
The row and column labels use the same alignment.
Numbers use blue, green, red, indigo, brown, teal, black, and gray for counts from one through eight.
The dark theme uses lighter equivalents.

During play and after a loss, flags retain their red `F`, including inactive flags.
After a loss, the hit mine has a red face and a white `!`.
Other mines have muted red faces and `*` symbols.
Incorrect flags have amber faces and `X` symbols.
After a win, the mine flags have green faces.
The status text also identifies a win or loss.

Inactive covered squares have muted faces.
Revealed numbers keep their colors when inactive.
Keyboard focus keeps the native focus indicator.
High contrast uses the framework's system colors and visible outlines instead of the authored colors and borders.
Symbols and native help text identify states without color.

## Edit with hot reload

Run the watcher instead of `dotnet run`:

```powershell
dotnet watch --project bindings\dotnet\Minesweeper\Minesweeper.csproj --non-interactive
```

Edit `bindings\dotnet\Minesweeper\Minefield.xui`.
Text, existing `size` and `help` expressions, and supported handler-body edits update the running game.
These edits preserve the current board.
Structural edits reset the game through window replacement or process restart.
Adding or removing an optional `size` or `help` binding also resets the game.

The [language guide](../../../docs/specs/xui-language.md) describes the reload rules and VS Code syntax package.

## Read the implementation

`Minefield.xui` declares the layout, controls, state bindings, and event handlers.
The explicit cell declarations compile into a fixed native tree.
The current language does not support repeated child templates.
Each named cell handler forwards its index to one shared action.

`GameState.cs` contains the rules and immutable board snapshots.
An action assigns a replacement `Game` state.
Generated C# updates changed native properties.
The game does not create or remove controls during play.

`CellStyles.cs` defines the shared, immutable [Button styles](../../../docs/specs/control-styling.md).
`Minefield.Presentation.cs` uses explicit cell references and selects styles from the current game state.
`SetGame` updates both the generated state bindings and the cell styles.
The program and game handlers use this method for every board replacement.
Repeated refreshes reuse existing definitions and skip unchanged style assignments.
The native binding releases styles that no cell uses.

`Program.cs` creates the window, selects the development host, and registers keyboard shortcuts.
Release output excludes the development host and compiler.
The sample does not add a UI interpreter or virtual tree.

## Publish and test

Use the [NativeAOT commands](../../../CONTRIBUTING.md#nativeaot-and-deployment) with the Minesweeper project.
The [test instructions](../../../CONTRIBUTING.md#tests) include game-rule checks and the native integration script.
The model checks include style selection and number contrast for both themes.
The test executable accepts `--styles` for additional native style checks and requires the matching `xui.dll` on `PATH`.

The integration script checks cell bindings, geometry, flags, first-click safety, win/loss behavior, and restart.
It also checks state-preserving edits, binding removal, and a NativeAOT executable.
The watcher changes an isolated copy under `build\minesweeper-check`, not the tracked sample.
The script stops its processes and retains logs in that directory.

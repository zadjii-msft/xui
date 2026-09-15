# Minesweeper

This sample uses `.xui` for the complete interface.
It has a fixed 9-by-9 board with ten mines.
The first reveal opens an empty area.
Empty areas expand automatically.
The game counts flags, safe squares, and moves.

## Run

The commands use Windows ARM64, the .NET 10 SDK, and Visual Studio C++ build tools.
Run these commands from the repository root.

1. Build the native library:

   ```powershell
   cmake -S . -B build\xui-language -A ARM64
   cmake --build build\xui-language --config Release --target xui
   ```

2. Add the library directory to this shell's DLL search path:

   ```powershell
   $env:PATH = (Resolve-Path build\xui-language\Release).Path + ";" + $env:PATH
   ```

3. Run the game:

   ```powershell
   dotnet run --project bindings\dotnet\Minesweeper\Minesweeper.csproj
   ```

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

The [language guide](../../../docs/xui-language.md) describes the reload rules and VS Code syntax package.

## Read the implementation

`Minefield.xui` declares the layout, controls, state bindings, and event handlers.
The explicit cell declarations compile into a fixed native tree.
The current language does not support repeated child templates.
Each named cell handler forwards its index to one shared action.

`GameState.cs` contains the rules and immutable board snapshots.
An action assigns a replacement `Game` state.
Generated C# updates changed native properties.
The game does not create or remove controls during play.

`Program.cs` creates the window, selects the development host, and registers keyboard shortcuts.
Release output excludes the development host and compiler.
The sample does not add a UI interpreter or virtual tree.

## Publish

Publish the native executable:

```powershell
dotnet publish bindings\dotnet\Minesweeper\Minesweeper.csproj -c Release -p:PublishAot=true
Copy-Item build\xui-language\Release\xui.dll bindings\dotnet\Minesweeper\bin\Release\net10.0\win-arm64\publish\
```

The executable and `xui.dll` must use the same architecture.

## Run the checks

Run the game-rule checks without a native window:

```powershell
dotnet run --project bindings\dotnet\Minesweeper.Tests
```

Build the native probe, then run the integration script:

```powershell
cmake --build build\xui-language --config Release --target xui xui_language_probe
.\tests\minesweeper.ps1
```

The script checks every cell's binding and geometry, flags, first-click safety, win/loss behavior, and restart.
It also checks state-preserving edits, binding removal, and a NativeAOT executable.
The watcher edits an isolated copy under `build\minesweeper-check`, not the tracked sample.
The script stops its processes and leaves logs in that directory.

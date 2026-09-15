# Thumbnail sample

See [CONTRIBUTING](../../CONTRIBUTING.md#native-samples) for build and run commands.

## Thumbnail sample

Without an argument, the sample displays an empty state and a folder field.
Open folder and Enter in the field start an asynchronous scan.
Unload clears the current view. Resource usage removes unused decoded entries and displays the current counters.
F6 switches between dark and light themes. The sample never changes or deletes source files.

Tab moves focus to the thumbnail viewport.
Arrow keys, Page Up, Page Down, Home, End, the mouse wheel, and UIA scrolling move that viewport.
Images remain read-only. The sample does not add selection or a nonfunctional Open image action.

`demo\thumbnail_grid.hpp` subclasses public `Stack` for sample-specific layout.
It retains ten rows of four tiles, not one control per file.
Each tile contains an `Image` and a caption. Row slots rotate as the viewport moves.
The pool covers the visible rows and a buffer within the 960-DIP maximum viewport height.
Only visible images decode. Buffered controls do not prefetch files.
The viewport keeps keyboard focus while tile names and sources change.

The source loader rejects more than 20,000 images or more than four million UTF-16 path units.
It retains a bounded immutable source, not a paged filesystem index.
Folder replacement uses the existing latest-generation `ViewTask` mailbox.
The image service separately bounds work that cannot stop inside a codec.

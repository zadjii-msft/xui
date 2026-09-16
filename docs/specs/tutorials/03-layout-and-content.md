# 3. Layout and existing controls

This chapter adds a progress indicator without inventing a `.xui` constructor.
`Progress` exists in the C# binding, but it is not a built-in markup node.
Create it through the owning window and pass it into the component.

## Pass an existing element

Add these parameters before the state declarations in `TaskCard.xui`:

```xui
param string Heading;
param global::Xui.Progress Completion;
```

Replace `Text("Today", id: "heading");` with:

```xui
Text(Heading, id: "heading");
```

Add this node after the toggle:

```xui
Content(Completion);
```

Change the component creation in `Program.cs`:

```csharp
var progress = window.Progress("Task completion")
    .SetRange(new(0, 100))
    .SetValue(0)
    .FixedSize(360, 20);
_ = new Tutorial.TaskCard(window, "Today", progress);
```

The parameter order defines the generated constructor order.
`Content` attaches that exact Progress element.
It neither clones the element nor transfers it to another window.
The element must not already have a parent.

## Keep imperative values in sync

Replace `SetComplete` with this method:

```csharp
void SetComplete(bool value)
{
    Complete = value;
    Completion.Value = value ? 100 : 0;
}
```

In `Reset`, replace `Complete = false;` with `SetComplete(false);`.
This keeps the state-driven toggle and imperative Progress value consistent.
The indicator measures task completion, not the number of Apply clicks.

This distinction matters: `Content(Completion)` observes element identity, not all its properties.
Generated state bindings do not discover arbitrary imperative changes.

## Arrange a row with Grid

Replace the toggle and the following `Content` node with:

```xui
Grid("Completion row",
    rows: [new(global::Xui.TrackSizing.Automatic)],
    columns: [new(global::Xui.TrackSizing.Automatic),
              new(global::Xui.TrackSizing.Star, 1)]) {
    Toggle("Complete", checked: Complete, change: SetComplete,
        id: "complete");
    Content(Completion, column: 1);
}
```

An Automatic track measures its content.
A Star track receives a share of the remaining space.
Grid placements start at zero.
Here, the toggle uses column zero and Progress uses column one.
The sample fixes the Progress size so that the result stays easy to inspect.

Use Stack for a simple row or column.
Use Grid when controls must line up across rows or columns.
Use `ScrollView` when content must remain reachable in a smaller viewport.
Its markup body requires exactly one child, usually a Stack.

Do not use fixed heights for every text element.
Long text, localization, and larger fonts need room.
See [layout controls](../controls/README.md) for sizing and scrolling alternatives.

Continue with [reusable components](04-components.md).

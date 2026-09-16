# 4. Reusable components

The task card now has two immutable constructor inputs: `Heading` and `Completion`.
Its editable task state remains private to each component instance.
Reuse the component instead of copying its view.

## Compose two cards

For this exercise, replace the single-card setup in `Main` with:

```csharp
using var window = new Window("Two task cards", 620, 760);
var firstProgress = window.Progress("First task completion")
    .SetRange(new(0, 100)).SetValue(0).FixedSize(360, 20);
var secondProgress = window.Progress("Second task completion")
    .SetRange(new(0, 100)).SetValue(0).FixedSize(360, 20);

var first = new Tutorial.TaskCard(window, "Today", firstProgress, attach: false);
var second = new Tutorial.TaskCard(window, "Tomorrow", secondProgress, attach: false);
var cards = window.Stack().Spacing(16).Add(first.Root).Add(second.Root);
var viewport = window.ScrollView(cards, "Task cards");
window.SetContent(window.Stack().Add(viewport, flex: 1));
window.Run();
```

This code belongs inside the same STA entry point.
The `using Xui;` import from chapter 1 remains required.
Each component receives a different Progress element.

`attach: false` creates the component tree without replacing the window's content.
`Root` exposes the existing root.
The outer Stack and ScrollView arrange both roots under one window.
Only the final `SetContent` installs that composition.

The literal child IDs in this exercise repeat between cards.
For production automation, locate each card through its parent or assign distinct IDs through the exposed controls.
Do not assume that a repeated ID identifies one element across the whole window.

## Respect ownership

An element can have one parent and one owning window.
Do not attach the first card's Progress to the second card.
Do not pass a control from a different window into either constructor.
The native bindings reject those operations.

A component parameter is not a mutable binding slot.
`Content` evaluates its identity expression during construction.
If your application needs replaceable content, use the relevant imperative container API and its ownership contract.
Do not model that replacement as a state-dependent `Content` expression.

The generated constructor can directly attach only a Stack root.
A component with a Grid or other root must use `attach: false` and an outer Stack.
See [component construction](../xui-language.md#reuse-a-component).

## Separate state from services

Use parameters for existing controls and stable dependencies.
Use `state` for values whose replacement should refresh authored expressions.
Keep file access, navigation controllers, and cancellation ownership in explicit application services.
The declarative layer does not manage those services for you.

The checked-in final sample keeps one card so its startup remains small.
Continue with [shared styles](05-styling.md), using either one card or this composition.

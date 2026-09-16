# 2. State, input, and validation

Replace the first chapter's `TaskCard.xui` with this complete component.
Keep its `Program.cs` unchanged.

```xui
namespace Tutorial;

component TaskCard {
    state string TaskName = "";
    state bool Complete = false;
    state int ApplyCount = 0;
    state string Status = "Enter a task, then apply.";

    view {
        VStack(spacing: 12, padding: 20) {
            Text("Today", id: "heading");
            TextInput("Task name", text: TaskName, change: SetTaskName,
                placeholder: "For example, review the design",
                id: "task-name", ref: EntryField);
            Toggle("Complete", checked: Complete, change: SetComplete,
                id: "complete");
            HStack(spacing: 8) {
                Button("Apply", click: Apply, id: "apply");
                Button("Reset", click: Reset, id: "reset");
            }
            Text(Status, id: "status");
            Text($"Applied {ApplyCount} time(s)", id: "apply-count");
        }
    }

    code csharp {
        void SetTaskName(string value) => TaskName = value;
        void SetComplete(bool value) => Complete = value;

        void Apply()
        {
            if (global::System.String.IsNullOrWhiteSpace(TaskName))
            {
                Status = "Enter a task name before applying.";
                return;
            }
            ApplyCount++;
            Status = Complete ? $"Completed: {TaskName}" : $"To do: {TaskName}";
        }

        void Reset()
        {
            TaskName = "";
            Complete = false;
            Status = "Enter a task, then apply.";
        }
    }
}
```

## Follow the data

`state` declares component state with a generated property.
When `TaskName` changes, the generated code updates expressions that directly use it.
When `Status` changes, only its dependent label needs that update.
The component does not rebuild its tree on every click.

The input's `change` callback receives a string.
The toggle's callback receives a Boolean.
The button's callback takes no arguments.
Use the exact callback signatures rather than a general C# event-argument pattern.

The text input has a separate accessible name, `Task name`.
Its placeholder is a hint, not a substitute for that name.
The `ref` argument exposes a typed `EntryField` property for imperative integration.
It does not create another input.

## Keep validation explicit

An empty or whitespace-only task produces a visible message.
That path does not increment `ApplyCount`.
Apply changes in-memory presentation only; it does not save a file.
Reset clears the draft and completion flag, but retains the application count.

Ordinary validation is application behavior, not an exception.
Unexpected native or callback failures must remain errors.
Do not catch every exception and display an apparent success message.

## Preserve native editing

XUI uses native editing for text input.
Do not replace it with a painted label and custom keyboard handlers.
Keep input changes short and avoid blocking I/O in the callback.
For a password, use `PasswordInput` and its scoped read API instead of this plain-text field.

A reference-type state value needs a replacement assignment to notify the generated bindings.
Changing an object inside an existing collection is not an automatic state notification.
See [state dependencies](../xui-language.md#bind-state).

## Check the behavior

Apply with an empty field, then with a task name.
The first action should show validation; the second should increment the count.
Toggle Complete and apply again.
Reset should clear the field and toggle without changing the count.

Check typing, selection, clipboard shortcuts, and undo in the text field.
These are manual checks for your application, not claims about every input method or assistive technology.

Continue with [layout and existing controls](03-layout-and-content.md).

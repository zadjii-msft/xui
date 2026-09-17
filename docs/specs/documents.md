# Documents, dialogs, and color

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.
For C# and Rust coverage, use the [binding reference](bindings.md).

## Documents, dialogs, and color

Include `xui/documents.hpp` for these seven families.
Property setters are silent. Native user edits and semantic actions invoke callbacks.
`Control::set_visible` hides a control and its retained descendants.
Hidden native documents keep their last nonzero geometry. This avoids repeated calendar font allocation during page changes.

```cpp
auto notes = std::make_shared<xui::MultilineText>(L"Notes");
notes->set_maximum_length(65536);
notes->set_text(L"First paragraph\rSecond paragraph");
notes->on_change(save_notes);

auto dialog = std::make_shared<xui::ContentDialog>(L"Edit notes", notes);
dialog->on_validate(validate_notes); // Return an error message, or an empty string.
dialog->on_result(handle_result);
window.show_dialog(dialog, *anchor, notes.get());
```

`ContentDialog` uses the existing client-bound `Popup` and the root Direct2D target.
Owner controls remain disabled while the dialog is open. Tab stays within the top popup.
Enter invokes the default action outside multiline documents. Escape cancels after native composition ends.
Invalid content stays visible. Dismissal restores focus and revokes the old popup generation before result callbacks.
Validation callbacks can close or reopen the dialog. An obsolete validation result cannot close the new generation.
The UIA Window pattern reports modal state and supports Close.
Minimize, maximize, and process-idle waits are not operations of a client-bound dialog.
`Window::confirm` remains the separate native confirmation service.

`InlineStatus` has information, success, warning, and error severities.
Each severity has a visible symbol and an accessible prefix.
The action and dismiss button have separate callbacks. Errors use assertive live-region semantics. Other severities use polite semantics.
External UIA tests receive one live event per message change or re-show. Duplicate property writes remain silent.
Messages have a 4,096-code-unit limit. The badge recipe omits both buttons.
Actual screen-reader speech still requires a manual check.

`MultilineText` and `RichText` use the Windows `Msftedit.dll` RichEdit engine.
Windows owns composition, selection, caret movement, scrolling, clipboard operations, and undo.
`set_monospace(true)` selects Consolas for code. The default font remains Segoe UI.
The default document limit is 65,536 UTF-16 code units. The maximum is 1,048,576.
Paragraphs use `\r`. Setters normalize `\n` and `\r\n`, and reject null characters or unpaired surrogates.
`TextSelection` uses UTF-16 offsets. A property selection cannot split a surrogate pair.
Native surrogate-pair input publishes one complete value, not an intermediate half-character.
`TextCommand` supports undo, redo, copy, cut, paste, and select-all while a native peer is attached.
RichEdit retains at most 16 undo actions. Windows determines their byte cost.
Property changes replace text once per revision, not once per paint.
`MultilineText` also supports [undo-preserving range replacement](#undo-preserving-range-replacement).

`RichText::set_runs` accepts at most 4,096 runs with bold, italic, underline, and explicit HTTP/HTTPS link targets.
A click or Ctrl+Enter requests a link callback. XUI never opens the target automatically.
Native edits retain formatting. The retained runs track surviving application-authored styles and link positions.
They are not a general RTF serialization format for arbitrary native formatting commands.
Unicode streaming explicitly selects plain text. RTF-looking strings remain literal text.
Paste accepts plain Unicode only. The OLE callback rejects embedded objects and drag/drop effects.
There is no RTF, HTML, image, or file importer in this API.

RichEdit supplies its own server-side UIA Text provider, including text ranges and font attributes.
Replacing that provider hides its Text pattern, so XUI leaves it intact.
Its accessible name follows `Control::name`. Its automation ID remains the native numeric control ID.
The custom `automation_id` override applies to the other document controls, not RichEdit.
Native focus HRESULTs are not proof of focus. Tests inspect actual focus and the disabled owner state.
Native EDIT and RichEdit remain Windows composition boundaries, not a new XUI TSF implementation.

`PasswordInput` defaults to 256 code units and permits at most 4,096.
`with_password` is the explicit application read boundary. Change callbacks carry no password value.
The native editor always keeps `ES_PASSWORD`. Copy, cut, and its context menu are disabled.
UIA reports `IsPassword`; value reads are empty or unavailable, including during reveal.
The default reveal policy is `never`.
`explicit_request` permits a separate noninteractive preview, while the native editor remains masked and keeps its real caret and selection behavior.
Focus loss or hiding removes that preview. It does not create an accessible plaintext value.
XUI erases the retained old value on replacement and destruction.
This is not a credential vault. Windows and application code can retain plaintext in process memory.

`DateTimePicker` has date, time, and calendar presentations.
The native date editor includes its calendar dropdown. Windows supplies locale formats and platform UIA behavior.
Values use local Gregorian fields, not UTC instants or time zones.
The supported year range is 1601–9999. Invalid dates and ranges that exclude the current value throw.
Date edits preserve the time fields. Time edits preserve the date fields.
Native calendar styling follows Windows, not the custom dark palette.
The native calendar background is initialized before bitmap composition, including unused margins.

`ColorPicker` stores unpremultiplied sRGB bytes in `RgbaColor`.
Four labeled numeric editors expose RangeValue and keyboard steps.
Fractional channel edits round to the nearest byte. Alpha changes the checkerboard preview.
Up to 16 swatches have distinct RGBA names and Invoke actions.
Swatch replacement reuses retained buttons. Color controls create no idle timer or image assets.

Native document pixels join the existing root frame before `EndDraw`.
See [the composition evidence](../llm/rendering-history.md#document-composition-evidence) for capture methods and remaining manual coverage.

## Undo-preserving range replacement

`DocumentText::replace_range(range, expected_text, replacement)` applies one semantic edit to an attached plain `MultilineText`.
The method returns a collapsed `TextSelection` immediately after the inserted text.
The model and native selection contain this result before the change callback runs.
Each successful call reports one change callback and creates one native undo action.
Earlier undo actions remain available within the existing 16-action limit.
The operation does not change the clipboard or simulate input.

```cpp
const auto snapshot = notes->text();
const auto caret = notes->replace_range({0, 5}, snapshot, L"Updated");
notes->command(xui::TextCommand::undo);
notes->command(xui::TextCommand::redo);
```

`expected_text` must exactly match the complete current document, not only the replaced range.
Its paragraphs use native CR separators.
The half-open range uses UTF-16 offsets into that snapshot.
Native typing anywhere in the document invalidates a stale snapshot.
XUI compares both the model and the native text before it changes the selection.
An unapplied text-property revision also rejects the operation, even if its text matches.

Replacement text accepts LF, CRLF, and CR separators.
XUI normalizes these separators to CR before it checks the resulting document length.
The replacement input cannot exceed 1,048,576 UTF-16 units before normalization.
The resulting document must fit `maximum_length()`.
Embedded NUL, unpaired surrogates, invalid ranges, split surrogate pairs, and unchanged replacements are errors.
An empty replacement deletes the range. An empty range inserts text.

Detached, hidden, disabled, read-only, and composing editors reject range replacement.
The native owner must also be enabled.
These precondition errors leave text, selection, callbacks, and undo history unchanged.
Calls use the creating UI thread and do not force pending text properties into the native editor.
Reentrant range replacement rejects the call during a change callback.
Document commands return false during that callback.

The callback runs after the native transaction returns.
It can close the owner or detach the document.
A callback exception does not roll back the completed edit.
Native execution failures report an error rather than a successful edit.
`RichText` rejects this operation because retained authored runs do not serialize all native formatting.
Existing text setters remain silent and retain their whole-document replacement behavior.

### C and C#

`xui_document_replace_range` is an additive C ABI function declared through `xui.h`.
It accepts strict UTF-8 strings and UTF-16 range offsets.
Each string retains the ABI limit of 1,048,576 bytes.
The two distinct output pointers receive the resulting selection only on success.
Invalid inputs return `XUI_INVALID_ARGUMENT`.
Stale or unavailable editors return `XUI_BUSY`, with an explanatory last error.
Existing handle, thread, closed-window, native-error, and callback-error statuses remain applicable.

```csharp
string snapshot = editor.Text;
TextSelection caret = editor.ReplaceRange(new(0, 5), snapshot, "Updated");
editor.Command(TextCommand.Undo);
editor.Command(TextCommand.Redo);
```

`MultilineText.ReplaceRange` returns the selection or throws through the existing C# error boundary.
Malformed strings also retain the managed string-validation errors.
`editor.Selection` remains available for explicit selection changes.
There is no Rust convenience wrapper for this additive function.

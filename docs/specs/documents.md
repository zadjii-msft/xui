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

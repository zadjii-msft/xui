using System.Runtime.InteropServices;

namespace Xui;

internal static partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_text_input_selection_get")]
    internal static partial int TextInputSelectionGet(ulong input, out ulong start, out ulong end);

    [LibraryImport("xui", EntryPoint = "xui_text_input_selection_set")]
    internal static partial int TextInputSelectionSet(ulong input, ulong start, ulong end);
}

public sealed partial class TextInput
{
    /// <summary>
    /// UTF-16 selection offsets. Endpoints clamp to the text and are ordered.
    /// A caret inside a surrogate pair moves left; a range includes the whole pair.
    /// Changes apply to the native editor immediately without changing focus.
    /// </summary>
    public TextSelection Selection
    {
        get
        {
            Window.Guard();
            Window.Check(Native.TextInputSelectionGet(Handle, out ulong start, out ulong end));
            return new(start, end);
        }
        set
        {
            Window.Guard();
            Window.Check(Native.TextInputSelectionSet(Handle, value.Start, value.End));
        }
    }

    public TextInput SetSelection(TextSelection selection) { Selection = selection; return this; }
}

using System.Runtime.InteropServices;

namespace Xui;

internal static unsafe partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_document_line_numbers")]
    internal static partial int DocumentLineNumbers(ulong document, uint enabled);
    [LibraryImport("xui", EntryPoint = "xui_document_replace_range")]
    internal static partial int DocumentReplaceRange(ulong document, ulong start, ulong end,
        Text expectedText, Text replacement, ulong* selectionStart, ulong* selectionEnd);
}

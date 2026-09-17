using System.Runtime.InteropServices;

namespace Xui;

public sealed unsafe partial class MultilineText
{
    public static bool SyntaxHighlightingAvailable
    {
        get
        {
            uint available;
            Window.CheckStatus(Native.SyntaxHighlightingAvailable(&available));
            return available != 0;
        }
    }

    public MultilineText SetSyntaxLanguage(string language)
    {
        Window.Guard();
        ArgumentNullException.ThrowIfNull(language);
        using var pins = new Window.Pins();
        Window.Check(Native.DocumentSyntaxLanguage(Handle, pins.Text(language)));
        return this;
    }

    public MultilineText SetSyntaxPath(string path)
    {
        Window.Guard();
        ArgumentNullException.ThrowIfNull(path);
        using var pins = new Window.Pins();
        Window.Check(Native.DocumentSyntaxPath(Handle, pins.Text(path)));
        return this;
    }
}

internal static unsafe partial class Native
{
    [LibraryImport("xui", EntryPoint = "xui_syntax_highlighting_available")]
    internal static partial int SyntaxHighlightingAvailable(uint* available);
    [LibraryImport("xui", EntryPoint = "xui_document_syntax_language")]
    internal static partial int DocumentSyntaxLanguage(ulong document, Text language);
    [LibraryImport("xui", EntryPoint = "xui_document_syntax_path")]
    internal static partial int DocumentSyntaxPath(ulong document, Text path);
}

using Xui;
internal static class ShellPreviewTests
{
    public static void Run()
    {
        using var window = new Window();
        var preview = window.ShellPreview("Windows preview");
        Check(preview.Status.State == PreviewState.Idle);
        int calls = 0;
        preview.Changed += status => { Check(status.Generation == preview.Status.Generation); calls++; };
        ulong first = preview.LoadLocal(@"C:\locally-authored.txt");
        ulong second = preview.LoadLocal(@"C:\replacement.txt");
        Check(second > first && calls == 2);
        preview.Cancel(first);
        Check(preview.Status.Generation == second && calls == 2);
        preview.Cancel(second);
        Check(preview.Status.State == PreviewState.Idle && calls == 3);
        Console.WriteLine("ShellPreview managed generation and callback tests passed.");
    }
    private static void Check(bool value) { if (!value) throw new InvalidOperationException("ShellPreview assertion failed."); }
}

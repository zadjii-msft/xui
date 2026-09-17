namespace Xui.Designer;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        string? fileSmokeDirectory = null;
        try
        {
            if (args.Length > 1 || (args.Length == 1 && args[0].StartsWith("--") && args[0] is not ("--smoke" or "--builder-smoke" or "--file-smoke" or "--file-close-smoke" or "--selection-smoke")))
                throw new ArgumentException("Usage: Designer.exe [trusted-file.xui | --smoke | --builder-smoke | --file-smoke | --file-close-smoke | --selection-smoke]");
            if (args.Contains("--file-smoke") || args.Contains("--file-close-smoke"))
                fileSmokeDirectory = Path.Combine(Path.GetTempPath(), "XuiDesignerFileSmoke-" + Guid.NewGuid().ToString("N"));
            using var app = new DesignerApplication(args.FirstOrDefault() is { } path && !path.StartsWith("--") ? path : null,
                fileSmokeDirectory is null ? null : Path.Combine(fileSmokeDirectory, "Drafts"));
            app.Run(args.Contains("--smoke"), args.Contains("--builder-smoke"), fileSmokeDirectory,
                fileCloseOnly: args.Contains("--file-close-smoke"),
                selectionSmoke: args.Contains("--selection-smoke"));
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
        finally
        {
            if (fileSmokeDirectory is not null && Directory.Exists(fileSmokeDirectory))
                Directory.Delete(fileSmokeDirectory, recursive: true);
        }
    }
}

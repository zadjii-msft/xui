namespace Xui.Designer;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length > 1 || (args.Length == 1 && args[0].StartsWith("--") && args[0] != "--smoke"))
                throw new ArgumentException("Usage: Designer.exe [trusted-file.xui | --smoke]");
            using var app = new DesignerApplication(args.FirstOrDefault() is { } path && path != "--smoke" ? path : null);
            app.Run(args.Contains("--smoke"));
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}

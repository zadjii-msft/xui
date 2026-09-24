using PortableDemo;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 0)
            {
                using var window = new Xui.Window("XUI order builder", 560, 760);
                _ = new OrderBuilder(window);
                window.Run();
            }
            else if (args is ["--smoke"]) OrderSmoke.Run();
            else throw new ArgumentException("Usage: WindowsOrderDemo [--smoke]");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}

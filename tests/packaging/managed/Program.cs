using System.Runtime.InteropServices;

internal static class Program
{
    [DllImport("xui", CallingConvention = CallingConvention.Cdecl)]
    private static extern uint xui_abi_version();

    private static int Main()
    {
        Console.WriteLine(typeof(PackageFixture.Counter).FullName);
        Console.WriteLine(typeof(Xui.Window).Assembly.GetName().Name);
        return xui_abi_version() == 0x00010000u ? 0 : 1;
    }
}

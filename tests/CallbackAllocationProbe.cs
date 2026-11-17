using System.Runtime.CompilerServices;
using System.Text.Json;
using Xui;

internal static class CallbackAllocationProbe
{
    private const int Count = 256;
    private static readonly Action<Window>[] Steps =
    [
        static window => { },
        static window => { window.Button("Census").Click += static () => { }; },
        static window => { window.KeyHandler = static _ => false; },
        static window => { window.NavigationHandler = static _ => false; },
        static window => { window.IconErrorHandler = static _ => { }; }
    ];
    private static readonly string[] Names = ["bare-window", "observed-button", "key", "navigation", "icon"];

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static long Lifetime()
    {
        long before = GC.GetAllocatedBytesForCurrentThread();
        using var lifetime = new CallbackLifetime();
        return GC.GetAllocatedBytesForCurrentThread() - before;
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static long Registration(CallbackLifetime lifetime, object target)
    {
        long before = GC.GetAllocatedBytesForCurrentThread();
        using var registration = lifetime.Register(target, static _ => { });
        return GC.GetAllocatedBytesForCurrentThread() - before;
    }
    [STAThread]
    private static int Main()
    {
        try
        {
            using var lifetime = new CallbackLifetime();
            var target = new object();
            for (int i = 0; i < 32; ++i) { Lifetime(); Registration(lifetime, target); }
            long lifetimeBytes = 0, registrationBytes = 0;
            for (int i = 0; i < Count; ++i)
            {
                lifetimeBytes += Lifetime();
                registrationBytes += Registration(lifetime, target);
            }
            Console.WriteLine(JsonSerializer.Serialize(new { phase = "registration-storage", iterations = Count,
                lifetime_bytes = lifetimeBytes, typed_registration_bytes = registrationBytes }));
            for (int step = 0; step < Steps.Length; ++step)
            {
                for (int i = 0; i < 32; ++i) { using var warm = new Window(); Steps[step](warm); }
                long construction = 0, setup = 0, disposal = 0;
                for (int i = 0; i < Count; ++i)
                {
                    long before = GC.GetAllocatedBytesForCurrentThread();
                    var window = new Window();
                    construction += GC.GetAllocatedBytesForCurrentThread() - before;
                    before = GC.GetAllocatedBytesForCurrentThread();
                    Steps[step](window);
                    setup += GC.GetAllocatedBytesForCurrentThread() - before;
                    before = GC.GetAllocatedBytesForCurrentThread();
                    window.Dispose();
                    disposal += GC.GetAllocatedBytesForCurrentThread() - before;
                }
                Console.WriteLine(JsonSerializer.Serialize(new { phase = Names[step], iterations = Count,
                    construction_bytes = construction, setup_bytes = setup, disposal_bytes = disposal }));
            }
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}

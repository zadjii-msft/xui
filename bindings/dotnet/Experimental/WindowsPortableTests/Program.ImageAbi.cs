using System.Reflection;
using System.Runtime.InteropServices;
using Xui;

internal static partial class Program
{
    private static void ImageAbiContracts()
    {
        var assembly = typeof(Window).Assembly;
        var options = assembly.GetType("Xui.Native+ImageMemoryOptionsValue", throwOnError: true)!;
        var state = assembly.GetType("Xui.Native+ImageMemoryStateValue", throwOnError: true)!;
        var statistics = assembly.GetType("Xui.Native+ImageMemoryStatisticsValue", throwOnError: true)!;
        Check(Marshal.SizeOf(options) == 48 && Marshal.OffsetOf(options, "Generation").ToInt64() == 8 &&
            Marshal.OffsetOf(options, "Format").ToInt64() == 16 && Marshal.OffsetOf(options, "Reserved").ToInt64() == 44,
            "The memory Image options ABI does not match the native 48-byte contract.");
        Check(Marshal.SizeOf(state) == 40 && Marshal.OffsetOf(state, "Generation").ToInt64() == 8 &&
            Marshal.OffsetOf(state, "Status").ToInt64() == 16 && Marshal.OffsetOf(state, "Reserved").ToInt64() == 36,
            "The memory Image state ABI does not match the native 40-byte contract.");
        Check(Marshal.SizeOf(statistics) == 32 && Marshal.OffsetOf(statistics, "Bytes").ToInt64() == 8 &&
            Marshal.OffsetOf(statistics, "Limit").ToInt64() == 24,
            "The memory Image statistics ABI does not match the native 32-byte contract.");
        var decode = typeof(MemoryImageRequest).GetMethod("Decode", BindingFlags.NonPublic | BindingFlags.Static)!;
        void Set(object value, string field, object data) =>
            state.GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(value, data);
        object Value()
        {
            var value = Activator.CreateInstance(state)!;
            Set(value, "Size", 40u); Set(value, "Version", 0x10000u);
            Set(value, "Generation", 1ul); Set(value, "Status", 2u);
            Set(value, "SourceWidth", 31u); Set(value, "SourceHeight", 17u);
            Set(value, "PixelWidth", 16u); Set(value, "PixelHeight", 8u);
            return value;
        }
        MemoryImageState Decode(object value)
        {
            try { return (MemoryImageState)decode.Invoke(null, [value])!; }
            catch (TargetInvocationException error) when (error.InnerException is not null)
            {
                System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error.InnerException).Throw();
                throw;
            }
        }
        Check(Decode(Value()) == new MemoryImageState(1, MemoryImageStatus.Ready, 31, 17, 16, 8),
            "Native Image state decoding lost original or decoded dimensions.");
        var fatal = Value();
        Set(fatal, "Status", 4u);
        Check(Decode(fatal).Status == MemoryImageStatus.PresentationFailed,
            "Native Image presentation failure was conflated with ordinary loading failure.");
        foreach (var (field, invalid) in new (string, object)[]
        {
            ("Size", 39u), ("Version", 0u), ("Generation", 0ul), ("Generation", (ulong)long.MaxValue + 1),
            ("Status", 5u), ("Reserved", 1u), ("SourceWidth", 16385u), ("SourceWidth", 0u),
            ("PixelWidth", 1025u), ("PixelHeight", 0u)
        })
        {
            var value = Value();
            Set(value, field, invalid);
            Throws<InvalidOperationException>(() => Decode(value));
        }
        var empty = Activator.CreateInstance(state)!;
        Set(empty, "Size", 40u); Set(empty, "Version", 0x10000u);
        Check(Decode(empty) == new MemoryImageState(0, MemoryImageStatus.Empty, 0, 0, 0, 0),
            "Initial empty native Image state did not decode.");
    }
}

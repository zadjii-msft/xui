using Xui.Experimental.Portable;

internal static partial class Program
{
    private static int assertions;

    private static void Main()
    {
        ValueChecks();
        GeometryChecks();
        RuntimeChecks();
        MotionPolicyChecks();
        GeneratedChecks();
        Console.WriteLine($"Retained reveal: {assertions} assertions passed.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void ValueChecks()
    {
        Assert(RevealMotion.Default == new RevealMotion() &&
            RevealMotion.Default.DurationMilliseconds == 0 && RevealMotion.Default.Direction == RevealDirection.Bottom,
            "The default reveal makes no motion request.");
        foreach (uint duration in new uint[] { 0, 1, 180, 400 })
            foreach (var direction in Enum.GetValues<RevealDirection>())
            {
                var motion = new RevealMotion(duration, direction);
                Assert(motion.DurationMilliseconds == duration && motion.Direction == direction &&
                    motion == new RevealMotion(duration, direction),
                    "Bounded motion preserves the exact requested duration and edge with value equality.");
            }
        Throws<ArgumentOutOfRangeException>(() => new RevealMotion(401));
        Throws<ArgumentOutOfRangeException>(() => new RevealMotion(uint.MaxValue));
        Throws<ArgumentOutOfRangeException>(() => new RevealMotion(direction: (RevealDirection)(-1)));
        Throws<ArgumentOutOfRangeException>(() => new RevealMotion(direction: (RevealDirection)2));
        Assert(new RevealMotion(180, RevealDirection.Bottom) != new RevealMotion(180, RevealDirection.Right) &&
            new RevealMotion(180) != new RevealMotion(0),
            "Different native direction or duration requests are not conflated.");
        Assert(new RevealPresentation(0.25f, true).Progress == 0.25f &&
            new RevealPresentation(0.25f, true).Animating &&
            default(RevealPresentation) == new RevealPresentation(0, false),
            "Presentation snapshots retain actual intermediate native values rather than a logical open boolean.");
    }
}

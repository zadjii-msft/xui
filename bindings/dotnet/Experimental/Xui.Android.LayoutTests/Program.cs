using Xui.Experimental.Android;

int assertions = 0;
void Assert(bool condition, string description)
{
    if (!condition) throw new Exception(description);
    assertions++;
}
void Throws(Action action)
{
    try { action(); }
    catch (ArgumentException) { assertions++; return; }
    throw new Exception("Expected an argument error.");
}
void Allocation(int available, int spacing, int[] desired, float[] weights, int[] expected) =>
    Assert(LayoutMath.Allocate(available, spacing, desired, weights).SequenceEqual(expected), "Exact pixel allocation.");

Assert(LayoutMath.Pixels(16, 3) == 48, "DIP conversion at xxxhdpi.");
Assert(LayoutMath.Pixels(1, 1.5f) == 2, "Fractional density rounding.");
Assert(LayoutMath.Pixels(0, 2) == 0, "Zero dimensions.");
Assert(LayoutMath.Pixels(float.MaxValue, 4) == LayoutMath.MaxDimension, "Saturating Android measurement.");
Assert(LayoutMath.Sum(LayoutMath.MaxDimension, 42) == LayoutMath.MaxDimension, "Saturating dimension sum.");
Throws(() => LayoutMath.Pixels(-1, 1));
Throws(() => LayoutMath.Pixels(float.NaN, 1));
Throws(() => LayoutMath.Pixels(1, 0));
Throws(() => LayoutMath.Pixels(1, float.PositiveInfinity));
Allocation(100, 10, [20, 20, 20], [0, 1, 1], [20, 30, 30]);
Allocation(101, 0, [0, 0], [1, 1], [50, 51]);
Allocation(90, 0, [30, 30], [1, 2], [30, 60]);
Allocation(40, 5, [30, 30], [0, 0], [30, 5]);
Allocation(100, 10, [20, 20], [0, 0], [20, 20]);
Allocation(0, 0, [20], [1], [0]);
Allocation(5, 100, [20, 20], [0, 1], [0, 0]);
Allocation(100, int.MaxValue, [20, 20, 20], [1, 1, 1], [0, 0, 0]);
Allocation(100, 0, [20, 20], [float.MaxValue, float.MaxValue], [50, 50]);
Allocation(100, 0, [], [], []);
Throws(() => LayoutMath.Allocate(-1, 0, [], []));
Throws(() => LayoutMath.Allocate(10, 0, [1], []));
Throws(() => LayoutMath.Allocate(10, 0, [1], [float.NaN]));
Throws(() => LayoutMath.Allocate(10, 0, [-1], [0]));

var random = new Random(3107);
for (int trial = 0; trial < 1000; trial++)
{
    int available = random.Next(0, 2000);
    int spacing = random.Next(0, 30);
    int count = random.Next(1, 12);
    int[] desired = Enumerable.Range(0, count).Select(_ => random.Next(0, 500)).ToArray();
    float[] weights = Enumerable.Range(0, count).Select(_ => (float)random.Next(0, 5)).ToArray();
    int[] result = LayoutMath.Allocate(available, spacing, desired, weights);
    int content = Math.Max(0, available - spacing * (count - 1));
    Assert(result.All(value => value >= 0), "Allocations stay nonnegative.");
    Assert(result.Sum() <= content, "Children cannot exceed the bounded content allocation.");
    if (weights.Any(weight => weight > 0)) Assert(result.Sum() == content, "Flex consumes the remaining pixels.");
    for (int i = 0; i < count; i++)
        if (weights[i] == 0) Assert(result[i] <= desired[i], "Non-flex children do not grow on the main axis.");
}
Console.WriteLine($"Android layout arithmetic: {assertions} assertions passed. No Android widgets ran.");

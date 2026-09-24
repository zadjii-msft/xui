using Xui.Experimental.Portable;

int assertions = 0;
void Check(bool value, string message)
{
    if (!value) throw new InvalidOperationException(message);
    assertions++;
}
void Reject(Action action)
{
    try { action(); }
    catch (ArgumentException) { assertions++; return; }
    throw new InvalidOperationException("Expected invalid geometry to be rejected.");
}

Check(VirtualizationMath.Extent(10000, 128) == 1280000, "Full extent.");
Check(VirtualizationMath.ClampOffset(6396, 183.2986297607422f, float.MaxValue, 282.44940185546875f) == 1172095.5f,
    "Offset normalization uses the same rounded finite extent advertised to native peers.");
Check(VirtualizationMath.Visible(10000, 128, 0, 512, 2) == new VirtualRange(0, 6), "Start boundary.");
Check(VirtualizationMath.Visible(10000, 128, 128, 512, 0) == new VirtualRange(1, 5), "Exact half-open boundary.");
Check(VirtualizationMath.Visible(10000, 128, 129, 512, 0) == new VirtualRange(1, 6), "Partial rows.");
Check(VirtualizationMath.Visible(10000, 128, float.MaxValue, 512, 2) == new VirtualRange(9994, 10000), "End clamping.");
Check(VirtualizationMath.Visible(10000, 128, 0, 0, 2).Count == 0, "Zero viewport.");
Check(VirtualizationMath.Visible(0, 128, 0, 512, 2).Count == 0, "Empty source.");
Check(VirtualizationMath.Visible(int.MaxValue, 1, 100, 10, int.MaxValue) == new VirtualRange(0, int.MaxValue), "Overscan saturation.");
Check(VirtualizationMath.Reveal(10000, 128, 9999, 0, 512) == 1279488, "Reveal final item.");
Check(VirtualizationMath.Reveal(10000, 128, 3, 0, 512) == 0, "Already visible.");
Check(VirtualizationMath.Reveal(10000, 128, 3, 0, 64) == 384, "Oversized row top aligns.");

foreach (float value in new[] { float.NaN, float.PositiveInfinity, float.NegativeInfinity, -1 })
{
    Reject(() => VirtualizationMath.Extent(1, value));
    Reject(() => VirtualizationMath.Visible(1, 1, value, 1, 1));
    Reject(() => VirtualizationMath.Visible(1, 1, 0, value, 1));
}
Reject(() => VirtualizationMath.Extent(1, 0));
Reject(() => VirtualizationMath.Extent(-1, 1));
Reject(() => VirtualizationMath.Extent(2, float.MaxValue));
Reject(() => VirtualizationMath.Visible(1, 1, 0, 1, -1));
Reject(() => VirtualizationMath.Reveal(1, 1, 1, 0, 1));
Reject(() => VirtualizationMath.Runs(10, new(8, 7), []));
Reject(() => VirtualizationMath.Runs(10, new(0, 1), [10]));
Reject(() => new VirtualViewportRect(0, float.NaN, 100, 1000).Validate());
Reject(() => new VirtualViewportRect(0, 100, -1, 1000).Validate());
Reject(() => new VirtualViewportRequest(0, 0, 1, default, default, false).Validate());
Reject(() => new VirtualViewportRequest(1, 2, 1, default, default, false).Validate());
new VirtualItemInfo(new string('a', 4096), 0, 1, 1).Validate();
new VirtualItemInfo(new string('a', 4094) + "\ud83e\udd8a", 0, 1, 1).Validate();
Reject(() => new VirtualItemInfo(new string('a', 4097), 0, 1, 1).Validate());
Reject(() => new VirtualItemInfo(new string('a', 4095) + "\ud83e\udd8a", 0, 1, 1).Validate());
Reject(() => new VirtualItemInfo("\ud800", 0, 1, 1).Validate());
Reject(() => new VirtualItemInfo("\udc00", 0, 1, 1).Validate());
Reject(() => new VirtualItemInfo("\ud800x", 0, 1, 1).Validate());
Reject(() => new VirtualItemInfo("bad\0key", 0, 1, 1).Validate());
Reject(() => new VirtualItemInfo("key", 1, 1, 1).Validate());
var ordinaryKey = KeyedItem.Create<IPortableComponent>(new string('x', 4097), _ => throw new InvalidOperationException("Unused factory."));
Check(ordinaryKey.Key.Length == 4097, "Ordinary keyed components retain their existing key contract.");
var perfOffsets = PortableDemo.VirtualListPerformance.RequestOffsets(10000, 128, 512);
Check(perfOffsets.Count == 110 && perfOffsets[1] - perfOffsets[0] == 128 &&
    Math.Abs(perfOffsets[2] - perfOffsets[1]) > 512, "Native performance workload alternates neighbor and far requests.");
var perf = PortableDemo.VirtualListPerformance.Evaluate(
    Enumerable.Repeat(10000d, 10).Concat(Enumerable.Range(1, 100).Select(value => (double)value)).ToArray());
Check(perf.P50Milliseconds == 50 && perf.P95Milliseconds == 95 && perf.MaximumMilliseconds == 100 &&
    perf.SamplesMilliseconds.Count == 100 && !perf.MeetsInitialBudget,
    "Performance report discards exactly ten warmups and reports nearest-rank percentiles without hiding the maximum.");
Check(PortableDemo.VirtualListPerformance.Evaluate(Enumerable.Repeat(50d, 110).ToArray()).MeetsInitialBudget,
    "Initial p95 budget accepts its exact 50ms boundary.");
Reject(() => PortableDemo.VirtualListPerformance.Evaluate(new double[109]));
Reject(() => PortableDemo.VirtualListPerformance.Evaluate(Enumerable.Repeat(double.NaN, 110).ToArray()));
Check(VirtualizationMath.Runs(10000, new(500, 506), [0, 9999, 502, 0]).SequenceEqual(
    [new VirtualRun(0, 1, false), new(1, 499, true), new(500, 6, false), new(506, 9493, true), new(9999, 1, false)]),
    "Far pins do not mount intervening rows.");

var random = new Random(607);
for (int test = 0; test < 10000; test++)
{
    int count = random.Next(1, 500);
    int start = random.Next(count);
    int end = random.Next(start, count + 1);
    int[] pins = Enumerable.Range(0, random.Next(5)).Select(_ => random.Next(count)).ToArray();
    var runs = VirtualizationMath.Runs(count, new(start, end), pins);
    var expected = Enumerable.Range(start, end - start).Concat(pins).Distinct().Order().ToArray();
    var actual = runs.Where(run => !run.IsGap).SelectMany(run => Enumerable.Range(run.Start, run.Count));
    Check(actual.SequenceEqual(expected), "Sparse run membership.");
    Check(runs.Sum(run => run.Count) == count, "Sparse runs cover complete extent.");
    Check(runs.All(run => run.Count > 0), "No empty runs.");
    Check(runs.Zip(runs.Skip(1)).All(pair => pair.First.Start + pair.First.Count == pair.Second.Start &&
        pair.First.IsGap != pair.Second.IsGap), "Ordered nonoverlapping merged runs.");
}
Console.WriteLine($"Portable virtualization interval math: {assertions} assertions passed.");
ControllerTests.Run();

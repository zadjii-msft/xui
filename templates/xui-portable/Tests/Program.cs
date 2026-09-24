using PortableApp;
using Xui.Experimental.Portable;

var initial = new CounterState();
if (initial.Greeting != "Hello, friend!" || initial.Increment().Count != 1 || initial.Count != 0)
    throw new InvalidOperationException("Counter model regression.");
var named = initial with { Name = "Ada" };
if (named.Greeting != "Hello, Ada!" || named.Increment().Name != "Ada")
    throw new InvalidOperationException("Named counter model regression.");

using var host = new Host(new TestDispatcher());
var counter = new Counter(host);
var input = counter.Input;
counter.State = named.Increment();
if (!ReferenceEquals(input, counter.Input) || input.Text != "Ada")
    throw new InvalidOperationException("Shared component state or retained input regression.");
Console.WriteLine("Shared model and portable component checks passed.");

sealed class TestDispatcher : IUiDispatcher
{
    private readonly int threadId = Environment.CurrentManagedThreadId;
    public bool CheckAccess() => Environment.CurrentManagedThreadId == threadId;
    public void Post(Action action) => throw new NotSupportedException("This synchronous fixture has no UI queue.");
}

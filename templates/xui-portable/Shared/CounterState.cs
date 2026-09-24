namespace PortableApp;

public sealed record CounterState(int Count = 0, string Name = "")
{
    public string Greeting => string.IsNullOrWhiteSpace(Name) ? "Hello, friend!" : $"Hello, {Name}!";
    public CounterState Increment() => this with { Count = checked(Count + 1) };
}

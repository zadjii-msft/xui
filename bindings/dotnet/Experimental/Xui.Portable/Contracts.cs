namespace Xui.Experimental.Portable;

public enum Axis { Horizontal, Vertical }
public enum ElementKind { Stack, Label, Button, TextInput, ScrollView }
public enum ElementProperty
{
    Text, Name, AutomationId, Enabled, Visible, Help,
    Spacing, Padding, FixedSize, PreferredSize, CaptionVisible, Placeholder
}

public readonly record struct Size(float Width, float Height);

public interface IUiDispatcher
{
    bool CheckAccess();
    void Post(Action action);
}

public interface IControlEvents
{
    bool Click();
    bool Change(string text);
    bool Submit();
}

public interface IElementPeer : IDisposable
{
    void AddChild(IElementPeer child);
    void Update(ElementProperty property);
}

public interface IBackend : IDisposable
{
    IElementPeer Create(Element element, IControlEvents events);
    void Mount(IElementPeer root);
}

internal static class Values
{
    internal static string Text(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.Contains('\0')) throw new ArgumentException("Text cannot contain NUL.", nameof(value));
        return value;
    }

    internal static float Length(float value)
    {
        if (!float.IsFinite(value) || value < 0)
            throw new ArgumentOutOfRangeException(nameof(value), "A length or flex weight must be finite and nonnegative.");
        return value;
    }

    internal static Size Size(float width, float height) => new(Length(width), Length(height));
}

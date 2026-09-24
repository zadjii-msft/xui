namespace Xui.Experimental.Portable;

public enum Axis { Horizontal, Vertical }
public enum ElementKind { Stack, Label, Button, TextInput, ScrollView, Toggle, CheckBox, Progress, Grid, MultilineText, PasswordInput, SingleChoice, RangeInput, PageView, TabStrip, NavigationView, Image, Reveal }
public enum CheckState : uint { Unchecked, Checked, Indeterminate }
public enum ProgressState : uint { Determinate, Indeterminate }
public enum ElementProperty
{
    Text, Name, AutomationId, Enabled, Visible, Help,
    Spacing, Padding, FixedSize, PreferredSize, CaptionVisible, Placeholder,
    Checked, CheckState, ThreeState, Range, Value, ProgressState, Constraints, Tracks, Typography, ReadOnly, Choices, Pages, Closable, Expanded, TextLayout, ImageRequest, RevealState
}

public readonly record struct Size(float Width, float Height);
public readonly record struct NumericRange(double Minimum, double Maximum, double SmallStep = 1, double LargeStep = 10);

public interface IUiDispatcher
{
    bool CheckAccess();
    void Post(Action action);
}

public interface ICancellableUiDispatcher : IUiDispatcher
{
    void Post(Action action, Action<Exception> canceled);
}

public interface IControlEvents
{
    bool Click();
    bool Change(string text);
    bool Submit();
}

public interface IValueControlEvents : IControlEvents
{
    bool ToggleChanged(bool value);
    bool CheckChanged(CheckState value);
}

public interface IElementPeer : IDisposable
{
    void AddChild(IElementPeer child);
    void Update(ElementProperty property);
}

public interface IConstrainedElementPeer : IElementPeer { }

public interface IMutableElementPeer : IElementPeer
{
    void InsertChild(int index, IElementPeer child);
    void RemoveChild(IElementPeer child);
    void ValidateMove(IElementPeer child, int index);
    void MoveChild(IElementPeer child, int index);
}

public interface IMutationPreflightPeer : IMutableElementPeer
{
    void ValidateMutation();
}

public interface IPortableComponent
{
    Element Root { get; }
}

public interface IBackend : IDisposable
{
    IElementPeer Create(Element element, IControlEvents events);
    void Mount(IElementPeer root);
}

public interface IBackendTreePreflight : IBackend
{
    void ValidateTree(Element root);
    void ValidateInsertion(Element parent, Element candidateRoot);
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

namespace Xui.Experimental.Portable;

public enum InputPurpose { Normal, Email, Url, Telephone, Number }

public interface IInputPurposeElementPeer : IElementPeer { }
public delegate void PasswordReceiver(ReadOnlySpan<char> password);

public interface IPasswordElementPeer : IElementPeer
{
    int PasswordLength { get; }
    void SetPassword(ReadOnlySpan<char> password);
    void WithPassword(PasswordReceiver receiver);
    void ClearPassword();
}

public interface IPasswordControlEvents : IControlEvents
{
    bool PasswordChanged();
}

public static class FormValues
{
    public static string NormalizeMultiline(string text, int maximumLength = 65536)
    {
        ArgumentNullException.ThrowIfNull(text);
        ValidateMaximum(maximumLength, 1048576);
        ValidateUtf16(text, password: false);
        string normalized = text.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n');
        if (normalized.Length > maximumLength)
            throw new ArgumentException("The document exceeds its UTF-16 length limit.", nameof(text));
        return normalized;
    }

    public static void ValidatePassword(ReadOnlySpan<char> password, int maximumLength = 256)
    {
        ValidateMaximum(maximumLength, 4096);
        if (password.Length > maximumLength) throw new ArgumentException("The password exceeds its UTF-16 length limit.", nameof(password));
        ValidateUtf16(password, password: true);
    }

    internal static void ValidateMaximum(int length, int maximum)
    {
        if (length < 1 || length > maximum) throw new ArgumentOutOfRangeException(nameof(length), $"The maximum length must be between 1 and {maximum} UTF-16 code units.");
    }

    private static void ValidateUtf16(ReadOnlySpan<char> text, bool password)
    {
        for (int i = 0; i < text.Length; i++)
        {
            char value = text[i];
            if (value == '\0' || (password && value is '\r' or '\n'))
                throw new ArgumentException(password ? "A password cannot contain NUL or line breaks." : "A document cannot contain NUL.");
            if (char.IsHighSurrogate(value))
            {
                if (i + 1 >= text.Length || !char.IsLowSurrogate(text[++i])) throw new ArgumentException("Text must contain complete UTF-16 scalar sequences.");
            }
            else if (char.IsLowSurrogate(value)) throw new ArgumentException("Text must contain complete UTF-16 scalar sequences.");
        }
    }
}

public sealed class MultilineText : Control
{
    private string text = "";
    private bool readOnly;
    private Action<string>? changed;
    public int MaximumLength { get; }
    public string Text
    {
        get => Read(text);
        set => Set(ref text, FormValues.NormalizeMultiline(value, MaximumLength), ElementProperty.Text);
    }
    public bool ReadOnly { get => Read(readOnly); set => Set(ref readOnly, value, ElementProperty.ReadOnly); }
    public MultilineText SetReadOnly(bool value) { ReadOnly = value; return this; }
    public event Action<string> Changed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); changed += value; }
        remove { Owner.VerifyEventRemoval(this); changed -= value; }
    }
    internal MultilineText(Host host, string name, int maximumLength) : base(host, ElementKind.MultilineText, name)
    {
        FormValues.ValidateMaximum(maximumLength, 1048576);
        MaximumLength = maximumLength;
    }
    internal void RaiseChange(string value)
    {
        string normalized = FormValues.NormalizeMultiline(value, MaximumLength);
        if (text == normalized) return;
        text = normalized;
        changed?.Invoke(normalized);
    }
    internal override void Release() { changed = null; base.Release(); }
}

public sealed class PasswordInput : Control
{
    private Action? changed;
    public int MaximumLength { get; }
    public int Length => Owner.PasswordLength(this);
    public void SetPassword(ReadOnlySpan<char> password) => Owner.WritePassword(this, password);
    public void WithPassword(PasswordReceiver receiver) => Owner.ReadPassword(this, receiver);
    public event Action Changed
    {
        add { VerifyAccess(); Owner.VerifyElementMutation(this); changed += value; }
        remove { Owner.VerifyEventRemoval(this); changed -= value; }
    }
    internal PasswordInput(Host host, string name, int maximumLength) : base(host, ElementKind.PasswordInput, name)
    {
        FormValues.ValidateMaximum(maximumLength, 4096);
        MaximumLength = maximumLength;
    }
    internal void RaiseChange() => changed?.Invoke();
    internal override void Release() { changed = null; base.Release(); }
}

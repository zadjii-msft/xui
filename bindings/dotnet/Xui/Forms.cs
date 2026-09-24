using System.Runtime.InteropServices;

namespace Xui;

public enum InputPurpose : uint { Normal, Email, Url, Telephone, Number }

public static class FormSupport
{
    public static bool Available => Native.FormsAvailable.Value;
    internal static void Require()
    {
        if (!Available) throw new NotSupportedException("This native XUI runtime does not provide the bounded Forms contract.");
    }
}

public sealed unsafe partial class Window
{
    public TextInput TextInput(string name, InputPurpose purpose)
    {
        Guard();
        if (!Enum.IsDefined(purpose)) throw new ArgumentOutOfRangeException(nameof(purpose));
        FormSupport.Require();
        using var pins = new Pins();
        Check(Native.TextInputCreateWithPurpose(Handle, pins.Text(name), (uint)purpose, out ulong input));
        return new(this, input);
    }
}

public sealed partial class TextInput
{
    public InputPurpose Purpose
    {
        get
        {
            Window.Guard();
            FormSupport.Require();
            Window.Check(Native.TextInputGetPurpose(Handle, out uint value));
            if (value > (uint)InputPurpose.Number) throw new InvalidOperationException("Native input returned an unknown purpose.");
            return (InputPurpose)value;
        }
    }
}

public sealed partial class PasswordInput
{
    public string Name { set => Window.Update(new Property(this, PropertyKind.Name, value)); }
    public string AutomationId { set => Window.Update(new Property(this, PropertyKind.AutomationId, value)); }
    public bool Enabled { set => Window.Update(new Property(this, PropertyKind.Enabled, Integer: value ? 1u : 0u)); }
    public PasswordInput SetVisible(bool value) { Features.Set(this, 37, first: value ? 1u : 0u); return this; }
    public PasswordInput SetHelp(string value) { Features.Set(this, 7, text: value); return this; }
    public bool Focused => Features.Get(this, 42).First != 0;
    public void Focus() { Window.Guard(); Window.Check(Native.Focus(Handle, 0)); }
    /// <summary>Clears retained and native secret buffers, undo state, and reveal state without a change event.</summary>
    /// <remarks>Valid on the owning UI thread even after close while this handle remains alive.</remarks>
    public void ClearPassword()
    {
        Window.Guard();
        FormSupport.Require();
        Window.Check(Native.PasswordClear(Handle));
    }
}

internal static partial class Native
{
    internal static readonly Lazy<bool> FormsAvailable = new(() =>
        HasExports("xui_forms_version", "xui_text_input_create_with_purpose", "xui_text_input_get_purpose",
            "xui_password_clear", "xui_element_set_axis_constraints", "xui_element_get_axis_constraints") &&
        FormsVersion() == 0x10000);
    [LibraryImport("xui", EntryPoint = "xui_forms_version")]
    internal static partial uint FormsVersion();
    [LibraryImport("xui", EntryPoint = "xui_text_input_create_with_purpose")]
    internal static partial int TextInputCreateWithPurpose(ulong window, Text name, uint purpose, out ulong input);
    [LibraryImport("xui", EntryPoint = "xui_text_input_get_purpose")]
    internal static partial int TextInputGetPurpose(ulong input, out uint purpose);
    [LibraryImport("xui", EntryPoint = "xui_password_clear")]
    internal static partial int PasswordClear(ulong input);
}

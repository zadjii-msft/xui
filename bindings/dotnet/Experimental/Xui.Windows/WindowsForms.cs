using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Xui.Experimental.Portable;
using P = Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

internal sealed class WindowsPurposeTextPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsConstrainedTextPeer(backend, element, events), IInputPurposeElementPeer { }

internal sealed class WindowsPasswordPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsConstrainedPeer(backend, element, events), IPasswordElementPeer
{
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private PasswordInput Password
    {
        get
        {
            Backend.VerifyAccess();
            ObjectDisposedException.ThrowIf(Disposed, this);
            return (PasswordInput)Native;
        }
    }

    public int PasswordLength => checked((int)Password.Length);
    public void SetPassword(ReadOnlySpan<char> password) => Password.SetPassword(password);
    public void ClearPassword() => Password.ClearPassword();

    public void WithPassword(P.PasswordReceiver receiver)
    {
        ArgumentNullException.ThrowIfNull(receiver);
        Password.WithPassword(utf8 =>
        {
            var text = new char[Utf8.GetCharCount(utf8)];
            try
            {
                int length = Utf8.GetChars(utf8, text);
                receiver(text.AsSpan(0, length));
            }
            finally { CryptographicOperations.ZeroMemory(MemoryMarshal.AsBytes(text.AsSpan())); }
        });
    }
}

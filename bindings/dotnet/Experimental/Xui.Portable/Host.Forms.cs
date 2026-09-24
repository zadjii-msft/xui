using System.Runtime.ExceptionServices;

namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    public TextInput TextInput(string name, InputPurpose purpose)
    {
        if (!Enum.IsDefined(purpose)) throw new ArgumentOutOfRangeException(nameof(purpose));
        return Create(() => new TextInput(this, name, purpose));
    }
    public MultilineText MultilineText(string name, int maximumLength = 65536) =>
        Create(() => new MultilineText(this, name, maximumLength));
    public PasswordInput PasswordInput(string name, int maximumLength = 256) =>
        Create(() => new PasswordInput(this, name, maximumLength));

    internal int PasswordLength(PasswordInput input)
    {
        VerifyAccess();
        var peer = InputPeer<IPasswordElementPeer>(input);
        int length = InputOperation(() => peer.PasswordLength);
        if (length < 0 || length > input.MaximumLength)
            throw new InvalidOperationException("The backend returned a password length outside the configured UTF-16 limit.");
        return length;
    }

    internal void WritePassword(PasswordInput input, ReadOnlySpan<char> password)
    {
        VerifyElementMutation(input);
        var peer = InputPeer<IPasswordElementPeer>(input);
        FormValues.ValidatePassword(password, input.MaximumLength);
        var current = attachment!;
        updating++;
        Exception? failure = null;
        try
        {
            NoteNativeModelMutation();
            peer.SetPassword(password);
        }
        catch (Exception error) { failure = FailAttachment(current, error); }
        finally { updating--; }
        FinishInteractionDelivery(failure);
    }

    internal void ReadPassword(PasswordInput input, PasswordReceiver receiver)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(receiver);
        var peer = InputPeer<IPasswordElementPeer>(input);
        ExceptionDispatchInfo? callbackFailure = null;
        int calls = 0;
        bool reading = true;
        try
        {
            InputOperation(() =>
            {
                peer.WithPassword(value =>
                {
                    if (!reading) throw new InvalidOperationException("A password receiver cannot outlive its synchronous read.");
                    if (++calls != 1) return;
                    try
                    {
                        FormValues.ValidatePassword(value, input.MaximumLength);
                        receiver(value);
                    }
                    catch (Exception error) { callbackFailure = ExceptionDispatchInfo.Capture(error); }
                });
                return true;
            });
        }
        catch (Exception error) when (callbackFailure is not null)
        {
            throw new AggregateException(callbackFailure.SourceException, error);
        }
        finally { reading = false; }
        if (calls != 1) throw new InvalidOperationException("The backend must invoke the password receiver exactly once.");
        callbackFailure?.Throw();
    }

    private static void ClearAttachmentPasswords(Attachment current, ISet<Element>? removed, List<Exception> failures)
    {
        foreach (var element in current.Order)
        {
            if (removed is not null && !removed.Contains(element)) continue;
            if (element is PasswordInput && current.Peers[element] is IPasswordElementPeer password)
            {
                try { password.ClearPassword(); }
                catch (Exception error) { failures.Add(error); }
            }
        }
    }
}

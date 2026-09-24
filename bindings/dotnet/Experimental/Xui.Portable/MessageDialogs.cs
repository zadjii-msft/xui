using System.Text;

namespace Xui.Experimental.Portable;

public enum MessageDialogKind { Alert, Confirm }
public enum MessageDialogDecision { Accepted, Declined }

public sealed class MessageDialogRequest
{
    public const int MaximumTitleLength = 256;
    public const int MaximumMessageLength = 8192;
    public const int MaximumButtonLength = 80;
    public MessageDialogKind Kind { get; }
    public string Title { get; }
    public string Message { get; }
    public string AcceptText { get; }
    public string? DeclineText { get; }
    public MessageDialogDecision DefaultDecision => Kind == MessageDialogKind.Confirm
        ? MessageDialogDecision.Declined : MessageDialogDecision.Accepted;

    private MessageDialogRequest(MessageDialogKind kind, string title, string message, string acceptText, string? declineText)
    {
        ValidateText(title, MaximumTitleLength, nameof(title), singleLine: true);
        ValidateText(message, MaximumMessageLength, nameof(message), singleLine: false);
        ValidateText(acceptText, MaximumButtonLength, nameof(acceptText), singleLine: true);
        if (kind == MessageDialogKind.Confirm) ValidateText(declineText!, MaximumButtonLength, nameof(declineText), singleLine: true);
        Kind = kind;
        Title = title;
        Message = message;
        AcceptText = acceptText;
        DeclineText = declineText;
    }

    public static MessageDialogRequest Alert(string title, string message, string acceptText = "OK") =>
        new(MessageDialogKind.Alert, title, message, acceptText, null);

    public static MessageDialogRequest Confirm(string title, string message, string acceptText = "Confirm", string declineText = "Cancel") =>
        new(MessageDialogKind.Confirm, title, message, acceptText, declineText);

    private static void ValidateText(string value, int maximum, string parameter, bool singleLine)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value, parameter);
        if (value.Length > maximum || value.Contains('\0') ||
            singleLine && value.AsSpan().IndexOfAny("\r\n\u0085\u2028\u2029") >= 0)
            throw new ArgumentException("Dialog text exceeds its limit or contains a prohibited character.", parameter);
        for (int offset = 0; offset < value.Length;)
        {
            if (Rune.DecodeFromUtf16(value.AsSpan(offset), out _, out int consumed) != System.Buffers.OperationStatus.Done)
                throw new ArgumentException("Dialog text must contain well-formed UTF-16.", parameter);
            offset += consumed;
        }
    }
}

public interface IMessageDialogRequest : IDisposable
{
    /// <summary>Requests cancellation from any thread; it does not certify native dismissal or quiescence.</summary>
    void Cancel();
}

public interface IMessageDialogBackend : IBackend
{
    /// <summary>Available only with explicit authority over the enclosing modal window, Activity, or document.</summary>
    CapabilityAvailability MessageDialogAvailability { get; }

    /// <summary>Returns the owned request before delivering any UI-thread completion outside native guards.</summary>
    IMessageDialogRequest BeginMessageDialog(MessageDialogRequest request,
        Action<OperationResult<MessageDialogDecision>> completed);
}

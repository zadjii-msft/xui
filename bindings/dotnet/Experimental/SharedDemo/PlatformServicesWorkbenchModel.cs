using System;
using System.Globalization;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed record PlatformServicesWorkbenchState
{
    public const int MaximumClipboardCharacters = 4096;
    public const int MaximumInspectionBytes = 65536;
    public string ClipboardDraft { get; init; } = "";
    public string UriDraft { get; init; } = "https://example.com";
    public CapabilityAvailability ClipboardAvailability { get; init; }
    public CapabilityAvailability UriAvailability { get; init; }
    public CapabilityAvailability FileAvailability { get; init; }
    public bool Busy { get; init; }
    public bool CancelRequested { get; init; }
    public string Status { get; init; } = "No service action has run.";
    public string Error { get; init; } = "";
    public int? ClipboardLength { get; init; }
    public string FileName { get; init; } = "";
    public int? FileByteCount { get; init; }
    public bool FileTooLarge { get; init; }

    public bool CanReadClipboard => !Busy && ClipboardAvailability != CapabilityAvailability.Unsupported;
    public bool CanWriteClipboard => CanReadClipboard && ClipboardDraft.Length <= MaximumClipboardCharacters;
    public bool CanOpenUri => !Busy && UriAvailability != CapabilityAvailability.Unsupported && TryUri(UriDraft, out _);
    public bool CanPickFile => !Busy && FileAvailability != CapabilityAvailability.Unsupported;
    public bool CanCancel => Busy && !CancelRequested;
    public string ClipboardAvailabilityText => "Clipboard: " + AvailabilityText(ClipboardAvailability);
    public string UriAvailabilityText => "HTTPS launch: " + AvailabilityText(UriAvailability);
    public string FileAvailabilityText => "Open file: " + AvailabilityText(FileAvailability);
    public string ClipboardSummary => ClipboardLength is int count
        ? "Clipboard read: " + count.ToString(CultureInfo.InvariantCulture) + " UTF-16 units. Content not displayed."
        : "Clipboard has not been read.";
    public string FileSummary => FileTooLarge
        ? "File exceeds the 65536-byte inspection limit. Content not displayed."
        : FileByteCount is int count ? "Inspected " + count.ToString(CultureInfo.InvariantCulture) + " bytes. Content not displayed."
        : "No file inspected.";
    public string UriValidation => TryUri(UriDraft, out _) ? "HTTPS only. Opens only when you choose Launch." :
        "Enter an absolute HTTPS URL without credentials or control characters.";
    public string ClipboardValidation => ClipboardDraft.Length <= MaximumClipboardCharacters
        ? "Write only when you choose Copy. Empty text clears the clipboard."
        : "Clipboard draft exceeds 4096 UTF-16 units.";

    public static bool TryUri(string text, out Uri? uri)
    {
        uri = null;
        if (text.Length > 2048 || string.IsNullOrWhiteSpace(text) || text != text.Trim()) return false;
        foreach (char character in text)
            if (char.IsControl(character)) return false;
        if (!Uri.TryCreate(text, UriKind.Absolute, out var candidate) ||
            candidate.Scheme != Uri.UriSchemeHttps || candidate.Host.Length == 0 || candidate.UserInfo.Length != 0) return false;
        uri = candidate;
        return true;
    }
    public static string AvailabilityText(CapabilityAvailability availability) => availability switch
    {
        CapabilityAvailability.Available => "available",
        CapabilityAvailability.RequiresUserGesture => "requires a user action",
        CapabilityAvailability.Unsupported => "unsupported",
        _ => throw new ArgumentOutOfRangeException(nameof(availability))
    };
    public static string SafeMetadata(string value, int maximumLength)
    {
        ArgumentNullException.ThrowIfNull(value);
        string bounded = value.Length > maximumLength ? value[..maximumLength] : value;
        if (bounded.Length > 0 && char.IsHighSurrogate(bounded[^1])) bounded = bounded[..^1];
        return string.Concat(System.Linq.Enumerable.Select(bounded, character => char.IsControl(character) ? ' ' : character));
    }
}

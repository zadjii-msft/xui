namespace Xui.FileExplorer.Models;

public readonly record struct KeyGesture(uint Key, bool Control, bool Shift, bool Alt)
{
    public static KeyGesture[] ParseSequence(string text)
    {
        if (string.IsNullOrWhiteSpace(text) || text.Length > 160)
            throw new InvalidDataException("Enter a shortcut, such as Ctrl+K, Ctrl+R.");
        var parts = text.Split(',', StringSplitOptions.TrimEntries);
        if (parts.Length > 3) throw new InvalidDataException("A key sequence supports at most three strokes.");
        var result = parts.Select(Parse).ToArray();
        if (result.Length > 1 && (!result[0].Control && !result[0].Alt))
            throw new InvalidDataException("Start a sequence with Ctrl or Alt to preserve typing.");
        if (result.Any(k => k.Control && k.Alt))
            throw new InvalidDataException("Ctrl+Alt is reserved for AltGr text input.");
        if (result.Any(k => k.Alt && k.Key == 0x73))
            throw new InvalidDataException("Alt+F4 is reserved for closing the window.");
        if (result.Any(k => (k.Alt && k.Key is 0x09 or 0x1b) || (k.Control && k.Key == 0x1b)))
            throw new InvalidDataException("This shortcut is reserved for Windows.");
        if (!result[0].Control && !result[0].Alt && (result[0].Key is >= 0x30 and <= 0x5a or >= 0xba and <= 0xe2))
            throw new InvalidDataException("Unmodified printable keys are reserved for native Find input.");
        return result;
    }

    public static KeyGesture Parse(string text)
    {
        bool control = false, shift = false, alt = false;
        var tokens = text.Split('+', StringSplitOptions.TrimEntries);
        foreach (string token in tokens[..^1])
        {
            switch (token.ToUpperInvariant())
            {
                case "CTRL" or "CONTROL" when !control: control = true; break;
                case "SHIFT" when !shift: shift = true; break;
                case "ALT" when !alt: alt = true; break;
                default: throw new InvalidDataException($"Invalid modifier '{token}'.");
            }
        }
        string key = tokens[^1].ToUpperInvariant();
        uint code = key switch
        {
            "ENTER" or "RETURN" => 0x0d, "ESC" or "ESCAPE" => 0x1b, "SPACE" => 0x20,
            "TAB" => 0x09, "LEFT" => 0x25, "UP" => 0x26, "RIGHT" => 0x27, "DOWN" => 0x28,
            "HOME" => 0x24, "END" => 0x23, "PAGEUP" or "PGUP" => 0x21,
            "PAGEDOWN" or "PGDN" => 0x22, "INSERT" or "INS" => 0x2d, "DELETE" or "DEL" => 0x2e,
            "BACKSPACE" => 8, "\\" => 0xdc, "/" => 0xbf, "." => 0xbe, "," => 0xbc,
            _ when key.Length == 1 && char.IsAsciiLetterOrDigit(key[0]) => key[0],
            _ when key.StartsWith('F') && int.TryParse(key.AsSpan(1), out int f) && f is >= 1 and <= 24 => (uint)(0x6f + f),
            _ => throw new InvalidDataException($"Unknown key '{text}'.")
        };
        return new(code, control, shift, alt);
    }

    public override string ToString()
    {
        string name = Key switch
        {
            0x0d => "Enter", 0x1b => "Escape", 0x20 => "Space", 0x09 => "Tab",
            0x25 => "Left", 0x26 => "Up", 0x27 => "Right", 0x28 => "Down", 0x24 => "Home",
            0x23 => "End", 0x21 => "PageUp", 0x22 => "PageDown", 0x2d => "Insert",
            0x2e => "Delete", 8 => "Backspace", 0xdc => "\\", 0xbf => "/", 0xbe => ".", 0xbc => ",",
            >= 0x70 and <= 0x87 => $"F{Key - 0x6f}",
            _ => ((char)Key).ToString()
        };
        return (Control ? "Ctrl+" : "") + (Shift ? "Shift+" : "") + (Alt ? "Alt+" : "") + name;
    }
}

public static class KeybindingValidation
{
    public static void Validate(IEnumerable<(string Id, IEnumerable<string> Bindings)> commands)
    {
        var seen = new List<(string Id, KeyGesture[] Keys)>();
        foreach (var (id, bindings) in commands)
        foreach (string binding in bindings)
        {
            var keys = KeyGesture.ParseSequence(binding);
            foreach (var previous in seen)
                if (keys.Take(Math.Min(keys.Length, previous.Keys.Length))
                    .SequenceEqual(previous.Keys.Take(Math.Min(keys.Length, previous.Keys.Length))))
                    throw new InvalidDataException($"Shortcut '{binding}' conflicts with '{previous.Id}' (duplicate or sequence prefix).");
            seen.Add((id, keys));
        }
    }
}

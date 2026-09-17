using System.Security;

namespace Xui.FileExplorer.Models;

public static class PreviewFilePolicy
{
    public static bool IsLocalPath(string path) =>
        path.Length is >= 3 and < 32768 && char.IsAsciiLetter(path[0]) && path[1] == ':' && path[2] == '\\'
        && path.IndexOfAny(['/', '\0', '\r', '\n']) < 0 && path.IndexOf(':', 2) < 0
        && !path[3..].Split('\\').Any(part => part is "" or "." or "..");

    public static bool AllowsContent(string path, out string reason)
    {
        reason = "Preview is limited to generic metadata because the file's local origin could not be verified.";
        if (!OperatingSystem.IsWindows() || !IsLocalPath(path)) return false;
        try
        {
            var drive = new DriveInfo(path[..3]);
            if (drive.DriveType != DriveType.Fixed || drive.DriveFormat is not ("NTFS" or "ReFS")) return false;
            for (string? directory = Path.GetDirectoryName(path); directory is not null; directory = Path.GetDirectoryName(directory))
                if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) return false;
            const FileAttributes denied = FileAttributes.Directory | FileAttributes.ReparsePoint | FileAttributes.Offline
                | (FileAttributes)0x00400000 | (FileAttributes)0x00040000;
            if ((File.GetAttributes(path) & denied) != 0) return false;
            if (new[] { ".lnk", ".url", ".website", ".search-ms", ".library-ms", ".exe", ".dll", ".msi" }
                .Contains(Path.GetExtension(path), StringComparer.OrdinalIgnoreCase)) return false;
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            try
            {
                using var origin = new FileStream(path + ":Zone.Identifier", FileMode.Open, FileAccess.Read, FileShare.Read);
                if (origin.Length > 4096) return false;
                using var reader = new StreamReader(origin, new System.Text.UTF8Encoding(false, true), true, 4096);
                if (!AllowsZone(reader.ReadToEnd())) return false;
            }
            catch (FileNotFoundException) { /* An absent zone stream denotes a local, unmarked file. */ }
            reason = "";
            return true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or SecurityException
            or ArgumentException or NotSupportedException)
        {
            reason = "Preview is limited to generic metadata. Origin check failed: " + error.Message;
            return false;
        }
    }

    public static bool AllowsZone(string zone)
    {
        bool section = false, found = false;
        foreach (string line in zone.TrimStart('\ufeff').Split('\n').Select(line => line.TrimEnd('\r')))
        {
            if (line.Contains('\0')) return false;
            string trimmed = line.TrimStart(' ', '\t');
            if (trimmed == "[ZoneTransfer]") section = true;
            else if (trimmed.StartsWith('[')) section = false;
            else if (trimmed.StartsWith("ZoneId", StringComparison.OrdinalIgnoreCase))
            {
                if (!section || found || line != "ZoneId=0") return false;
                found = true;
            }
        }
        return found;
    }
}

namespace Xui.FileExplorer.Models;

internal sealed record BreadcrumbPart(string Name, string Path);

internal static class BreadcrumbPath
{
    public static IReadOnlyList<BreadcrumbPart> Create(string path)
    {
        if (!Path.IsPathFullyQualified(path))
            throw new ArgumentException("Breadcrumbs require an absolute path.", nameof(path));
        path = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        string root = Path.GetPathRoot(path)!;
        var parts = new List<BreadcrumbPart> { new(root, root) };
        string current = root;
        foreach (string name in path[root.Length..].Split(
            [Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar], StringSplitOptions.RemoveEmptyEntries))
        {
            current = Path.Combine(current, name);
            parts.Add(new(name, current));
        }
        return parts;
    }
}

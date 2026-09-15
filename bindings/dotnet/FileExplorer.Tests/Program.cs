using System.Text.Json;
using Xui.FileExplorer.Models;

internal static class Program
{
    private static int assertions;
    private static readonly CancellationToken None = CancellationToken.None;

    public static async Task<int> Main()
    {
        var fixture = Path.Combine(Directory.GetCurrentDirectory(), ".file-explorer-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(fixture);
        try
        {
            await FileSystemTests(fixture);
            TabTests(fixture);
            PaneTests(fixture);
            StateTests(fixture);
            Console.WriteLine($"PASS: {assertions} assertions.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
        finally
        {
            Directory.Delete(fixture, recursive: true);
            var parent = Path.GetDirectoryName(fixture)!;
            if (!Directory.EnumerateFileSystemEntries(parent).Any())
                Directory.Delete(parent);
        }
    }

    private static async Task FileSystemTests(string fixture)
    {
        var root = Path.Combine(fixture, "Files");
        Directory.CreateDirectory(root);
        var directory = Path.Combine(root, "Alpha folder");
        Directory.CreateDirectory(Path.Combine(directory, "Nested"));
        Directory.CreateDirectory(Path.Combine(root, "My alpha"));
        Directory.CreateDirectory(Path.Combine(root, "資料"));
        File.WriteAllBytes(Path.Combine(root, "alpha small.txt"), new byte[2]);
        File.WriteAllBytes(Path.Combine(root, "Z large.txt"), new byte[100]);
        File.WriteAllText(Path.Combine(root, "notes"), "hello");
        File.WriteAllText(Path.Combine(directory, "child.txt"), "child");

        var service = new FileSystemService();
        var read = await service.ReadDirectoryAsync(root, fixture, None);
        Equal(root, read.Path);
        Equal(6, read.Entries.Count);
        True(read.Entries.Take(3).All(entry => entry.IsDirectory));
        True(read.Entries.Skip(3).All(entry => !entry.IsDirectory));
        True(!read.Entries.Any(entry => entry.Name == "child.txt"));
        Equal("Folder", read.Entries[0].Kind);
        var small = read.Entries.Single(entry => entry.Name == "alpha small.txt");
        Equal(2L, small.Size);
        Equal("TXT file", small.Kind);
        Equal(DateTimeKind.Utc, small.ModifiedUtc.Kind);
        Equal("File", read.Entries.Single(entry => entry.Name == "notes").Kind);

        Equal(directory, FileSystemService.ResolvePath("Alpha folder", root));
        Equal(directory, FileSystemService.ResolvePath($" \"{directory}\" ", fixture));
        Equal(directory, FileSystemService.ResolvePath("'Alpha folder'", root));
        Equal(root, FileSystemService.ResolvePath(@"Alpha folder\..", root));
        Equal(root, FileSystemService.ResolvePath("", root));
        Equal(root, FileSystemService.ResolvePath(root + Path.DirectorySeparatorChar, fixture));
        Equal(@"\\server\share\folder", FileSystemService.ResolvePath(@"\\server\share\folder", root));
        var variable = "XUI_EXPLORER_TEST_" + Guid.NewGuid().ToString("N");
        Environment.SetEnvironmentVariable(variable, root);
        try
        {
            Equal(directory, FileSystemService.ResolvePath($"%{variable}%\\Alpha folder", fixture));
            Equal(2, (await service.SuggestAsync($"\"%{variable}%\\Alpha folder\"", fixture, None)).Entries.Count);
        }
        finally
        {
            Environment.SetEnvironmentVariable(variable, null);
        }

        var initial = await service.SuggestAsync("", root, None);
        Equal(root, initial.Directory);
        Sequence(read.Entries.Select(entry => entry.Name), initial.Entries.Select(entry => entry.Name));
        var cwd = Directory.GetCurrentDirectory();
        var cwdSuggestions = await service.SuggestAsync("", cwd, None);
        Equal(cwd, cwdSuggestions.Directory);
        Sequence(Directory.GetFileSystemEntries(cwd).Select(Path.GetFileName).Order(),
            cwdSuggestions.Entries.Select(entry => entry.Name).Order());
        var exact = await service.SuggestAsync("Alpha folder", root, None);
        Equal(directory, exact.Directory);
        Equal(2, exact.Entries.Count);
        var partial = await service.SuggestAsync("aLpHa", root, None);
        Sequence(new[] { "Alpha folder", "alpha small.txt", "My alpha" }, partial.Entries.Select(entry => entry.Name));
        Equal(6, partial.Snapshot.Entries.Count);
        foreach (string query in new[] { "", "aLpHa", "lPhA", "alpha small.txt", "nothing-matches", "資", root + "\\" })
        {
            var cached = FileSystemService.SuggestFromSnapshot(read, query, root)
                ?? throw new Exception("A same-directory query unexpectedly required I/O.");
            var scanned = await service.SuggestAsync(query, root, None);
            Equal(scanned.Directory, cached.Directory);
            Sequence(scanned.Entries.Select(entry => entry.FullPath), cached.Entries.Select(entry => entry.FullPath));
            True(ReferenceEquals(read, cached.Snapshot));
        }
        Equal<NavigationSuggestions?>(null, FileSystemService.SuggestFromSnapshot(read, directory, root));
        Equal<NavigationSuggestions?>(null, FileSystemService.SuggestFromSnapshot(read, directory + "\\", root));
        Equal<NavigationSuggestions?>(null, FileSystemService.SuggestFromSnapshot(read, @"absent\child", root));
        Sequence(read.Entries.Select(entry => entry.Name),
            FileSystemService.SuggestFromSnapshot(partial.Snapshot, "", root)!.Entries.Select(entry => entry.Name));
        var memoryOnly = new DirectorySnapshot(Path.Combine(root, "not-on-disk"),
            [new(Path.Combine(root, "not-on-disk", "cached.txt"), "cached.txt", false, 1, DateTime.UnixEpoch)]);
        Equal("cached.txt", FileSystemService.SuggestFromSnapshot(memoryOnly, "cache", memoryOnly.Path)!.Entries.Single().Name);
        Throws<ArgumentException>(() => FileSystemService.SuggestFromSnapshot(read, "\0", root));
        var contains = await service.SuggestAsync("lPhA", root, None);
        Equal(3, contains.Entries.Count);
        var expanded = partial.Entries[0].FullPath + Path.DirectorySeparatorChar;
        Equal(directory, (await service.SuggestAsync(expanded, root, None)).Directory);
        Equal("alpha small.txt", (await service.SuggestAsync("alpha small.txt", root, None)).Entries.Single().Name);
        Equal(0, (await service.SuggestAsync("nothing-matches", root, None)).Entries.Count);
        Equal(1, (await service.SuggestAsync("資", root, None)).Entries.Count);
        await ThrowsAsync<DirectoryNotFoundException>(() => service.ReadDirectoryAsync("absent", root, None));
        await ThrowsAsync<DirectoryNotFoundException>(() => service.SuggestAsync(@"absent\also-absent", root, None));
        await ThrowsAsync<IOException>(() => service.ReadDirectoryAsync(small.FullPath, root, None));
        await ThrowsAsync<ArgumentException>(() => service.ReadDirectoryAsync("\0", root, None));

        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        await ThrowsAsync<OperationCanceledException>(() => service.ReadDirectoryAsync(root, fixture, cancellation.Token));
        await ThrowsAsync<OperationCanceledException>(() => service.SuggestAsync("", root, cancellation.Token));

        var statefulTab = new ExplorerTab(1, root);
        statefulTab.Commit(read);
        statefulTab.SelectedPath = small.FullPath;
        statefulTab.ScrollOffset = 42;
        try
        {
            statefulTab.Commit(await service.ReadDirectoryAsync("absent", root, None));
        }
        catch (DirectoryNotFoundException)
        {
        }
        Equal(root, statefulTab.Path);
        Equal(small.FullPath, statefulTab.SelectedPath);
        Equal(42d, statefulTab.ScrollOffset);
        True(!statefulTab.CanBack);
        Equal(read.Entries.Count, statefulTab.Entries.Count);
        SortTests(read.Entries);
    }

    private static void SortTests(IReadOnlyList<FileEntry> entries)
    {
        var ascending = FileSystemService.FilterAndSort(entries, "", 3, false);
        Sequence(new long[] { 2, 5, 100 }, ascending.Where(entry => !entry.IsDirectory).Select(entry => entry.Size));
        var descending = FileSystemService.FilterAndSort(entries, "", 3, true);
        True(descending.Take(3).All(entry => entry.IsDirectory));
        Sequence(new long[] { 100, 5, 2 }, descending.Where(entry => !entry.IsDirectory).Select(entry => entry.Size));
        Equal(3, FileSystemService.FilterAndSort(entries, "ALPHA", 0, false).Count);
        Equal(0, FileSystemService.FilterAndSort(entries, "missing", 0, false).Count);
        Equal(6, entries.Count);
        var dates = new[]
        {
            new FileEntry("b.txt", "b.txt", false, 20, DateTime.UnixEpoch.AddDays(20)),
            new FileEntry("z.bin", "z.bin", false, 3, DateTime.UnixEpoch),
            new FileEntry("folder", "folder", true, 0, DateTime.UnixEpoch)
        };
        Sequence(new[] { "folder", "z.bin", "b.txt" },
            FileSystemService.FilterAndSort(dates, "", 1, false).Select(entry => entry.Name));
        Sequence(new[] { "folder", "b.txt", "z.bin" },
            FileSystemService.FilterAndSort(dates, "", 1, true).Select(entry => entry.Name));
        Sequence(new[] { "folder", "z.bin", "b.txt" },
            FileSystemService.FilterAndSort(dates, "", 2, false).Select(entry => entry.Name));
        Sequence(new[] { "folder", "z.bin", "b.txt" },
            FileSystemService.FilterAndSort(dates, "", 0, true).Select(entry => entry.Name));
        Throws<ArgumentOutOfRangeException>(() => FileSystemService.FilterAndSort(entries, "", 4, false));
    }

    private static void TabTests(string fixture)
    {
        var tab = new ExplorerTab(8, fixture);
        Equal(8UL, tab.Id);
        True(!tab.CanBack && !tab.CanForward);
        True(!tab.TryGetHistory(-1, out _));
        var first = Snapshot(fixture, "first");
        var second = Snapshot(fixture, "second");
        tab.Commit(first);
        tab.Commit(first);
        True(!tab.CanBack);
        tab.SelectedPath = "missing selection";
        tab.ScrollOffset = 91;
        tab.Filter = "filter";
        tab.FindOpen = true;
        tab.SortColumn = 3;
        tab.SortDescending = true;
        tab.Commit(first);
        Equal("missing selection", tab.SelectedPath);
        Equal(91d, tab.ScrollOffset);
        tab.CommitHistory(first, 0);
        Equal("missing selection", tab.SelectedPath);
        Equal(91d, tab.ScrollOffset);
        tab.Commit(new DirectorySnapshot(first.Path.ToUpperInvariant() + Path.DirectorySeparatorChar, []));
        Equal("missing selection", tab.SelectedPath);
        Equal(91d, tab.ScrollOffset);
        tab.CommitHistory(new DirectorySnapshot(Path.Combine(first.Path, "."), []), 0);
        Equal("missing selection", tab.SelectedPath);
        Equal(91d, tab.ScrollOffset);
        Equal("filter", tab.Filter);
        True(tab.FindOpen && tab.SortDescending);
        Equal(3, tab.SortColumn);
        True(!tab.CanBack);
        tab.Commit(second);
        Equal<string?>(null, tab.SelectedPath);
        Equal(0d, tab.ScrollOffset);
        True(tab.CanBack && !tab.CanForward);
        True(tab.TryGetHistory(-1, out var back));
        Equal(first.Path, back);
        Equal(second.Path, tab.Path);
        True(!tab.TryGetHistory(int.MaxValue, out _));
        Throws<InvalidOperationException>(() => tab.CommitHistory(second, -1));
        Equal(second.Path, tab.Path);
        tab.Filter = "filter";
        tab.FindOpen = true;
        tab.SortColumn = 3;
        tab.SortDescending = true;
        tab.SelectedPath = "old selection";
        tab.ScrollOffset = 123;
        tab.CommitHistory(first, -1);
        True(!tab.CanBack && tab.CanForward);
        Equal("filter", tab.Filter);
        True(tab.FindOpen && tab.SortDescending);
        Equal(3, tab.SortColumn);
        Equal<string?>(null, tab.SelectedPath);
        Equal(0d, tab.ScrollOffset);
        True(tab.TryGetHistory(1, out var forward));
        Equal(second.Path, forward);
        tab.CommitHistory(second, 1);
        Equal(second.Path, tab.Path);
        tab.CommitHistory(first, -1);
        tab.Commit(Snapshot(fixture, "third"));
        True(!tab.CanForward && tab.CanBack);
        Throws<ArgumentNullException>(() => tab.Commit(null!));
        Throws<ArgumentException>(() => tab.Commit(new DirectorySnapshot(" ", [])));
        True(tab.Path.EndsWith("third", StringComparison.Ordinal));

        var mutable = new List<FileEntry> { new("file", "file", false, 1, DateTime.UnixEpoch) };
        tab.Commit(new DirectorySnapshot(fixture, mutable));
        mutable.Clear();
        Equal(1, tab.Entries.Count);
        var bounded = new ExplorerTab(9, fixture);
        for (var i = 0; i < 200; i++)
            bounded.Commit(Snapshot(fixture, i.ToString()));
        var steps = 0;
        while (bounded.TryGetHistory(-1, out var path))
        {
            bounded.CommitHistory(new DirectorySnapshot(path, []), -1);
            steps++;
        }
        Equal(ExplorerTab.HistoryLimit - 1, steps);
        Equal(Path.Combine(fixture, "72"), bounded.Path);
        True(!bounded.TryGetHistory(int.MinValue, out _));
    }

    private static void PaneTests(string fixture)
    {
        var pane = new ExplorerPane(fixture);
        var other = new ExplorerPane(fixture);
        var first = pane.Active;
        True(!pane.CloseTab(first.Id));
        var second = pane.AddTab(Path.Combine(fixture, "second"));
        Equal(second, pane.Active);
        second.Filter = "other filter";
        first.Commit(Snapshot(fixture, "first"));
        True(pane.SelectTab(first.Id));
        Equal("", first.Filter);
        Equal(fixture, other.Active.Path);
        True(!pane.SelectTab(999));
        Equal(first, pane.Active);
        pane.CycleTab(-1);
        Equal(second, pane.Active);
        pane.CycleTab(1);
        Equal(first, pane.Active);
        pane.CycleTab(int.MinValue);
        Equal(first, pane.Active);
        True(pane.CloseTab(second.Id));
        Equal(first, pane.Active);
        var third = pane.AddTab(fixture);
        True(third.Id != second.Id);
        True(pane.CloseTab(third.Id));
        Equal(first, pane.Active);
        True(!pane.CloseTab(999));
        for (var i = 1; i < ExplorerPane.TabLimit; i++)
            pane.AddTab(fixture);
        Equal(ExplorerPane.TabLimit, pane.Tabs.Count);
        Equal(ExplorerPane.TabLimit, pane.Tabs.Select(tab => tab.Id).Distinct().Count());
        Throws<InvalidOperationException>(() => pane.AddTab(fixture));
        Equal(1, other.Tabs.Count);
    }

    private static void StateTests(string fixture)
    {
        var path = Path.Combine(fixture, "State", "state.json");
        var store = new AppStateStore(path);
        var state = store.Load();
        Equal(0, state.Bookmarks.Count);
        Equal(0, state.Recents.Count);
        var unicode = Path.Combine(fixture, "資料 with spaces");
        state.ToggleBookmark(unicode);
        state.ToggleBookmark(unicode.ToUpperInvariant());
        Equal(0, state.Bookmarks.Count);
        state.ToggleBookmark(unicode);
        state.AddRecent(unicode);
        state.AddRecent(unicode.ToUpperInvariant());
        Equal(1, state.Recents.Count);
        for (var i = 0; i < 40; i++)
            state.AddRecent(Path.Combine(fixture, i.ToString()));
        Equal(ExplorerState.RecentLimit, state.Recents.Count);
        Equal(Path.Combine(fixture, "39"), state.Recents[0]);
        store.Save(state);
        var loaded = new AppStateStore(path).Load();
        Sequence(state.Bookmarks, loaded.Bookmarks);
        Sequence(state.Recents, loaded.Recents);
        loaded.AddRecent(unicode);
        store.Save(loaded);
        Equal(unicode, store.Load().Recents[0]);
        var original = File.ReadAllBytes(path);
        var invalid = new ExplorerState { Recents = null! };
        Throws<InvalidDataException>(() => store.Save(invalid));
        Sequence(original, File.ReadAllBytes(path));
        Throws<ArgumentException>(() => state.AddRecent("\0"));
        Throws<ArgumentException>(() => state.ToggleBookmark(new string('x', 32768)));

        // A read-only sharing lease prevents replacement, without changing file permissions.
        using (var lease = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            loaded.ToggleBookmark(Path.Combine(fixture, "changed"));
            Throws<IOException>(() => store.Save(loaded));
            Sequence(original, File.ReadAllBytes(path));
        }
        Equal(1, Directory.GetFiles(Path.GetDirectoryName(path)!).Length);
        var unrelated = Path.Combine(Path.GetDirectoryName(path)!, ".unrelated.tmp");
        File.WriteAllText(unrelated, "do not remove");
        store.Save(loaded);
        True(File.Exists(unrelated));
        Equal(2, store.Load().Bookmarks.Count);

        foreach (var json in new[]
                 {
                     "{broken", "null", "[]", "{\"Bookmarks\":null}", "{\"Recents\":[null]}",
                     "{\"Bookmarks\":[\"\"]}", "{\"Recents\":[1]}",
                     JsonSerializer.Serialize(new ExplorerState { Recents = Enumerable.Repeat("path", 33).ToList() }),
                     JsonSerializer.Serialize(new ExplorerState { Bookmarks = [new string('x', 32768)] })
                 })
        {
            File.WriteAllText(path, json);
            var corrupt = Throws<InvalidDataException>(() => store.Load());
            True(corrupt.Message.Contains(path, StringComparison.Ordinal));
            Throws<InvalidDataException>(() => store.Save(new ExplorerState()));
            Equal(json, File.ReadAllText(path));
        }
        File.Delete(path);
        Equal(0, store.Load().Bookmarks.Count);
        store.Save(new ExplorerState());
        Equal(0, store.Load().Recents.Count);
        Equal(2, Directory.GetFiles(Path.GetDirectoryName(path)!).Length);
    }

    private static DirectorySnapshot Snapshot(string root, string name) => new(Path.Combine(root, name), []);

    private static void True(bool condition)
    {
        assertions++;
        if (!condition)
            throw new InvalidOperationException($"Assertion {assertions} failed.");
    }

    private static void Equal<T>(T expected, T actual)
    {
        assertions++;
        if (!EqualityComparer<T>.Default.Equals(expected, actual))
            throw new InvalidOperationException($"Assertion {assertions}: expected '{expected}', got '{actual}'.");
    }

    private static void Sequence<T>(IEnumerable<T> expected, IEnumerable<T> actual)
        => True(expected.SequenceEqual(actual));

    private static T Throws<T>(Action action) where T : Exception
    {
        assertions++;
        try { action(); }
        catch (T error) { return error; }
        throw new InvalidOperationException($"Assertion {assertions}: expected {typeof(T).Name}.");
    }

    private static async Task ThrowsAsync<T>(Func<Task> action) where T : Exception
    {
        assertions++;
        try { await action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Assertion {assertions}: expected {typeof(T).Name}.");
    }
}

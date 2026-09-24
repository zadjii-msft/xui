using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using Xui.Experimental.Portable;

namespace PortableDemo;

public enum StudioSection { Library, Drafts, Insights }
public enum StudioCategory { All, Engineering, Design, Research }

public static class StudioTabs
{
    public const string OperationsKey = "operations";
    public const ulong OperationsId = 10001;
    public static bool IsDocument(string key) => key != OperationsKey;
    public static ulong NativeId(string key) => key == OperationsKey ? OperationsId : StudioCatalog.Get(key).NativeId;
    public static string Key(ulong id) => id == OperationsId ? OperationsKey :
        id is >= 1 and <= StudioCatalog.Count ? StudioCatalog.Entries[checked((int)id - 1)].Key :
        throw new ArgumentOutOfRangeException(nameof(id));
}

public sealed record StudioCatalogEntry(string Key, string Title, StudioCategory Category, string Summary)
{
    public ulong NativeId => ulong.Parse(Key.AsSpan(4), NumberStyles.None, CultureInfo.InvariantCulture);
}

public static class StudioCatalog
{
    public const int Count = 10000;
    public static ImmutableArray<StudioCatalogEntry> Entries { get; } = CreateEntries();

    private static ImmutableArray<StudioCatalogEntry> CreateEntries()
    {
        string[] topics = ["Keyboard navigation", "Release checklist", "Design notes", "Performance review", "Accessibility audit"];
        var entries = ImmutableArray.CreateBuilder<StudioCatalogEntry>(Count);
        for (int index = 0; index < Count; index++)
        {
            string number = (index + 1).ToString("00000", CultureInfo.InvariantCulture);
            var category = (StudioCategory)(index % 3 + 1);
            entries.Add(new("doc-" + number, topics[index % topics.Length] + " " + number, category,
                category + " / local workspace document"));
        }
        return entries.MoveToImmutable();
    }
    public static StudioCatalogEntry Get(string key)
    {
        ArgumentNullException.ThrowIfNull(key);
        if (key.Length != 9 || !key.StartsWith("doc-", StringComparison.Ordinal) ||
            !int.TryParse(key.AsSpan(4), NumberStyles.None, CultureInfo.InvariantCulture, out int id) ||
            id < 1 || id > Count || Entries[id - 1].Key != key)
            throw new ArgumentException("Unknown studio document identity.", nameof(key));
        return Entries[id - 1];
    }
    public static StudioDocumentDraft Original(string key)
    {
        var entry = Get(key);
        return new(entry.Key, entry.Title,
            entry.Title + "\n\n" +
            "Purpose\nReview the " + entry.Category.ToString().ToLowerInvariant() + " work with the team.\n\n" +
            "Checklist\n- Preserve native editing and keyboard access.\n- Record the decisions and remaining questions.\n- Verify the result before marking it complete.\n\n" +
            "This generated document is local sample data. No user file has been opened.");
    }
}

public sealed record StudioDocumentDraft
{
    public string Key { get; }
    public string Title { get; }
    public string Body { get; }
    [JsonConstructor]
    public StudioDocumentDraft(string key, string title, string body)
    {
        _ = StudioCatalog.Get(key);
        ArgumentNullException.ThrowIfNull(title);
        if (title.AsSpan().IndexOfAny("\0\r\n\u0085\u2028\u2029") >= 0)
            throw new ArgumentException("Document titles must be single-line text without NUL.", nameof(title));
        Key = key;
        Title = title;
        Body = FormValues.NormalizeMultiline(body, 65536);
    }
    [JsonIgnore] public bool CanAnalyze => !string.IsNullOrWhiteSpace(Title) && Title.Length <= 160 && !string.IsNullOrWhiteSpace(Body);
    [JsonIgnore] public string DisplayTitle
    {
        get
        {
            if (string.IsNullOrWhiteSpace(Title)) return "Untitled draft";
            if (Title.Length <= 64) return Title;
            int length = char.IsHighSurrogate(Title[63]) ? 63 : 64;
            return Title[..length] + "...";
        }
    }
    [JsonIgnore] public string Validation => string.IsNullOrWhiteSpace(Title) ? "Enter a document title." :
        Title.Length > 160 ? "Keep the document title within 160 UTF-16 units before analyzing." :
        string.IsNullOrWhiteSpace(Body) ? "Add document content before analyzing." :
        "Local draft only. No file is saved or uploaded.";
    public StudioDocumentDraft WithTitle(string title) => new(Key, title, Body);
    public StudioDocumentDraft WithBody(string body) => new(Key, Title, body);
}

public sealed record WorkspaceStudioSession
{
    public const int MaximumOpenDocuments = 16;
    public int Version { get; }
    public StudioSection Section { get; }
    public StudioCategory Category { get; }
    public string Query { get; }
    public ImmutableArray<string> OpenTabs { get; }
    public string ActiveDocument { get; }
    public string SelectedDocument { get; }
    public ImmutableArray<StudioDocumentDraft> Drafts { get; }
    public bool AnalysisInterrupted { get; }

    [JsonConstructor]
    public WorkspaceStudioSession(int version, StudioSection section, StudioCategory category, string query,
        ImmutableArray<string> openTabs, string activeDocument, string selectedDocument,
        ImmutableArray<StudioDocumentDraft> drafts, bool analysisInterrupted)
    {
        if (version is not (1 or 2)) throw new JsonException("Unsupported studio session version.");
        if (!Enum.IsDefined(section) || !Enum.IsDefined(category)) throw new JsonException("Unknown studio navigation or category.");
        ArgumentNullException.ThrowIfNull(query);
        if (query.Contains('\0')) throw new ArgumentException("Catalog query cannot contain NUL.", nameof(query));
        if (openTabs.IsDefault || drafts.IsDefault) throw new JsonException("Tabs and drafts must be initialized arrays.");
        if (openTabs.Length > MaximumOpenDocuments || drafts.Length > StudioCatalog.Count)
            throw new JsonException("Session exceeds the open-editor or local-draft limit.");
        var keys = new HashSet<string>(StringComparer.Ordinal);
        foreach (string key in openTabs)
        {
            if (key == StudioTabs.OperationsKey)
            {
                if (version < 2) throw new JsonException("Operations tabs require studio session version two.");
            }
            else _ = StudioCatalog.Get(key);
            if (!keys.Add(key)) throw new JsonException("Open workspace tabs must have unique identities.");
        }
        ArgumentNullException.ThrowIfNull(activeDocument);
        ArgumentNullException.ThrowIfNull(selectedDocument);
        if ((openTabs.Length == 0 && activeDocument != "") || (openTabs.Length > 0 && !keys.Contains(activeDocument)))
            throw new JsonException("Active document must identify an open tab, or be empty when no tabs are open.");
        if (selectedDocument != "") _ = StudioCatalog.Get(selectedDocument);
        keys.Clear();
        foreach (var draft in drafts)
        {
            if (draft is null) throw new JsonException("Document drafts cannot be null.");
            if (!keys.Add(draft.Key)) throw new JsonException("Document drafts must have unique identities.");
        }
        Version = version;
        Section = section;
        Category = category;
        Query = query;
        OpenTabs = openTabs;
        ActiveDocument = activeDocument;
        SelectedDocument = selectedDocument;
        Drafts = drafts;
        AnalysisInterrupted = analysisInterrupted;
    }

    public static WorkspaceStudioSession Seed() => new(2, StudioSection.Library, StudioCategory.All, "",
        ["doc-00001", "doc-00002"], "doc-00001", "doc-00001", [], false);
    [JsonIgnore] public StudioDocumentDraft? ActiveDraft => ActiveDocument == "" || !StudioTabs.IsDocument(ActiveDocument) ? null : Document(ActiveDocument);
    [JsonIgnore] public string ActiveCategory => ActiveDocument == "" ? "" :
        StudioTabs.IsDocument(ActiveDocument) ? StudioCatalog.Get(ActiveDocument).Category.ToString() : "Local operations";
    [JsonIgnore] public int ActiveTabIndex => OpenTabs.IndexOf(ActiveDocument);
    [JsonIgnore] public int ChangedCount => Drafts.Length;
    [JsonIgnore] public string QueryValidation => Query.Length > 256 ? "Keep catalog search within 256 UTF-16 units." : "";
    [JsonIgnore] public string Summary => OpenTabs.Length.ToString(CultureInfo.InvariantCulture) + " open / " +
        ChangedCount.ToString(CultureInfo.InvariantCulture) + " local drafts";
    public StudioDocumentDraft Document(string key) => Drafts.FirstOrDefault(draft => draft.Key == key) ?? StudioCatalog.Original(key);
    public bool HasDraft(string key) => Drafts.Any(draft => draft.Key == key);
    public bool CanOpenDocument(string key) => OpenTabs.Contains(key, StringComparer.Ordinal) || OpenTabs.Length < MaximumOpenDocuments;
    public ImmutableArray<string> VisibleKeys()
    {
        if (Query.Length > 256) return [];
        var changed = Drafts.Select(draft => draft.Key).ToHashSet(StringComparer.Ordinal);
        var titles = Drafts.ToDictionary(draft => draft.Key, draft => draft.Title, StringComparer.Ordinal);
        return [.. StudioCatalog.Entries.Where(entry =>
            (Section != StudioSection.Drafts || changed.Contains(entry.Key)) &&
            (Category == StudioCategory.All || Category == entry.Category) &&
            (string.IsNullOrWhiteSpace(Query) || (titles.TryGetValue(entry.Key, out var title) ? title : entry.Title)
                .Contains(Query.Trim(), StringComparison.OrdinalIgnoreCase) || entry.Key.Contains(Query.Trim(), StringComparison.OrdinalIgnoreCase)))
            .Select(entry => entry.Key)];
    }
    public WorkspaceStudioSession Navigate(StudioSection section) => Copy(section: section);
    public WorkspaceStudioSession WithQuery(string query) => Copy(query: query);
    public WorkspaceStudioSession WithCategory(StudioCategory category) => Copy(category: category);
    public WorkspaceStudioSession SelectDocument(string key) { _ = StudioCatalog.Get(key); return Copy(selected: key); }
    public WorkspaceStudioSession OpenDocument(string key)
    {
        _ = StudioCatalog.Get(key);
        if (!CanOpenDocument(key)) throw new InvalidOperationException("Close a tab before opening another document. Local drafts are kept.");
        return Copy(tabs: OpenTabs.Contains(key, StringComparer.Ordinal) ? OpenTabs : OpenTabs.Add(key), active: key, selected: key);
    }
    public WorkspaceStudioSession ActivateTab(string key)
    {
        if (!OpenTabs.Contains(key, StringComparer.Ordinal)) throw new InvalidOperationException("Cannot activate a closed document.");
        return Copy(active: key, selected: StudioTabs.IsDocument(key) ? key : SelectedDocument);
    }
    public WorkspaceStudioSession OpenOperations()
    {
        if (!CanOpenDocument(StudioTabs.OperationsKey)) throw new InvalidOperationException("Close a tab before opening Operations.");
        return Copy(tabs: OpenTabs.Contains(StudioTabs.OperationsKey) ? OpenTabs : OpenTabs.Add(StudioTabs.OperationsKey),
            active: StudioTabs.OperationsKey);
    }
    public WorkspaceStudioSession CloseTab(string key)
    {
        int index = OpenTabs.IndexOf(key);
        if (index < 0) throw new InvalidOperationException("Cannot close a document that is not open.");
        var tabs = OpenTabs.RemoveAt(index);
        string active = ActiveDocument != key ? ActiveDocument : tabs.Length == 0 ? "" : tabs[Math.Min(index, tabs.Length - 1)];
        return Copy(tabs: tabs, active: active);
    }
    public WorkspaceStudioSession MoveTab(string key, int destination)
    {
        int index = OpenTabs.IndexOf(key);
        if (index < 0) throw new InvalidOperationException("Cannot move a closed document.");
        if (destination < 0 || destination >= OpenTabs.Length) throw new ArgumentOutOfRangeException(nameof(destination));
        return Copy(tabs: OpenTabs.RemoveAt(index).Insert(destination, key));
    }
    public WorkspaceStudioSession Edit(StudioDocumentDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        if (!OpenTabs.Contains(draft.Key, StringComparer.Ordinal)) throw new InvalidOperationException("Open the document before editing.");
        int index = -1;
        for (int i = 0; i < Drafts.Length; i++) if (Drafts[i].Key == draft.Key) { index = i; break; }
        var drafts = Drafts;
        if (draft == StudioCatalog.Original(draft.Key)) { if (index >= 0) drafts = drafts.RemoveAt(index); }
        else drafts = index < 0 ? drafts.Add(draft) : drafts.SetItem(index, draft);
        return Copy(drafts: drafts);
    }
    public WorkspaceStudioSession Revert(string key)
    {
        if (!OpenTabs.Contains(key, StringComparer.Ordinal)) throw new InvalidOperationException("Open the document before reverting.");
        return Edit(StudioCatalog.Original(key));
    }
    public WorkspaceStudioSession Interrupted(bool value) => Copy(interrupted: value);
    private WorkspaceStudioSession Copy(StudioSection? section = null, StudioCategory? category = null, string? query = null,
        ImmutableArray<string>? tabs = null, string? active = null, string? selected = null,
        ImmutableArray<StudioDocumentDraft>? drafts = null, bool? interrupted = null) =>
        new(2, section ?? Section, category ?? Category, query ?? Query, tabs ?? OpenTabs, active ?? ActiveDocument,
            selected ?? SelectedDocument, drafts ?? Drafts, interrupted ?? AnalysisInterrupted);
}

public static class WorkspaceStudioSessionCodec
{
    public static string Serialize(WorkspaceStudioSession session) => JsonSerializer.Serialize(
        session ?? throw new ArgumentNullException(nameof(session)), StudioJsonContext.Default.WorkspaceStudioSession);
    public static WorkspaceStudioSession Restore(string json) => JsonSerializer.Deserialize(
        json, StudioJsonContext.Default.WorkspaceStudioSession) ?? throw new JsonException("Studio session cannot be null.");
}

[JsonSourceGenerationOptions(RespectRequiredConstructorParameters = true, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
    AllowDuplicateProperties = false)]
[JsonSerializable(typeof(WorkspaceStudioSession))]
internal partial class StudioJsonContext : JsonSerializerContext;

public sealed record StudioAnalysis(string DocumentKey, int Characters, int Words, int Lines, int OpenTasks)
{
    public static StudioAnalysis Calculate(StudioDocumentDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        if (!draft.CanAnalyze) throw new InvalidOperationException(draft.Validation);
        string[] lines = draft.Body.Split('\n');
        int words = draft.Body.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries).Length;
        int tasks = lines.Count(line => line.TrimStart().StartsWith("- ", StringComparison.Ordinal));
        return new(draft.Key, draft.Body.Length, words, lines.Length, tasks);
    }
    public string Summary => Words.ToString(CultureInfo.InvariantCulture) + " words / " +
        Lines.ToString(CultureInfo.InvariantCulture) + " lines / " + OpenTasks.ToString(CultureInfo.InvariantCulture) + " checklist items";
}

public interface IStudioAnalysisService
{
    Task<StudioAnalysis> AnalyzeAsync(StudioDocumentDraft draft, CancellationToken cancellationToken);
}

public sealed class LocalStudioAnalysisService : IStudioAnalysisService
{
    public async Task<StudioAnalysis> AnalyzeAsync(StudioDocumentDraft draft, CancellationToken cancellationToken)
    {
        await Task.Delay(200, cancellationToken).ConfigureAwait(false);
        cancellationToken.ThrowIfCancellationRequested();
        return StudioAnalysis.Calculate(draft);
    }
}

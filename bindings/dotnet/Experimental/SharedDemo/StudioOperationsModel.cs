using System;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace PortableDemo;

public enum StudioOperationsScope { CurrentCatalog = 1, AllDocuments = 2, LocalDrafts = 3 }

public sealed record StudioWorkloadItem(string Key, string Title, StudioCategory Category, int Words, int ChecklistItems)
{
    public string Summary => Words.ToString(CultureInfo.InvariantCulture) + " words / " +
        ChecklistItems.ToString(CultureInfo.InvariantCulture) + " checklist items";
}

public sealed record StudioOperationsRequest(WorkspaceStudioSession Session, ImmutableArray<string> Keys,
    StudioOperationsScope Scope, long Revision);

public sealed record StudioOperationsReport(long Revision, StudioOperationsScope Scope, int Documents,
    int ModifiedDocuments, int OpenDocuments, int Characters, int Words, int ChecklistItems,
    int EngineeringDocuments, int DesignDocuments, int ResearchDocuments, ImmutableArray<StudioWorkloadItem> Workload)
{
    public string Summary => Documents.ToString(CultureInfo.InvariantCulture) + " documents / " +
        ModifiedDocuments.ToString(CultureInfo.InvariantCulture) + " local drafts";
}

public static class StudioOperations
{
    public static StudioOperationsRequest Capture(WorkspaceStudioSession session, ImmutableArray<string> visibleKeys,
        StudioOperationsScope scope, long revision)
    {
        ArgumentNullException.ThrowIfNull(session);
        if (!Enum.IsDefined(scope)) throw new ArgumentOutOfRangeException(nameof(scope));
        if (revision < 1) throw new ArgumentOutOfRangeException(nameof(revision));
        ImmutableArray<string> keys = scope switch
        {
            StudioOperationsScope.CurrentCatalog => visibleKeys,
            StudioOperationsScope.AllDocuments => [.. StudioCatalog.Entries.Select(entry => entry.Key)],
            StudioOperationsScope.LocalDrafts => [.. session.Drafts.Select(draft => draft.Key).Order(StringComparer.Ordinal)],
            _ => throw new ArgumentOutOfRangeException(nameof(scope))
        };
        if (keys.IsDefault || keys.Length > StudioCatalog.Count ||
            keys.Distinct(StringComparer.Ordinal).Count() != keys.Length)
            throw new ArgumentException("Operations requires a bounded unique document projection.", nameof(visibleKeys));
        foreach (string key in keys) _ = StudioCatalog.Get(key);
        return new(session, keys, scope, revision);
    }
}

public interface IStudioOperationsService
{
    Task<StudioOperationsReport> ScanAsync(StudioOperationsRequest request, CancellationToken cancellationToken);
}

public sealed class LocalStudioOperationsService : IStudioOperationsService
{
    public async Task<StudioOperationsReport> ScanAsync(StudioOperationsRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        var validated = StudioOperations.Capture(request.Session, request.Keys, request.Scope, request.Revision);
        if (!validated.Keys.SequenceEqual(request.Keys)) throw new ArgumentException("Operations scope does not match its captured projection.", nameof(request));
        var drafts = request.Session.Drafts.ToDictionary(draft => draft.Key, StringComparer.Ordinal);
        var opened = request.Session.OpenTabs.ToHashSet(StringComparer.Ordinal);
        var top = new System.Collections.Generic.List<StudioWorkloadItem>(9);
        int modified = 0, open = 0, characters = 0, words = 0, tasks = 0, engineering = 0, design = 0, research = 0;
        await Task.Delay(1, cancellationToken).ConfigureAwait(false);
        for (int index = 0; index < request.Keys.Length; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            string key = request.Keys[index];
            var entry = StudioCatalog.Get(key);
            var document = drafts.TryGetValue(key, out var draft) ? draft : StudioCatalog.Original(key);
            int wordCount = document.Body.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries).Length;
            int checklistCount = document.Body.Split('\n').Count(line => line.TrimStart().StartsWith("- ", StringComparison.Ordinal));
            characters = checked(characters + document.Body.Length);
            words = checked(words + wordCount);
            tasks = checked(tasks + checklistCount);
            if (draft is not null) modified++;
            if (opened.Contains(key)) open++;
            switch (entry.Category)
            {
                case StudioCategory.Engineering: engineering++; break;
                case StudioCategory.Design: design++; break;
                case StudioCategory.Research: research++; break;
                default: throw new InvalidOperationException("Unknown catalog category.");
            }
            top.Add(new(key, document.DisplayTitle, entry.Category, wordCount, checklistCount));
            top.Sort((left, right) =>
            {
                int compare = right.ChecklistItems.CompareTo(left.ChecklistItems);
                if (compare == 0) compare = right.Words.CompareTo(left.Words);
                return compare == 0 ? string.CompareOrdinal(left.Key, right.Key) : compare;
            });
            if (top.Count > 8) top.RemoveAt(8);
            if ((index + 1) % 128 == 0) await Task.Delay(1, cancellationToken).ConfigureAwait(false);
        }
        cancellationToken.ThrowIfCancellationRequested();
        return new(request.Revision, request.Scope, request.Keys.Length, modified, open, characters, words, tasks,
            engineering, design, research, [.. top]);
    }
}

public sealed record StudioOperationsState
{
    public StudioOperationsScope Scope { get; init; } = StudioOperationsScope.CurrentCatalog;
    public long Revision { get; init; } = 1;
    public bool Busy { get; init; }
    public bool CancelRequested { get; init; }
    public StudioOperationsReport? Report { get; init; }
    public string Status { get; init; } = "No operations snapshot yet. Scan local data when ready.";
    public string Error { get; init; } = "";
    public bool Stale => Report is not null && (Report.Revision != Revision || Report.Scope != Scope);
    public bool CanCancel => Busy && !CancelRequested;
    public string Freshness => Report is null ? "NOT COMPUTED" : Stale ? "STALE SNAPSHOT / REFRESH REQUIRED" : "CURRENT LOCAL SNAPSHOT";
}

using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using System.Text.Json.Serialization;

namespace PortableDemo;

public enum DynamicTaskFilter { All, Open, Done }

public sealed record DynamicTaskItem
{
    public long Id { get; }
    public string Title { get; }
    public bool Completed { get; }
    [JsonIgnore] public string Key => "dynamic-task-" + Id.ToString(CultureInfo.InvariantCulture);
    [JsonIgnore] public bool CanToggle => !string.IsNullOrWhiteSpace(Title);
    [JsonIgnore] public string StatusText => Completed ? "Completed" : "Open";
    [JsonIgnore] public string ToggleText => Completed ? "Reopen" : "Complete";
    [JsonIgnore] public string Validation => CanToggle ? "" : "Enter a title before changing status.";

    [JsonConstructor]
    public DynamicTaskItem(long id, string title, bool completed)
    {
        if (id < 1) throw new ArgumentOutOfRangeException(nameof(id));
        Id = id;
        Title = GalleryValues.Text(title);
        Completed = completed;
    }
}

public sealed record DynamicTaskBoardState
{
    public ImmutableArray<DynamicTaskItem> Items { get; }
    public long NextId { get; }
    public string DraftTitle { get; }
    public DynamicTaskFilter Filter { get; }

    public DynamicTaskBoardState() : this([], 1, "", DynamicTaskFilter.All) { }

    [JsonConstructor]
    public DynamicTaskBoardState(ImmutableArray<DynamicTaskItem> items, long nextId, string draftTitle, DynamicTaskFilter filter)
    {
        if (items.IsDefault) throw new ArgumentException("Task items must be an initialized immutable array.", nameof(items));
        if (nextId < 1) throw new ArgumentOutOfRangeException(nameof(nextId));
        var keys = new HashSet<long>();
        foreach (var item in items)
        {
            ArgumentNullException.ThrowIfNull(item);
            if (!keys.Add(item.Id)) throw new ArgumentException("Task IDs must be unique.", nameof(items));
            if (item.Id >= nextId) throw new ArgumentOutOfRangeException(nameof(nextId), "Next ID must exceed every saved task ID.");
        }
        Items = items;
        NextId = nextId;
        DraftTitle = GalleryValues.Text(draftTitle);
        Filter = GalleryValues.Defined(filter);
    }

    public static DynamicTaskBoardState Seed() => new(
        [new(1, "Design sign-in", true), new(2, "Build shared UI", false), new(3, "Write guide", false)],
        4, "", DynamicTaskFilter.All);

    [JsonIgnore] public int CompletedCount => Items.Count(item => item.Completed);
    [JsonIgnore] public ImmutableArray<DynamicTaskItem> VisibleItems =>
        [.. Items.Where(item => Filter == DynamicTaskFilter.All || item.Completed == (Filter == DynamicTaskFilter.Done))];
    [JsonIgnore] public bool CanAdd => !string.IsNullOrWhiteSpace(DraftTitle) && NextId < long.MaxValue;
    [JsonIgnore] public string Summary => Items.Length.ToString(CultureInfo.InvariantCulture) + " tasks / " +
        CompletedCount.ToString(CultureInfo.InvariantCulture) + " done";
    [JsonIgnore] public string Validation => NextId == long.MaxValue ? "No task IDs remain in this board." :
        string.IsNullOrWhiteSpace(DraftTitle) ? "Enter a title to add a task." : "Ready to add. Changes are local.";

    public DynamicTaskBoardState WithDraft(string title) => new(Items, NextId, title, Filter);
    public DynamicTaskBoardState WithFilter(DynamicTaskFilter filter) => new(Items, NextId, DraftTitle, filter);
    public DynamicTaskBoardState Reverse() => new([.. Items.Reverse()], NextId, DraftTitle, Filter);
    public DynamicTaskBoardState AddDraft()
    {
        if (!CanAdd) throw new InvalidOperationException(Validation);
        return new(Items.Add(new(NextId, DraftTitle, false)), NextId + 1, "", DynamicTaskFilter.All);
    }
    public DynamicTaskBoardState Rename(string key, string title)
    {
        int index = IndexOf(key);
        var item = Items[index];
        return new(Items.SetItem(index, new(item.Id, title, item.Completed)), NextId, DraftTitle, Filter);
    }
    public DynamicTaskBoardState Toggle(string key)
    {
        int index = IndexOf(key);
        var item = Items[index];
        if (!item.CanToggle) throw new InvalidOperationException(item.Validation);
        return new(Items.SetItem(index, new(item.Id, item.Title, !item.Completed)), NextId, DraftTitle, Filter);
    }
    public DynamicTaskBoardState Remove(string key) => new(Items.RemoveAt(IndexOf(key)), NextId, DraftTitle, Filter);

    private int IndexOf(string key)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        for (int i = 0; i < Items.Length; i++)
            if (Items[i].Key == key) return i;
        throw new KeyNotFoundException($"Task '{key}' does not exist.");
    }
}

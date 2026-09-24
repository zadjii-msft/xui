using System;
using System.Text.Json.Serialization;

namespace PortableDemo;

public enum TaskStage { Todo, Doing, Done }
public enum TaskFilter { All, Open, Done }

public sealed record PriorityTask
{
    private string title = "";
    private TaskStage stage;
    [JsonRequired] public string Title { get => title; init => title = GalleryValues.Text(value); }
    [JsonRequired] public TaskStage Stage { get => stage; init => stage = GalleryValues.Defined(value); }
    public string StageText => Stage switch
    {
        TaskStage.Todo => "To do",
        TaskStage.Doing => "Doing",
        TaskStage.Done => "Done",
        _ => throw new InvalidOperationException("Unknown task stage.")
    };
}

public sealed record TaskBoardState
{
    private PriorityTask first = new() { Title = "Design sign-in", Stage = TaskStage.Done };
    private PriorityTask second = new() { Title = "Build shared UI", Stage = TaskStage.Doing };
    private PriorityTask third = new() { Title = "Write guide", Stage = TaskStage.Todo };
    private int selectedSlot = 2;
    private TaskFilter filter;

    [JsonRequired] public PriorityTask First { get => first; init => first = value ?? throw new ArgumentNullException(nameof(value)); }
    [JsonRequired] public PriorityTask Second { get => second; init => second = value ?? throw new ArgumentNullException(nameof(value)); }
    [JsonRequired] public PriorityTask Third { get => third; init => third = value ?? throw new ArgumentNullException(nameof(value)); }
    [JsonRequired] public int SelectedSlot { get => selectedSlot; init => selectedSlot = GalleryValues.Slot(value); }
    [JsonRequired] public TaskFilter Filter { get => filter; init => filter = GalleryValues.Defined(value); }
    public PriorityTask Selected => SelectedSlot switch { 1 => First, 2 => Second, 3 => Third, _ => throw new InvalidOperationException() };
    public int CompletedCount => (First.Stage == TaskStage.Done ? 1 : 0) +
        (Second.Stage == TaskStage.Done ? 1 : 0) + (Third.Stage == TaskStage.Done ? 1 : 0);
    public string ProgressText => GalleryValues.Number(CompletedCount) + " of 3 complete";
    public string FirstText => "01  " + First.Title;
    public string SecondText => "02  " + Second.Title;
    public string ThirdText => "03  " + Third.Title;
    public bool FirstVisible => Matches(First);
    public bool SecondVisible => Matches(Second);
    public bool ThirdVisible => Matches(Third);
    public bool EmptyFilter => !FirstVisible && !SecondVisible && !ThirdVisible;
    public string FilterText => Filter switch
    {
        TaskFilter.All => "Showing all priorities",
        TaskFilter.Open => "Showing open priorities",
        TaskFilter.Done => "Showing completed priorities",
        _ => throw new InvalidOperationException("Unknown task filter.")
    };
    public string EditorText => "Edit priority " + GalleryValues.Number(SelectedSlot);
    public string SelectedStatusText => "Status: " + Selected.StageText;
    public string NextStatusText => Selected.Stage switch
    {
        TaskStage.Todo => "Start task",
        TaskStage.Doing => "Complete task",
        TaskStage.Done => "Reopen task",
        _ => throw new InvalidOperationException("Unknown task stage.")
    };
    public bool CanAdvance => !string.IsNullOrWhiteSpace(Selected.Title);
    public string Validation => CanAdvance ? "Edits stay in this local board." : "Enter a title before changing status.";

    public TaskBoardState RenameSelected(string title) => ReplaceSelected(Selected with { Title = title });
    public TaskBoardState AdvanceSelected()
    {
        if (!CanAdvance) return this;
        var next = Selected.Stage switch
        {
            TaskStage.Todo => TaskStage.Doing,
            TaskStage.Doing => TaskStage.Done,
            TaskStage.Done => TaskStage.Todo,
            _ => throw new InvalidOperationException("Unknown task stage.")
        };
        return ReplaceSelected(Selected with { Stage = next });
    }

    private bool Matches(PriorityTask task) => Filter == TaskFilter.All ||
        (Filter == TaskFilter.Open ? task.Stage != TaskStage.Done : task.Stage == TaskStage.Done);
    private TaskBoardState ReplaceSelected(PriorityTask task) => SelectedSlot switch
    {
        1 => this with { First = task },
        2 => this with { Second = task },
        3 => this with { Third = task },
        _ => throw new InvalidOperationException("Unknown priority slot.")
    };
}

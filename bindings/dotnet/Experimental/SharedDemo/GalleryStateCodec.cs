using System;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace PortableDemo;

public static class GalleryStateCodec
{
    public static string Serialize(TaskBoardState state) =>
        JsonSerializer.Serialize(state ?? throw new ArgumentNullException(nameof(state)), GalleryJsonContext.Default.TaskBoardState);
    public static string Serialize(ExpenseLedgerState state) =>
        JsonSerializer.Serialize(state ?? throw new ArgumentNullException(nameof(state)), GalleryJsonContext.Default.ExpenseLedgerState);
    public static string Serialize(SessionPlannerState state) =>
        JsonSerializer.Serialize(state ?? throw new ArgumentNullException(nameof(state)), GalleryJsonContext.Default.SessionPlannerState);

    public static TaskBoardState RestoreTaskBoard(string json) =>
        JsonSerializer.Deserialize(json, GalleryJsonContext.Default.TaskBoardState) ?? throw new JsonException("Task board state cannot be null.");
    public static ExpenseLedgerState RestoreExpenseLedger(string json) =>
        JsonSerializer.Deserialize(json, GalleryJsonContext.Default.ExpenseLedgerState) ?? throw new JsonException("Expense ledger state cannot be null.");
    public static SessionPlannerState RestoreSessionPlanner(string json) =>
        JsonSerializer.Deserialize(json, GalleryJsonContext.Default.SessionPlannerState) ?? throw new JsonException("Session planner state cannot be null.");
}

[JsonSourceGenerationOptions(IgnoreReadOnlyProperties = true, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow)]
[JsonSerializable(typeof(TaskBoardState))]
[JsonSerializable(typeof(ExpenseLedgerState))]
[JsonSerializable(typeof(SessionPlannerState))]
internal partial class GalleryJsonContext : JsonSerializerContext;

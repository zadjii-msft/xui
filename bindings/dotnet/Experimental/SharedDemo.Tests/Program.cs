using System.Globalization;
using System.Text.Json;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static int assertions;
    private static void Main()
    {
        ModelChecks();
        CodecChecks();
        RecordingChecks();
        Console.WriteLine($"Shared application gallery: {assertions} assertions passed.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { assertions++; return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void ModelChecks()
    {
        var board = new TaskBoardState();
        Assert(board.ProgressText == "1 of 3 complete" && board.SelectedSlot == 2 && board.Selected.Title == "Build shared UI", "Board seed.");
        var done = board.AdvanceSelected();
        Assert(done.ProgressText == "2 of 3 complete" && done.Selected.Stage == TaskStage.Done && board.Selected.Stage == TaskStage.Doing, "Immutable task completion.");
        Assert(done.AdvanceSelected().Selected.Stage == TaskStage.Todo && done.AdvanceSelected().AdvanceSelected().Selected.Stage == TaskStage.Doing, "Status cycle.");
        var blank = board.RenameSelected(" \t ");
        Assert(!blank.CanAdvance && ReferenceEquals(blank, blank.AdvanceSelected()) && blank.Validation == "Enter a title before changing status.", "Blank title blocks transition with explicit validation.");
        var renamed = board.RenameSelected("  Zo\u00eb \u674e  ");
        Assert(renamed.Selected.Title == "  Zo\u00eb \u674e  " && renamed.First == board.First && renamed.Third == board.Third, "Native text and unselected tasks preserved.");
        var open = board with { Filter = TaskFilter.Open };
        Assert(!open.FirstVisible && open.SecondVisible && open.ThirdVisible && !open.EmptyFilter, "Open filter.");
        var completed = board with { Filter = TaskFilter.Done };
        Assert(completed.FirstVisible && !completed.SecondVisible && !completed.ThirdVisible, "Completed filter.");
        var empty = completed with { First = completed.First with { Stage = TaskStage.Todo } };
        Assert(empty.EmptyFilter && empty.FilterText == "Showing completed priorities", "Empty filter is explicit.");
        Throws<ArgumentOutOfRangeException>(() => _ = board with { SelectedSlot = 0 });
        Throws<ArgumentOutOfRangeException>(() => _ = board with { SelectedSlot = 4 });
        Throws<ArgumentOutOfRangeException>(() => _ = board with { Filter = (TaskFilter)8 });
        Throws<ArgumentOutOfRangeException>(() => _ = new PriorityTask { Stage = (TaskStage)8 });
        Throws<ArgumentNullException>(() => _ = board with { First = null! });
        Throws<ArgumentNullException>(() => board.RenameSelected(null!));
        Throws<ArgumentException>(() => board.RenameSelected("a\0b"));

        var ledger = new ExpenseLedgerState();
        Assert(ledger.Total == 75.25m && ledger.Remaining == 74.75m && ledger.CanReview && !ledger.Reviewing, "Literal ledger seed arithmetic.");
        Assert(ledger.Review().ReviewSummary == "Within budget by $74.75." && !ledger.Reviewing, "Local immutable ledger review.");
        var over = ledger with { Budget = "50" };
        Assert(over.Remaining == -25.25m && over.RemainingText == "Over: $25.25" && over.Review().ReviewSummary == "Over budget by $25.25.", "Over-budget values aren't clamped.");
        var cents = ledger with { Budget = "0.30", Food = "0.10", Travel = "0.20", Supplies = "0" };
        Assert(cents.Total == 0.30m && cents.Remaining == 0m && cents.RemainingText == "Left: $0.00", "Exact decimal arithmetic.");
        var max = ledger with { Budget = "999999.99", Food = "999999.99", Travel = "999999.99", Supplies = "999999.99" };
        Assert(max.Total == 2999999.97m && max.Remaining == -1999999.98m, "Literal maximum category sum.");
        foreach (string invalid in new[] { "", " ", ".", ".50", "1.", "1.234", "-1", "+1", "1,00", "1,000", "$1", "1e2", "1..0", "NaN", "1000000", "999999.999", "\u0661" })
        {
            var bad = ledger with { Food = invalid };
            Assert(!bad.CanReview && bad.Total is null && bad.Remaining is null && bad.TotalText == "Spent: --" && bad.RemainingText == "Left: --", $"Invalid money '{invalid}' isn't a zero total.");
            Assert(bad.Validation == "Food: use 0 to 999999.99, up to 2 decimals." && ReferenceEquals(bad.Review(), bad), "Invalid money has explicit validation.");
        }
        foreach (string valid in new[] { "0", "000.10", " 12.50 ", "999999.99" })
            Assert((ledger with { Food = valid }).CanReview, "Supported money input.");
        Assert((ledger with { Budget = "" }).TotalText == "Spent: $75.25" && (ledger with { Budget = "" }).RemainingText == "Left: --", "Invalid budget doesn't fabricate remaining amount.");
        Throws<ArgumentNullException>(() => _ = ledger with { Budget = null! });
        Throws<ArgumentException>(() => _ = ledger with { Travel = "a\0b" });

        var planner = new SessionPlannerState();
        Assert(planner.FocusMinutes == 80 && planner.TotalMinutes == 90 && planner.FinishText == "Finish: 10:30", "Literal planner seed arithmetic.");
        Assert(planner.Review().ReviewSummary == "90 min including two breaks\nFinish: 10:30\nNo timer is running.", "Planner review is not a running timer.");
        var overnight = planner with { StartTime = "23:45", FirstMinutes = "10", SecondMinutes = "20", ThirdMinutes = "15", BreakMinutes = "3" };
        Assert(overnight.FocusMinutes == 45 && overnight.TotalMinutes == 51 && overnight.FinishText == "Finish: 00:36 (+1 day)", "Literal next-day schedule.");
        var midnight = planner with { StartTime = "23:57", FirstMinutes = "1", SecondMinutes = "1", ThirdMinutes = "1", BreakMinutes = "0" };
        Assert(midnight.FinishText == "Finish: 00:00 (+1 day)" && midnight.TotalMinutes == 3, "Midnight boundary and zero breaks.");
        var longest = planner with { StartTime = "23:59", FirstMinutes = "180", SecondMinutes = "180", ThirdMinutes = "180", BreakMinutes = "60" };
        Assert(longest.TotalMinutes == 660 && longest.FinishText == "Finish: 10:59 (+1 day)", "Maximum plan arithmetic.");
        foreach (string invalid in new[] { "", "9:00", "24:00", "00:60", "12:000", "0 :00", "00: 0", "aa:bb", "12.00", "-1:00" })
        {
            var bad = planner with { StartTime = invalid };
            Assert(!bad.CanReview && bad.FinishText == "Finish: --" && bad.Validation == "Start: use 24-hour HH:mm, 00:00 to 23:59.", "Malformed time is explicit.");
            Assert(ReferenceEquals(bad, bad.Review()), "Cannot review invalid time.");
        }
        foreach (string invalid in new[] { "", "0", "181", "-1", "+1", "1.0", "1e2", "2147483648" })
        {
            var bad = planner with { SecondMinutes = invalid };
            Assert(!bad.CanReview && bad.FocusText == "Focus: --" && bad.DurationText == "Duration: --" && bad.Validation == "Focus 2: use 1 to 180 whole minutes.", "Invalid focus length cannot masquerade as zero.");
        }
        foreach (string invalid in new[] { "", "-1", "61", "0.5" })
            Assert(!(planner with { BreakMinutes = invalid }).CanReview, "Break limits.");
        Assert((planner with { StartTime = " 09:00 ", FirstMinutes = " 25 " }).FinishText == "Finish: 10:30", "Surrounding whitespace accepted without rewriting.");
        Throws<ArgumentNullException>(() => _ = planner with { StartTime = null! });
        Throws<ArgumentException>(() => _ = planner with { ThirdMinutes = "1\0" });

        var culture = CultureInfo.CurrentCulture;
        try
        {
            foreach (string name in new[] { "fr-FR", "tr-TR", "ar-SA" })
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo(name);
                Assert(ledger.TotalText == "Spent: $75.25" && ledger.RemainingText == "Left: $74.75", "USD display stays invariant.");
                Assert(planner.FocusText == "80 min focus" && planner.FinishText == "Finish: 10:30", "Time display stays invariant.");
                Assert(board.ProgressText == "1 of 3 complete", "Task count stays invariant.");
            }
        }
        finally { CultureInfo.CurrentCulture = culture; }
    }

    private static void CodecChecks()
    {
        Assert(!JsonSerializer.IsReflectionEnabledByDefault, "All state serialization runs with reflection disabled.");
        var task = new TaskBoardState().RenameSelected("  Zo\u00eb \u674e  ") with { Filter = TaskFilter.Open };
        var ledger = new ExpenseLedgerState { Food = "invalid draft", Reviewing = false };
        var planner = new SessionPlannerState { StartTime = "23:57", FirstMinutes = "", Reviewing = false };
        Assert(GalleryStateCodec.RestoreTaskBoard(GalleryStateCodec.Serialize(task)) == task, "Typed task state roundtrip.");
        Assert(GalleryStateCodec.RestoreExpenseLedger(GalleryStateCodec.Serialize(ledger)) == ledger, "Invalid ledger drafts survive restoration.");
        Assert(GalleryStateCodec.RestoreSessionPlanner(GalleryStateCodec.Serialize(planner)) == planner, "Invalid planner drafts survive restoration.");
        Assert(!GalleryStateCodec.Serialize(task).Contains("ProgressText", StringComparison.Ordinal), "Computed properties aren't serialized.");
        Assert(!GalleryStateCodec.Serialize(ledger).Contains("Total", StringComparison.Ordinal), "Computed money isn't serialized.");
        Assert(!GalleryStateCodec.Serialize(planner).Contains("FinishMinute", StringComparison.Ordinal), "Computed time isn't serialized.");
        Throws<JsonException>(() => GalleryStateCodec.RestoreTaskBoard("null"));
        Throws<JsonException>(() => GalleryStateCodec.RestoreExpenseLedger("{\"Unknown\":true}"));
        Throws<JsonException>(() => GalleryStateCodec.RestoreSessionPlanner("{"));
        Throws<JsonException>(() => GalleryStateCodec.RestoreTaskBoard("{}"));
        Throws<JsonException>(() => GalleryStateCodec.RestoreExpenseLedger("{}"));
        Throws<JsonException>(() => GalleryStateCodec.RestoreSessionPlanner("{}"));
        Throws<ArgumentOutOfRangeException>(() => GalleryStateCodec.RestoreTaskBoard(GalleryStateCodec.Serialize(task).Replace("\"SelectedSlot\":2", "\"SelectedSlot\":4")));
        Throws<ArgumentNullException>(() => GalleryStateCodec.Serialize((TaskBoardState)null!));
    }
}

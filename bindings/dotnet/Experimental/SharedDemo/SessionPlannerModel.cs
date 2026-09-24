using System.Text.Json.Serialization;

namespace PortableDemo;

public sealed record SessionPlannerState
{
    private string startTime = "09:00";
    private string firstMinutes = "25";
    private string secondMinutes = "35";
    private string thirdMinutes = "20";
    private string breakMinutes = "5";

    [JsonRequired] public string StartTime { get => startTime; init => startTime = GalleryValues.Text(value); }
    [JsonRequired] public string FirstMinutes { get => firstMinutes; init => firstMinutes = GalleryValues.Text(value); }
    [JsonRequired] public string SecondMinutes { get => secondMinutes; init => secondMinutes = GalleryValues.Text(value); }
    [JsonRequired] public string ThirdMinutes { get => thirdMinutes; init => thirdMinutes = GalleryValues.Text(value); }
    [JsonRequired] public string BreakMinutes { get => breakMinutes; init => breakMinutes = GalleryValues.Text(value); }
    [JsonRequired] public bool Reviewing { get; init; }

    public int? StartMinute => GalleryValues.StartMinute(StartTime);
    public int? FirstLength => GalleryValues.Minutes(FirstMinutes, 1, 180);
    public int? SecondLength => GalleryValues.Minutes(SecondMinutes, 1, 180);
    public int? ThirdLength => GalleryValues.Minutes(ThirdMinutes, 1, 180);
    public int? BreakLength => GalleryValues.Minutes(BreakMinutes, 0, 60);
    public int? FocusMinutes => FirstLength + SecondLength + ThirdLength;
    public int? TotalMinutes => FocusMinutes + BreakLength * 2;
    public int? FinishMinute => StartMinute + TotalMinutes;
    public bool CanReview => FinishMinute.HasValue;
    public string FocusText => FocusMinutes is int value ? GalleryValues.Number(value) + " min focus" : "Focus: --";
    public string FinishText => FinishMinute is int value
        ? "Finish: " + GalleryValues.Clock(value) + (value >= 1440 ? " (+1 day)" : "")
        : "Finish: --";
    public string DurationText => TotalMinutes is int value
        ? GalleryValues.Number(value) + " min including two breaks"
        : "Duration: --";
    public string Validation
    {
        get
        {
            if (!StartMinute.HasValue) return "Start: use 24-hour HH:mm, 00:00 to 23:59.";
            if (!FirstLength.HasValue) return "Focus 1: use 1 to 180 whole minutes.";
            if (!SecondLength.HasValue) return "Focus 2: use 1 to 180 whole minutes.";
            if (!ThirdLength.HasValue) return "Focus 3: use 1 to 180 whole minutes.";
            if (!BreakLength.HasValue) return "Break: use 0 to 60 whole minutes.";
            return Reviewing ? "Plan reviewed. No timer is running." : "Local plan only. No timer or notifications.";
        }
    }
    public string ReviewSummary => Reviewing && CanReview
        ? DurationText + "\n" + FinishText + "\nNo timer is running."
        : "";
    public SessionPlannerState Review() => CanReview ? this with { Reviewing = true } : this;
}

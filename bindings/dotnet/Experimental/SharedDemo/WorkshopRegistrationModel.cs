using System;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed record WorkshopRegistrationState
{
    private string name = "";
    private string email = "";
    private string seats = "1";
    private string goals = "";
    [JsonRequired] public string Name { get => name; init => name = GalleryValues.Text(value); }
    [JsonRequired] public string Email { get => email; init => email = GalleryValues.Text(value); }
    [JsonRequired] public string Seats { get => seats; init => seats = GalleryValues.Text(value); }
    [JsonRequired] public string Goals
    {
        get => goals;
        init => goals = FormValues.NormalizeMultiline(value, 500);
    }
    [JsonRequired] public bool InPerson { get; init; }
    [JsonRequired] public bool Consent { get; init; }
    [JsonRequired] public bool Reviewing { get; init; }

    public int? SeatCount => GalleryValues.Minutes(Seats, 1, 6);
    public decimal UnitPrice => InPerson ? 45m : 20m;
    public decimal? Total => SeatCount * UnitPrice;
    public bool EmailValid => new OrderState { Email = Email }.EmailIsValid;
    public bool CanReview => !string.IsNullOrWhiteSpace(Name) && EmailValid && SeatCount.HasValue &&
        !string.IsNullOrWhiteSpace(Goals) && Consent;
    public string AttendanceText => InPerson ? "In person / $45.00 per seat" : "Online / $20.00 per seat";
    public string TotalText => Total is decimal value ? "Local estimate: $" + value.ToString("0.00", CultureInfo.InvariantCulture) : "Local estimate: --";
    public string Validation => string.IsNullOrWhiteSpace(Name) ? "Enter the attendee name." :
        !EmailValid ? "Enter an email address with a dotted domain." :
        !SeatCount.HasValue ? "Choose 1 to 6 whole seats. The number keyboard is only a hint." :
        string.IsNullOrWhiteSpace(Goals) ? "Describe at least one learning goal." :
        !Consent ? "Confirm this is a local review, not a booking." :
        Reviewing ? "Review only. No registration, email, or payment was sent." : "Ready for a local review.";
    public string ReviewSummary => Reviewing && CanReview
        ? Name + "\n" + Email + "\n" + AttendanceText + "\nSeats: " + SeatCount!.Value.ToString(CultureInfo.InvariantCulture) +
            "\n" + TotalText + "\nLearning goals:\n" + Goals + "\nNo registration placed."
        : "";
    public WorkshopRegistrationState Review() => CanReview ? this with { Reviewing = true } : this;
}

public static class WorkshopRegistrationCodec
{
    public static string Serialize(WorkshopRegistrationState state) => JsonSerializer.Serialize(
        state ?? throw new ArgumentNullException(nameof(state)), WorkshopJsonContext.Default.WorkshopRegistrationState);
    public static WorkshopRegistrationState Restore(string json) => JsonSerializer.Deserialize(
        json, WorkshopJsonContext.Default.WorkshopRegistrationState) ?? throw new JsonException("Workshop draft cannot be null.");
}

[JsonSourceGenerationOptions(IgnoreReadOnlyProperties = true, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
    AllowDuplicateProperties = false)]
[JsonSerializable(typeof(WorkshopRegistrationState))]
internal partial class WorkshopJsonContext : JsonSerializerContext;

using System.Text.Json.Serialization;

namespace PortableDemo;

public sealed record ExpenseLedgerState
{
    private string budget = "150.00";
    private string food = "32.50";
    private string travel = "18.00";
    private string supplies = "24.75";

    [JsonRequired] public string Budget { get => budget; init => budget = GalleryValues.Text(value); }
    [JsonRequired] public string Food { get => food; init => food = GalleryValues.Text(value); }
    [JsonRequired] public string Travel { get => travel; init => travel = GalleryValues.Text(value); }
    [JsonRequired] public string Supplies { get => supplies; init => supplies = GalleryValues.Text(value); }
    [JsonRequired] public bool Reviewing { get; init; }

    public decimal? BudgetAmount => GalleryValues.Amount(Budget);
    public decimal? FoodAmount => GalleryValues.Amount(Food);
    public decimal? TravelAmount => GalleryValues.Amount(Travel);
    public decimal? SuppliesAmount => GalleryValues.Amount(Supplies);
    public decimal? Total => FoodAmount + TravelAmount + SuppliesAmount;
    public decimal? Remaining => BudgetAmount - Total;
    public bool CanReview => Remaining.HasValue;
    public string TotalText => Total is decimal value ? "Spent: " + GalleryValues.Money(value) : "Spent: --";
    public string RemainingText => Remaining is decimal value
        ? (value >= 0 ? "Left: " + GalleryValues.Money(value) : "Over: " + GalleryValues.Money(-value))
        : "Left: --";
    public string Validation
    {
        get
        {
            if (!BudgetAmount.HasValue) return AmountError("Budget");
            if (!FoodAmount.HasValue) return AmountError("Food");
            if (!TravelAmount.HasValue) return AmountError("Travel");
            if (!SuppliesAmount.HasValue) return AmountError("Supplies");
            return Reviewing ? "Local review only. No money moved." : "Amounts use USD. Ready to review.";
        }
    }
    public string ReviewSummary => Reviewing && Remaining is decimal value
        ? (value >= 0 ? "Within budget by " + GalleryValues.Money(value) + "." : "Over budget by " + GalleryValues.Money(-value) + ".")
        : "";

    public ExpenseLedgerState Review() => CanReview ? this with { Reviewing = true } : this;
    private static string AmountError(string field) => field + ": use 0 to 999999.99, up to 2 decimals.";
}

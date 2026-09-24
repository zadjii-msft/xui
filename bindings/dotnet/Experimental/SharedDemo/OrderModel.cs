using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace PortableDemo;

public sealed record OrderState
{
    public const decimal CoffeePrice = 12.50m;
    public const decimal TeaPrice = 8.00m;
    public const decimal CocoaPrice = 10.00m;
    public const int MaximumQuantity = 9;

    private string customerName = "";
    private string email = "";
    private string discountCode = "";
    private int coffeeQuantity;
    private int teaQuantity;
    private int cocoaQuantity;

    public string CustomerName { get => customerName; init => customerName = CheckText(value); }
    public string Email { get => email; init => email = CheckText(value); }
    public string DiscountCode { get => discountCode; init => discountCode = CheckText(value); }
    public int CoffeeQuantity { get => coffeeQuantity; init => coffeeQuantity = CheckQuantity(value); }
    public int TeaQuantity { get => teaQuantity; init => teaQuantity = CheckQuantity(value); }
    public int CocoaQuantity { get => cocoaQuantity; init => cocoaQuantity = CheckQuantity(value); }
    public bool Reviewing { get; init; }

    public bool HasItems => CoffeeQuantity + TeaQuantity + CocoaQuantity > 0;
    public bool NameIsValid => !string.IsNullOrWhiteSpace(CustomerName);
    public bool EmailIsValid
    {
        get
        {
            string value = Email.Trim();
            int at = value.IndexOf('@');
            if (at <= 0 || at != value.LastIndexOf('@') || value.Any(c => char.IsWhiteSpace(c) || char.IsControl(c)))
                return false;
            string local = value[..at];
            if (local[0] == '.' || local[^1] == '.' || local.Contains("..") ||
                !local.All(c => char.IsLetterOrDigit(c) || ".!#$%&'*+-/=?^_`{|}~".Contains(c)))
                return false;
            string domain = value[(at + 1)..];
            return domain.Contains('.') && domain.Split('.').All(label => label.Length > 0 &&
                label[0] != '-' && label[^1] != '-' && label.All(c => char.IsLetterOrDigit(c) || c == '-'));
        }
    }
    public bool HasDiscount => string.Equals(DiscountCode.Trim(), "SAVE10", StringComparison.OrdinalIgnoreCase);
    public bool CouponIsValid => string.IsNullOrWhiteSpace(DiscountCode) || HasDiscount;
    public bool CanReview => HasItems && NameIsValid && EmailIsValid && CouponIsValid;

    public decimal Subtotal => CoffeeQuantity * CoffeePrice + TeaQuantity * TeaPrice + CocoaQuantity * CocoaPrice;
    public decimal Discount => HasDiscount ? decimal.Round(Subtotal * 0.10m, 2, MidpointRounding.AwayFromZero) : 0m;
    public decimal Total => Subtotal - Discount;
    public string CoffeeQuantityText => "Coffee: " + CoffeeQuantity.ToString(CultureInfo.InvariantCulture);
    public string TeaQuantityText => "Tea: " + TeaQuantity.ToString(CultureInfo.InvariantCulture);
    public string CocoaQuantityText => "Cocoa: " + CocoaQuantity.ToString(CultureInfo.InvariantCulture);
    public string SubtotalText => "Subtotal: " + Money(Subtotal);
    public string DiscountText => "Discount: -" + Money(Discount);
    public string TotalText => "Total: " + Money(Total);
    public string Validation
    {
        get
        {
            var errors = new List<string>();
            if (!HasItems) errors.Add("Choose at least one item.");
            if (!NameIsValid) errors.Add("Enter your name.");
            if (!EmailIsValid) errors.Add("Enter a valid email address.");
            if (!CouponIsValid) errors.Add("Use SAVE10 or leave the discount code empty.");
            if (errors.Count > 0) return string.Join(" ", errors);
            return Reviewing ? "Review only: no order has been placed." : "Ready to review. No payment will be taken.";
        }
    }
    public string ReviewSummary => Reviewing
        ? $"Local review for {CustomerName}\nEmail: {Email}\n{CoffeeQuantityText}\n{TeaQuantityText}\n{CocoaQuantityText}\n{TotalText}\nNo order placed."
        : "";

    private static string Money(decimal value) => "$" + value.ToString("0.00", CultureInfo.InvariantCulture);
    private static int CheckQuantity(int value)
    {
        if (value < 0 || value > MaximumQuantity) throw new ArgumentOutOfRangeException(nameof(value), "Quantity must be between 0 and 9.");
        return value;
    }
    private static string CheckText(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.Contains('\0')) throw new ArgumentException("Text cannot contain NUL.", nameof(value));
        return value;
    }
}

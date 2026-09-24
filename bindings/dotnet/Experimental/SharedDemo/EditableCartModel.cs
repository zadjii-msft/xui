using System;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;

namespace PortableDemo;

public sealed record CartProduct(string Id, string Name, decimal UnitPrice, string Description);

public static class CartCatalog
{
    public static ImmutableArray<CartProduct> Products { get; } =
    [
        new("coffee", "Coffee", OrderState.CoffeePrice, "A bag of whole-bean coffee. Local catalog example."),
        new("tea", "Tea", OrderState.TeaPrice, "A box of loose-leaf tea. Local catalog example."),
        new("cocoa", "Cocoa", OrderState.CocoaPrice, "A tin of cocoa powder. Local catalog example.")
    ];
    public static CartProduct Get(string id) => Products.SingleOrDefault(product => product.Id == id) ??
        throw new ArgumentException("Unknown cart product.", nameof(id));
    public static string Money(decimal value) => "$" + value.ToString("0.00", CultureInfo.InvariantCulture);
}

public sealed record CartLine
{
    public long Id { get; }
    public string ProductId { get; }
    public string QuantityText { get; }

    [JsonConstructor]
    public CartLine(long id, string productId, string quantityText)
    {
        if (id < 1) throw new ArgumentOutOfRangeException(nameof(id));
        _ = CartCatalog.Get(productId);
        Id = id;
        ProductId = productId;
        QuantityText = GalleryValues.Text(quantityText);
    }
    [JsonIgnore] public string Key => "cart-line-" + Id.ToString(CultureInfo.InvariantCulture);
    [JsonIgnore] public CartProduct Product => CartCatalog.Get(ProductId);
    [JsonIgnore] public int? Quantity => GalleryValues.Minutes(QuantityText, 1, 9);
    [JsonIgnore] public decimal? Total => Quantity * Product.UnitPrice;
    [JsonIgnore] public string Title => Product.Name + " / " + CartCatalog.Money(Product.UnitPrice) + " each";
    [JsonIgnore] public string TotalText => Total is decimal amount ? "Line total: " + CartCatalog.Money(amount) : "Line total: --";
    [JsonIgnore] public string Validation => Quantity.HasValue ? "" : "Use 1 to 9 whole items, or remove this line.";
}

public sealed record EditableCartDraft
{
    public ImmutableArray<CartLine> Lines { get; }
    public long NextId { get; }
    public string CustomerName { get; }
    public string DiscountCode { get; }

    public EditableCartDraft() : this([], 1, "", "") { }

    [JsonConstructor]
    public EditableCartDraft(ImmutableArray<CartLine> lines, long nextId, string customerName, string discountCode)
    {
        if (lines.IsDefault) throw new ArgumentException("Cart lines must be initialized.", nameof(lines));
        if (nextId < 1) throw new ArgumentOutOfRangeException(nameof(nextId));
        if (lines.Any(line => line is null)) throw new ArgumentException("Cart lines cannot be null.", nameof(lines));
        if (lines.Select(line => line.Id).Distinct().Count() != lines.Length)
            throw new ArgumentException("Cart line identities must be unique.", nameof(lines));
        if (lines.Any(line => line.Id >= nextId)) throw new ArgumentOutOfRangeException(nameof(nextId));
        Lines = lines;
        NextId = nextId;
        CustomerName = GalleryValues.Text(customerName);
        DiscountCode = GalleryValues.Text(discountCode);
    }

    [JsonIgnore] public bool CanAdd => NextId < long.MaxValue;
    [JsonIgnore] public bool HasDiscount => string.Equals(DiscountCode.Trim(), "SAVE10", StringComparison.OrdinalIgnoreCase);
    [JsonIgnore] public bool CouponValid => string.IsNullOrWhiteSpace(DiscountCode) || HasDiscount;
    [JsonIgnore] public bool QuantitiesValid => Lines.All(line => line.Quantity.HasValue);
    [JsonIgnore] public decimal? Subtotal => QuantitiesValid ? Lines.Sum(line => line.Total!.Value) : null;
    [JsonIgnore] public decimal? Discount => Subtotal is decimal amount
        ? (HasDiscount ? decimal.Round(amount * 0.10m, 2, MidpointRounding.AwayFromZero) : 0m) : null;
    [JsonIgnore] public decimal? Total => CouponValid ? Subtotal - Discount : null;
    [JsonIgnore] public bool CanQuote => Lines.Length > 0 && QuantitiesValid && CouponValid && !string.IsNullOrWhiteSpace(CustomerName);
    [JsonIgnore] public string Summary => Lines.Length.ToString(CultureInfo.InvariantCulture) + " cart lines";
    [JsonIgnore] public string SubtotalText => Subtotal is decimal value ? "Subtotal: " + CartCatalog.Money(value) : "Subtotal: --";
    [JsonIgnore] public string DiscountText => Discount is decimal value ? "Discount: -" + CartCatalog.Money(value) : "Discount: --";
    [JsonIgnore] public string TotalText => Total is decimal value ? "Estimate: " + CartCatalog.Money(value) : "Estimate: --";
    [JsonIgnore] public string Validation => Lines.Length == 0 ? "Browse the catalog and add an item." :
        !QuantitiesValid ? "Correct the quantity on each cart line." :
        string.IsNullOrWhiteSpace(CustomerName) ? "Enter a name for the local quote." :
        !CouponValid ? "Use SAVE10 or leave the discount code empty." :
        "Ready for a local quote. No checkout or payment.";

    public EditableCartDraft Add(string productId)
    {
        if (!CanAdd) throw new InvalidOperationException("No cart line identities remain.");
        return new(Lines.Add(new(NextId, productId, "1")), NextId + 1, CustomerName, DiscountCode);
    }
    public EditableCartDraft Remove(string key) => new(Lines.RemoveAt(IndexOf(key)), NextId, CustomerName, DiscountCode);
    public EditableCartDraft ChangeQuantity(string key, string text)
    {
        int index = IndexOf(key);
        var line = Lines[index];
        return new(Lines.SetItem(index, new(line.Id, line.ProductId, text)), NextId, CustomerName, DiscountCode);
    }
    public EditableCartDraft Reverse() => new([.. Lines.Reverse()], NextId, CustomerName, DiscountCode);
    public EditableCartDraft Sort() => new([.. Lines.OrderBy(line => line.Product.Name, StringComparer.Ordinal).ThenBy(line => line.Id)], NextId, CustomerName, DiscountCode);
    public EditableCartDraft WithName(string text) => new(Lines, NextId, text, DiscountCode);
    public EditableCartDraft WithDiscount(string text) => new(Lines, NextId, CustomerName, text);
    private int IndexOf(string key)
    {
        for (int index = 0; index < Lines.Length; index++)
            if (Lines[index].Key == key) return index;
        throw new ArgumentException("Unknown cart line.", nameof(key));
    }
}

public enum CartPage { Browse, Detail, Edit }

public sealed record CartRoute
{
    public CartPage Page { get; }
    public string ProductId { get; }
    [JsonConstructor]
    public CartRoute(CartPage page, string productId)
    {
        if (!Enum.IsDefined(page)) throw new JsonException("Unknown cart route.");
        if (page == CartPage.Detail) _ = CartCatalog.Get(productId);
        else if (productId != "") throw new JsonException("Only a product detail route has a product ID.");
        Page = page;
        ProductId = productId;
    }
}

public sealed record EditableCartSession
{
    public int Version { get; }
    public EditableCartDraft Draft { get; }
    public ImmutableArray<CartRoute> Routes { get; }
    public bool Interrupted { get; }
    [JsonConstructor]
    public EditableCartSession(int version, EditableCartDraft draft, ImmutableArray<CartRoute> routes, bool interrupted)
    {
        if (version != 1) throw new JsonException("Unsupported editable cart session version.");
        if (draft is null) throw new JsonException("Cart draft cannot be null.");
        if (routes.IsDefaultOrEmpty || routes.Length > 3 || routes.Any(route => route is null) ||
            routes[0].Page != CartPage.Browse ||
            (routes.Length >= 2 && routes[1].Page == CartPage.Browse) ||
            (routes.Length == 3 && (routes[1].Page != CartPage.Detail || routes[2].Page != CartPage.Edit)))
            throw new JsonException("Cart routes must be Browse, optionally Detail, optionally Edit.");
        Version = version;
        Draft = draft;
        Routes = routes;
        Interrupted = interrupted;
    }
}

public static class EditableCartSessionCodec
{
    public static string Serialize(EditableCartSession session) => JsonSerializer.Serialize(
        session ?? throw new ArgumentNullException(nameof(session)), CartJsonContext.Default.EditableCartSession);
    public static EditableCartSession Restore(string json) => JsonSerializer.Deserialize(
        json, CartJsonContext.Default.EditableCartSession) ?? throw new JsonException("Cart session cannot be null.");
}

[JsonSourceGenerationOptions(RespectRequiredConstructorParameters = true,
    UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow, AllowDuplicateProperties = false)]
[JsonSerializable(typeof(EditableCartSession))]
internal partial class CartJsonContext : JsonSerializerContext;

public sealed record CartQuote(decimal Subtotal, decimal Discount, decimal Total)
{
    public static CartQuote Calculate(EditableCartDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        if (!draft.CanQuote) throw new InvalidOperationException(draft.Validation);
        return new(draft.Subtotal!.Value, draft.Discount!.Value, draft.Total!.Value);
    }
}

public interface ICartQuoteService
{
    Task<CartQuote> QuoteAsync(EditableCartDraft draft, CancellationToken cancellationToken);
}

public sealed class LocalCartQuoteService : ICartQuoteService
{
    public async Task<CartQuote> QuoteAsync(EditableCartDraft draft, CancellationToken cancellationToken)
    {
        await Task.Delay(250, cancellationToken).ConfigureAwait(false);
        return CartQuote.Calculate(draft);
    }
}

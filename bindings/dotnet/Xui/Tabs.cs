namespace Xui;

/// <summary>A stable tab identity and title with an optional vector fallback and asynchronous image path.</summary>
public readonly record struct TabEntry(ulong Id, string Title,
    ButtonIcon Icon = ButtonIcon.None, string ImagePath = "");

public sealed unsafe partial class TabStrip
{
    /// <summary>Replaces tabs without selection events. Images use the shared bounded worker and cache.</summary>
    public TabStrip SetTabItems(ReadOnlySpan<TabEntry> items, ulong? selected = null)
    {
        Window.Guard();
        if (items.Length > 4096) throw new ArgumentOutOfRangeException(nameof(items));
        using var pins = new Window.Pins();
        var entries = new Native.Choice[items.Length];
        var visuals = new Native.ItemVisual[items.Length];
        for (int i = 0; i < items.Length; ++i)
        {
            var item = items[i];
            entries[i] = new() { Size = (uint)sizeof(Native.Choice), Id = item.Id, Text = pins.Text(item.Title) };
            visuals[i] = new() { Size = (uint)sizeof(Native.ItemVisual), Icon = (uint)item.Icon,
                ImagePath = pins.Text(item.ImagePath) };
        }
        fixed (Native.Choice* p = entries)
        fixed (Native.ItemVisual* v = visuals)
            Window.Check(Native.TabItemsVisual(Handle, p, v, (uint)items.Length, selected ?? 0, selected.HasValue ? 1u : 0u));
        return this;
    }
}

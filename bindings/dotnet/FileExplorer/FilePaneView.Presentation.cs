using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed partial class FilePaneView
{
    private string presentationDateFormat = "yyyy-MM-dd HH:mm";
    internal ExplorerCustomization PresentationSettings => app.State.Customization;
    internal event Action PresentationChanged
    {
        add => app.CustomizationChanged += value;
        remove => app.CustomizationChanged -= value;
    }

    public void ApplyCustomization()
    {
        var settings = PresentationSettings;
        var typography = new PartStyleValues { FontFamily = settings.FontFamily, FontSize = settings.FontSize,
            RowHeight = settings.RowHeight };
        Grid.SetControlStyleValues(StylePart.Root, new() { RowHeight = settings.RowHeight });
        Grid.SetControlStyleValues(StylePart.Cell, typography with { RowHeight = null });
        Grid.SetControlStyleValues(StylePart.Header, typography with { RowHeight = null });
        Tree.SetPresentationFontSize(ExplorerPresentation.CompactFontSize(settings));
        Tree.SetControlStyleValues(StylePart.Root, typography with { Indentation = 16, Padding = new(0),
            RowHeight = Math.Max(20, settings.RowHeight - 8), FontSize = ExplorerPresentation.CompactFontSize(settings) });
        Items.SetControlStyleValues(StylePart.Root, typography);
        Grid.SetPresentation(settings.SingleClick);
        Tree.SetPresentation(settings.SingleClick);
        Items.SetPresentation(settings.SingleClick, settings.ThumbnailFill);
        for (uint i = 0; i < ExplorerTab.ColumnLimit; i++)
        {
            var list = Columns.Column(i);
            list.SetControlStyleValues(StylePart.Root, typography);
            list.SetPresentation(settings.SingleClick);
        }
        Tabs.Duration = settings.Animations ? 180u : 0u;
        FindReveal.Duration = settings.Animations ? 180u : 0u;
        ApplyItemSize();
        if (presentationDateFormat != settings.DateFormat)
        {
            presentationDateFormat = settings.DateFormat;
            if (displayedTab != 0 && !IsLoading)
            {
                SaveViewport();
                ApplyFilter();
            }
        }
    }

    private void ApplyItemSize()
    {
        var (width, imageHeight) = Model.Active.ViewMode switch
        {
            ExplorerViewMode.ExtraLargeIcons => (256f, 256f),
            ExplorerViewMode.LargeIcons => (160f, 160f),
            ExplorerViewMode.MediumIcons => (96f, 96f),
            _ => (180f, 0f)
        };
        var settings = PresentationSettings;
        float height = imageHeight + settings.RowHeight;
        Items.ItemSize(width, height);
        Items.SetControlStyleValues(StylePart.Root, new() { FontFamily = settings.FontFamily,
            FontSize = settings.FontSize, RowHeight = height });
    }
}

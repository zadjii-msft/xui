using Android.Util;
using Android.Views;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private void StudioRowGeometryChecks(AndroidDispatcher dispatcher)
    {
        WrappedButtonGeometryChecks(dispatcher);
        foreach (float scale in new[] { 1f, 2f })
        foreach (int width in new[] { 320, 1400 })
        foreach (bool dirty in new[] { false, true })
        {
            using var configuration = new global::Android.Content.Res.Configuration(Resources!.Configuration!) { FontScale = scale };
            using var configured = CreateConfigurationContext(configuration)
                ?? throw new InvalidOperationException("Cannot create the native Studio row font-scale context.");
            using var context = new ContextThemeWrapper(configured, global::Android.Resource.Style.ThemeMaterialLightNoActionBar);
            using var surface = new OrderSurface(context);
            using var host = new Host(dispatcher);
            var errors = new System.Collections.Concurrent.ConcurrentQueue<Exception>();
            using var controller = new WorkspaceStudioController(host, new LocalStudioAnalysisService(), errors.Enqueue);
            string title = dirty ? new string('W', 160) : StudioCatalog.Original("doc-00001").Title;
            var row = new StudioCatalogRow(host, "doc-00001", controller)
            {
                Document = new StudioDocumentDraft("doc-00001", title, "body"),
                Category = "Engineering",
                Changed = dirty
            };
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var root = (ElementFrame)surface.GetChildAt(0)!;
            var open = (TextView)backend.FindViews("doc-00001-open").Single();
            var caption = (TextView)backend.FindViews("doc-00001-category").Single();
            var heading = (TextView)backend.FindViews("doc-00001-catalog-title").Single();
            float density = context.Resources!.DisplayMetrics!.Density;
            int pixels = Xui.Experimental.Android.LayoutMath.Pixels(width, density);
            root.MeasureWith(MeasureConstraint.Exactly(pixels), MeasureConstraint.Unspecified);
            root.Layout(0, 0, root.MeasuredWidth, root.MeasuredHeight);
            int natural = root.Height;
            Assert(heading.Text == title && heading.Layout!.LineCount == 1,
                "The real Studio row keeps its complete accessible title while drawing a native single-line label.");
            foreach (int pitch in new[] { 96, 128 })
            {
                row.Root.SetHeight(pitch);
                root.MeasureWith(MeasureConstraint.Exactly(pixels), MeasureConstraint.Unspecified);
                root.Layout(0, 0, root.MeasuredWidth, root.MeasuredHeight);
                int requiredOpen = open.Layout!.Height + open.CompoundPaddingTop + open.CompoundPaddingBottom;
                int requiredCaption = caption.Layout!.Height + caption.CompoundPaddingTop + caption.CompoundPaddingBottom;
                int Y(View view)
                {
                    int y = 0;
                    for (View? current = view; current is not null && current != root; current = current.Parent as View) y += current.Top;
                    return y;
                }
                bool complete = open.Height >= requiredOpen && caption.Height >= requiredCaption &&
                    Y(open) + open.Height <= root.Height && Y(caption) + caption.Height <= root.Height;
                Log.Info("Xui.Android.Orders",
                    $"Studio row: key=doc-00001 fontScale={scale} width={width} dirty={dirty} pitch={pitch}dp natural={natural / density:F3}dp " +
                    $"root={root.Height}px open={open.Height}/{requiredOpen}px atY{Y(open)} caption={caption.Height}/{requiredCaption}px atY{Y(caption)} complete={complete}.");
                if (pitch == 128)
                {
                    Assert(complete, "The exact shared Studio row candidate fits both native child contents within 128dp.");
                    Assert(open.Width > 0 && open.Height > 0 && caption.Width > 0 && caption.Height > 0,
                        "The actual native Open target and caption retain nonzero geometry.");
                }
            }
            controller.Dispose();
            host.Dispose();
            Assert(errors.IsEmpty, "The real shared row/controller produced no asynchronous errors during geometry inspection.");
        }
    }

    private void WrappedButtonGeometryChecks(AndroidDispatcher dispatcher)
    {
        foreach (float scale in new[] { 1f, 2f })
        foreach (int width in new[] { 280, 320, 360 })
        {
            using var configuration = new global::Android.Content.Res.Configuration(Resources!.Configuration!) { FontScale = scale };
            using var configured = CreateConfigurationContext(configuration)
                ?? throw new InvalidOperationException("Cannot create the native wrapped-button context.");
            using var context = new ContextThemeWrapper(configured, global::Android.Resource.Style.ThemeMaterialLightNoActionBar);
            using var surface = new OrderSurface(context);
            using var host = new Host(dispatcher);
            using var controller = new WorkspaceStudioController(host, new LocalStudioAnalysisService(), _ => { });
            var editor = new StudioDocumentEditor(host, "doc-00001", controller);
            var backend = new AndroidBackend(surface, dispatcher);
            host.Attach(backend);
            var root = (ElementFrame)surface.GetChildAt(0)!;
            int pixels = Xui.Experimental.Android.LayoutMath.Pixels(width, context.Resources!.DisplayMetrics!.Density);
            root.MeasureWith(MeasureConstraint.Exactly(pixels), MeasureConstraint.Unspecified);
            root.Layout(0, 0, root.MeasuredWidth, root.MeasuredHeight);
            foreach (string id in new[] { "doc-00001-analyze", "doc-00001-revert" })
            {
                var button = (TextView)backend.FindViews(id).Single();
                int required = button.Layout!.Height + button.CompoundPaddingTop + button.CompoundPaddingBottom;
                Log.Info("Xui.Android.Orders", $"Studio button: fontScale={scale} width={width} id={id} nativeWidth={button.Width} lines={button.Layout.LineCount} height={button.Height} required={required}.");
                Assert(button.Height >= required, "The exact shared Studio button fits every wrapped native caption line and font padding.");
            }
        }
    }
}

using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeCatalogTextMetrics()
    {
        foreach (float width in new[] { 212f, 228f })
        foreach (float scale in new[] { 1f, 2f })
        {
            using var application = new Application();
            using var window = application.CreateWindow("Catalog text metrics", 520, 760, visualStyle: VisualStyle.WinUI);
            var surface = Surface(window);
            using var dispatcher = new WindowsDispatcher(window);
            using var host = new P.Host(dispatcher);
            using var controller = new WorkspaceStudioController(host, new LocalStudioAnalysisService(),
                error => throw new InvalidOperationException("Unexpected diagnostic application work.", error));
            StudioCatalogRow[] rows;
            using (var build = host.BeginBuild())
            {
                var root = host.Stack(P.Axis.Vertical).Spacing(12);
                rows = new[] { 0, 96, 128 }.Select(pitch =>
                {
                    var row = new StudioCatalogRow(host, "doc-00001", controller)
                    {
                        Document = StudioCatalog.Original("doc-00001").WithTitle(new string('W', 160)),
                        Changed = true
                    };
                    row.Root.SetWidth(width);
                    if (pitch != 0) row.Root.SetHeight(pitch);
                    row.TitleLabel.Typography = new P.Typography(fontSize: 14 * scale);
                    row.OpenButton.Typography = new P.Typography(fontSize: 14 * scale);
                    ((P.Label)row.Root.Children[1]).Typography = new P.Typography(P.TextRole.Caption, fontSize: 12 * scale);
                    root.Add(row.Root);
                    return row;
                }).ToArray();
                host.SetContent(root);
                build.Complete();
            }
            var backend = new WindowsBackend(surface, dispatcher);
            host.Attach(backend);
            application.Show(window);
            var ui = new NativeUi(application);
            ElementBounds Bounds(P.Element element) => backend.Peer(element).Native.GetBounds();
            var work = Task.Run(() =>
            {
                try
                {
                    ui.Ui(() =>
                    {
                        var referenceRoot = Bounds(rows[0].Root);
                        var referenceButton = Bounds(rows[0].OpenButton);
                        var referenceCaption = Bounds(rows[0].Root.Children[1]);
                        Console.WriteLine($"Native catalog text size multiplier={scale}, width={width} DIP: unconstrained root={referenceRoot.Height}, Open={referenceButton.Height}, caption={referenceCaption.Height}; padding=16, gap=4; no DPI scaling substitution.");
                        foreach (int index in new[] { 1, 2 })
                        {
                            var root = Bounds(rows[index].Root);
                            var button = Bounds(rows[index].OpenButton);
                            var caption = Bounds(rows[index].Root.Children[1]);
                            bool fits = button.Height + 0.1f >= referenceButton.Height &&
                                caption.Height + 0.1f >= referenceCaption.Height &&
                                caption.Y + caption.Height <= root.Y + root.Height - 8 + 0.1f;
                            Console.WriteLine($"  allocated pitch={root.Height}: Open={button.Height}, caption={caption.Height}, complete native lines={fits}.");
                            Check(((Label)backend.Peer(rows[index].TitleLabel).Native).Text.Length == 160,
                                "Catalog title ellipsis shortened the source/accessibility string.");
                        }
                        host.Detach();
                    });
                }
                finally { ui.Ui(window.Close); }
            });
            RunNativeWork(application, work);
        }
    }
}

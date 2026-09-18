using Xui;

internal static class FeatureDemo
{
    private sealed class Rows : IReadOnlyImmutableSource
    {
        public int FetchedRows;
        public ulong Count => 1000000;
        public ItemKey Key(ulong index) => new(index + 1, 1);
        public ulong? Find(ItemKey key) => key.Version == 1 && key.Id > 0 && key.Id <= Count ? key.Id - 1 : null;
        public ItemContent Item(ulong index, ulong column = 0) { ++FetchedRows; return new($"Record {index + 1}", "Immutable virtual source", Progress: (index % 100) / 100.0); }
    }
    internal static void Run(bool fail)
    {
        using var w = new Window("XUI feature bindings", 700, 800);
        w.IconErrorHandler = error => throw new InvalidOperationException($"Cannot load the application icon: {error}");
        w.SetIconSource(Path.Combine(AppContext.BaseDirectory, "zoey.ico"));
        var root = w
            .Stack()
            .Padding(16)
            .Spacing(8);
        var status = w.Label("F6: dialog. Escape: cancel. F8: change range. F12: close.");
        var combo = w.ComboBox("Presentation")
            .SetItems([new(1,"List"),new(2,"Tiles")],1);
        var range = w.RangeInput("Zoom")
            .SetRange(new(0,100))
            .SetValue(20);
        var data = new Rows();
        var items = w.ItemsView("One million rows");
        using (var source = w.ImmutableSource(data)) items = items.SetSource(source);
        var map = w.MapView("Offline map")
            .SetView(new(47.6,-122.3),4)
            .SetMarkers([new(1,new(47.6,-122.3),"Seattle")])
            .FixedSize(650,180);
        var edit = w.Button("Edit document");
        var document = w.MultilineText("Notes")
            .SetText("Authored text 😀")
            .FixedSize(390,80);
        var color = w.ColorPicker("Accent")
            .SetValue(new(30,100,220));
        var form = w.Stack()
            .Spacing(8)
            .Add(document)
            .Add(color);
        var dialog = w.ContentDialog("Document and color",form);
        dialog.OnResult(accepted => status.SetText(accepted ? "Accepted" : "Canceled"));
        edit.Click += () => dialog.Show(edit);
        range.Event += e =>
        {
            if (e.Kind != EventKind.Change) return;
            if (fail) throw new InvalidOperationException("Feature callback sentinel");
            status.SetText($"Range: {range.Value}");
        };
        combo.Event += e => items.SetPresentation(e.Value == 1 ? ItemsPresentation.List : ItemsPresentation.Tiles);
        w.SetContent(root.Add(status).Add(combo).Add(range).Add(edit).Add(map).Add(items,1));
        w.Key += e =>
        {
            switch (e.Value & 0xffff)
            {
                case 0x75: dialog.Show(edit); break;
                case 0x77: range.ChangeValue(25); break;
                case 0x7b: w.Close(); break;
            }
        };
        w.Run();
        if (data.FetchedRows == 0 || data.FetchedRows >= 4096) throw new InvalidOperationException("Virtual source exceeded its visible-query budget.");
        Console.WriteLine($"Million-row source fetched {data.FetchedRows} visible rows.");
    }
}

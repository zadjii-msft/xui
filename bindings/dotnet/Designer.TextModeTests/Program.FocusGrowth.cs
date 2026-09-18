using Xui;

internal static partial class Program
{
    private static void RunFocusGrowth(VisualStyle style)
    {
        using var window = new Window("Designer property focus growth", 480, 320, visualStyle: style);
        window.SetShowActivated(false);
        var body = window.Stack().Spacing(8);
        var fields = Enumerable.Range(0, 64)
            .Select(index => window.TextInput($"Property {index}").SetText($"Value {index}")).ToArray();
        foreach (var field in fields) body.Add(field.FixedSize(360, 60));
        var scroll = window.ScrollView(body, "Properties");
        window.SetContent(window.Stack().Add(scroll, 1));
        Exception? failure = null;
        int assertions = 0;
        if (!window.Post(() =>
        {
            try
            {
                for (int round = 0; round < 2; round++)
                {
                    int index = 0;
                    foreach (var field in round == 0 ? fields : fields.Reverse())
                    {
                        bool selectAll = round == 0 && index % 2 == 0;
                        string text = field.Text;
                        field.Selection = new(1, 3);
                        field.Focus(selectAll);
                        var expected = selectAll ? new TextSelection(0, (ulong)text.Length) : new TextSelection(1, 3);
                        if (!field.Focused || field.Selection != expected || field.Text != text)
                            throw new InvalidOperationException("Property focus must preserve the native selection after peer growth.");
                        var bounds = field.GetBounds();
                        var viewport = scroll.GetBounds();
                        if (bounds.Y < viewport.Y || bounds.Y + bounds.Height > viewport.Y + viewport.Height)
                            throw new InvalidOperationException("Focusing a property must reveal it inside the scroll viewport.");
                        assertions += 2;
                        index++;
                    }
                }
                if (window.CallbackStatus != 0) throw new InvalidOperationException("Property focus reported a native callback failure.");
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        })) throw new InvalidOperationException("The focus growth window rejected its action.");
        window.Run();
        if (failure is not null) throw new InvalidOperationException($"{style}: property focus growth failed.", failure);
        if (assertions != fields.Length * 4) throw new InvalidOperationException("Property focus growth did not finish.");
        Console.WriteLine($"Designer property focus growth assertions ({style}): {assertions} passed.");
    }
}

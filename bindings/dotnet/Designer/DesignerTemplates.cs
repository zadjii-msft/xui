using System.Collections.ObjectModel;
using System.Text;

namespace Xui.Designer;

internal sealed record DesignerTemplate(string Id, string Name, string Description, string Source);

internal static class DesignerTemplates
{
    internal static ReadOnlyCollection<DesignerTemplate> All { get; } = Array.AsReadOnly<DesignerTemplate>(
    [
        Load("blank", "Blank canvas", "A stack ready for controls.", "Templates.Blank"),
        Load("counter", "Interactive counter", "State, event handlers, typography, and button styles.", "Starter"),
        Load("form", "Contact form", "Native text inputs, a toggle, and a submit action.", "Templates.ContactForm"),
        Load("settings", "Settings panel", "Two-way state updates and theme-aware styles.", "Templates.Settings"),
        Load("dashboard", "Dashboard", "A responsive grid with cards and actions.", "Templates.Dashboard"),
        Load("split", "Split workspace", "A resizable split view with native text input.", "Templates.SplitWorkspace"),
        Load("values", "Value controls", "A native range input updates state and a progress meter.", "Templates.ValueControls")
    ]);

    internal static DesignerTemplate Get(string id) =>
        All.FirstOrDefault(template => StringComparer.Ordinal.Equals(template.Id, id))
        ?? throw new ArgumentException($"Unknown designer template '{id}'.", nameof(id));

    private static DesignerTemplate Load(string id, string name, string description, string resource)
    {
        using var stream = typeof(DesignerTemplates).Assembly.GetManifestResourceStream($"Designer.{resource}.xui")
            ?? throw new InvalidOperationException($"The designer template '{id}' is missing.");
        using var reader = new StreamReader(stream, new UTF8Encoding(false, true));
        return new(id, name, description, reader.ReadToEnd());
    }
}

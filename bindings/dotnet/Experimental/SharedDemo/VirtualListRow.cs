using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed class VirtualListRow : IPortableComponent
{
    public Element Root { get; }
    public TextInput Input { get; }
    internal bool NativeSelectionReady { get; set; }

    public VirtualListRow(Host host, VirtualListItem item,
        Action<TextInteraction>? interactionChanged = null, Action? submitted = null)
    {
        using var scope = host.BeginBuild();
        var root = host.Stack(Axis.Vertical).Padding(8);
        Input = host.TextInput($"Task {item.Number + 1:N0}");
        Input.AutomationId = $"virtual-{item.Key}";
        Input.Help = "Edits and selection stay with this task. Enter advances after composition ends. Tab follows realized controls.";
        Input.SetCaptionVisible(false);
        Input.Text = item.Draft;
        Input.Changed += value => { NativeSelectionReady = true; item.Draft = value; };
        if (interactionChanged is not null) Input.InteractionChanged += interactionChanged;
        if (submitted is not null) Input.Submitted += submitted;
        root.Add(Input);
        host.SetContent(root);
        scope.Complete();
        Root = root;
    }
}

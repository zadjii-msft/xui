using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Stack = Xui.Experimental.Portable.Stack;

namespace Xui.Experimental.Web;

/// <summary>The caller owns the imported module. The host owns this backend after attachment starts.</summary>
public sealed class DomBackend : IBackend
{
    private readonly IJSInProcessObjectReference surface;
    private int nextId;
    private int peers;
    private bool disposed;

    public DomBackend(IJSInProcessObjectReference module, string mountId, string errorId)
    {
        ArgumentNullException.ThrowIfNull(module);
        surface = module.Invoke<IJSInProcessObjectReference>("createSurface", mountId, errorId);
    }

    public IElementPeer Create(Element element, IControlEvents events)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(element);
        ArgumentNullException.ThrowIfNull(events);
        var peer = new DomPeer(this, checked(++nextId), element, events);
        peers++;
        try { peer.Create(); return peer; }
        catch (Exception error)
        {
            try { peer.Dispose(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    public void Mount(IElementPeer root)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (root is not DomPeer peer || peer.Owner != this)
            throw new ArgumentException("The root belongs to another DOM backend.", nameof(root));
        surface.InvokeVoid("mount", peer.Id);
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        Cleanup(() => surface.InvokeVoid("unmount"), ReleaseIfEmpty);
    }

    private void ReleaseIfEmpty()
    {
        if (disposed && peers == 0) surface.Dispose();
    }

    private static void Cleanup(params Action[] actions)
    {
        List<Exception>? errors = null;
        foreach (var action in actions)
        {
            try { action(); }
            catch (Exception error) { (errors ??= []).Add(error); }
        }
        if (errors is not null) throw new AggregateException(errors);
    }

    public sealed class DomPeer : IElementPeer
    {
        internal DomBackend Owner { get; }
        internal int Id { get; }
        private readonly Element element;
        private readonly IControlEvents events;
        private readonly DotNetObjectReference<DomPeer> reference;
        private bool disposed;
        private int textRevision;

        internal DomPeer(DomBackend owner, int id, Element element, IControlEvents events)
        {
            Owner = owner;
            Id = id;
            this.element = element;
            this.events = events;
            reference = DotNetObjectReference.Create(this);
        }

        internal void Create()
        {
            var control = element as Control;
            var stack = element as Stack;
            var input = element as TextInput;
            Owner.surface.InvokeVoid("create", Id, new
            {
                kind = element.Kind.ToString(),
                axis = stack?.Axis.ToString(),
                flex = element.Flex,
                fixedSize = element.FixedSize,
                preferredSize = element.PreferredSize,
                spacing = stack?.SpacingValue ?? 0,
                padding = stack?.PaddingValue ?? 0,
                name = control?.Name ?? "",
                automationId = control?.AutomationId ?? "",
                help = control?.Help ?? "",
                enabled = control?.Enabled ?? true,
                visible = control?.Visible ?? true,
                text = input?.Text ?? "",
                placeholder = input?.Placeholder ?? "",
                captionVisible = input?.CaptionVisible ?? true
            }, reference);
        }

        public void AddChild(IElementPeer child)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (child is not DomPeer peer || peer.Owner != Owner)
                throw new ArgumentException("The child belongs to another DOM backend.", nameof(child));
            Owner.surface.InvokeVoid("addChild", Id, peer.Id);
        }

        public void Update(ElementProperty property)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            object? value = property switch
            {
                ElementProperty.Name => ((Control)element).Name,
                ElementProperty.AutomationId => ((Control)element).AutomationId,
                ElementProperty.Enabled => ((Control)element).Enabled,
                ElementProperty.Visible => ((Control)element).Visible,
                ElementProperty.Help => ((Control)element).Help,
                ElementProperty.Text => ((TextInput)element).Text,
                ElementProperty.Placeholder => ((TextInput)element).Placeholder,
                ElementProperty.CaptionVisible => ((TextInput)element).CaptionVisible,
                ElementProperty.Spacing => ((Stack)element).SpacingValue,
                ElementProperty.Padding => ((Stack)element).PaddingValue,
                ElementProperty.FixedSize => element.FixedSize,
                ElementProperty.PreferredSize => element.PreferredSize,
                _ => throw new ArgumentOutOfRangeException(nameof(property))
            };
            if (property == ElementProperty.Text) checked { textRevision++; }
            Owner.surface.InvokeVoid("update", Id, property.ToString(), value, textRevision);
        }

        [JSInvokable]
        public bool Deliver(string kind, string? text, int revision)
        {
            if (disposed || Owner.disposed) return false;
            if (revision < 0) throw new ArgumentOutOfRangeException(nameof(revision));
            return kind switch
            {
                "click" when element is Button && text is null => events.Click(),
                "change" when element is TextInput && text is not null =>
                    revision == textRevision && events.Change(text),
                "submit" when element is TextInput && text is null => events.Submit(),
                _ => throw new ArgumentException("Invalid DOM event payload.", nameof(kind))
            };
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            Cleanup(() => Owner.surface.InvokeVoid("destroy", Id), reference.Dispose, () =>
            {
                Owner.peers--;
                Owner.ReleaseIfEmpty();
            });
        }
    }
}

using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Stack = Xui.Experimental.Portable.Stack;
using Peer = Xui.Experimental.Web.DomBackend.DomPeer;

namespace Xui.Experimental.Web;

internal sealed class DomLayoutCache
{
    internal readonly record struct Measurement(Size Size, bool WidthUnbounded, bool HeightUnbounded);
    private readonly Dictionary<Peer, Dictionary<(MeasureConstraint Width, MeasureConstraint Height), Measurement>> measured = [];
    private readonly Dictionary<Peer, Dictionary<(MeasureMode Mode, float Width), Size>> native = [];

    internal void Clear() { measured.Clear(); native.Clear(); }
    internal void Invalidate(Peer peer)
    {
        for (Peer? current = peer; current is not null; current = current.NativeParent)
        {
            measured.Remove(current);
            native.Remove(current);
        }
    }
    internal bool TryMeasure(Peer peer, MeasureConstraint width, MeasureConstraint height, out Measurement size)
    {
        size = default;
        return measured.TryGetValue(peer, out var values) && values.TryGetValue((width, height), out size);
    }
    internal void Measure(Peer peer, MeasureConstraint width, MeasureConstraint height, Measurement size)
    {
        if (!measured.TryGetValue(peer, out var values)) measured[peer] = values = [];
        if (values.Count >= 8) values.Clear();
        values[(width, height)] = size;
    }
    internal bool TryNative(Peer peer, MeasureMode mode, float width, out Size size)
    {
        size = default;
        return native.TryGetValue(peer, out var values) && values.TryGetValue((mode, width), out size);
    }
    internal void Native(Peer peer, MeasureMode mode, float width, Size size)
    {
        if (!native.TryGetValue(peer, out var values)) native[peer] = values = [];
        if (values.Count >= 8) values.Clear();
        values[(mode, width)] = size;
    }
}

internal sealed class DomLayout(IJSInProcessObjectReference surface, DomLayoutCache cache)
{
    private sealed class State(Element element)
    {
        internal Element Element { get; } = element;
        internal AxisConstraints? Width { get; } = element.WidthConstraints;
        internal AxisConstraints? Height { get; } = element.HeightConstraints;
        internal Size? Fixed { get; } = element.FixedSize;
        internal Size? Preferred { get; } = element.PreferredSize;
        internal float Flex { get; } = element.Flex;
        internal bool Visible { get; } = element is not Control { Visible: false } && element is not PageView { Visible: false };
        internal GridPlacement? Cell { get; } = element.Cell;
        internal float Padding { get; } = element is Stack stack ? stack.PaddingValue : 0;
        internal float Spacing { get; } = element is Stack stack ? stack.SpacingValue : 0;
    }
    private readonly Dictionary<Peer, State> states = [];
    private readonly Dictionary<Peer, (bool Width, bool Height)> unbounded = [];
    private readonly List<int> frameIds = [];
    private readonly List<float> frameBounds = [];
    private readonly List<bool> frameContainers = [];

    internal void ArrangeRoot(Peer root, Size viewport)
    {
        Measure(root, MeasureConstraint.Exactly(viewport.Width), MeasureConstraint.Exactly(viewport.Height));
        Arrange(root, 0, 0, viewport.Width, viewport.Height);
        surface.InvokeVoid("applyPackedFrames", frameIds.ToArray(), frameBounds.ToArray(), frameContainers.ToArray());
    }

    private State Read(Peer peer)
    {
        if (!states.TryGetValue(peer, out var state)) states[peer] = state = new(peer.Element);
        return state;
    }
    private Peer[] Visible(Peer peer) => peer.NativeChildren.Where((child, index) =>
        Read(child).Visible && (peer.Element is not PageView pages ||
            (index < pages.Pages.Count && pages.Pages[index].Id == pages.Selected && pages.Pages[index].Enabled && pages.Visible))).ToArray();
    private static MeasureConstraint Inner(MeasureConstraint offer, float padding) => offer.Mode == MeasureMode.Unspecified
        ? offer : offer with { Size = Math.Max(0, offer.Size - padding * 2) };
    private static MeasureConstraint Intrinsic(MeasureConstraint offer) => offer.Mode == MeasureMode.Unspecified
        ? offer : MeasureConstraint.AtMost(offer.Size, offer.IsUnbounded);
    private GridPlacement[] Cells(Peer[] children) => children.Select(child =>
        Read(child).Cell ?? throw new InvalidOperationException("A grid child requires a placement.")).ToArray();

    private void PrimeVerticalLeaves(Peer[] children, MeasureConstraint width)
    {
        var pending = new List<(Peer Peer, MeasureMode Mode, float Width)>();
        var seen = new HashSet<(Peer Peer, MeasureMode Mode, float Width)>();
        void Collect(Peer peer, MeasureConstraint offered)
        {
            var state = Read(peer);
            var element = state.Element;
            var constrained = LayoutMath.ConstrainMeasure(offered, state.Width, state.Fixed?.Width, state.Preferred?.Width);
            if (element is PageView or Reveal or Xui.Experimental.Portable.Image) return;
            if (element is Stack { Axis: Axis.Vertical } stack)
            {
                foreach (var child in Visible(peer)) Collect(child, Inner(constrained, state.Padding));
            }
            else if (element is not (Stack or Grid or ScrollView))
            {
                var key = (peer, constrained.Mode, constrained.Size);
                if (!cache.TryNative(peer, constrained.Mode, constrained.Size, out _) && seen.Add(key)) pending.Add(key);
            }
        }
        foreach (var child in children) Collect(child, width);
        if (pending.Count == 0) return;
        var results = surface.Invoke<Size[]>("measureNativeBatch", pending.Select(item => item.Peer.Id).ToArray(),
            pending.Select(item => item.Mode.ToString()).ToArray(), pending.Select(item => item.Width).ToArray());
        if (results.Length != pending.Count) throw new InvalidOperationException("Native batch measurement returned the wrong number of results.");
        for (int i = 0; i < results.Length; i++) cache.Native(pending[i].Peer, pending[i].Mode, pending[i].Width, results[i]);
    }

    private Size Measure(Peer peer, MeasureConstraint width, MeasureConstraint height)
    {
        if (cache.TryMeasure(peer, width, height, out var retained))
        {
            unbounded[peer] = (retained.WidthUnbounded, retained.HeightUnbounded);
            return retained.Size;
        }
        var state = Read(peer);
        var element = state.Element;
        var w = LayoutMath.ConstrainMeasure(width, state.Width, state.Fixed?.Width, state.Preferred?.Width);
        var h = LayoutMath.ConstrainMeasure(height, state.Height, state.Fixed?.Height, state.Preferred?.Height);
        unbounded[peer] = (w.IsUnbounded, h.IsUnbounded);
        if (element is Reveal reveal)
        {
            var child = peer.NativeChildren.Single();
            var full = Measure(child, reveal.Motion.Direction == RevealDirection.Bottom ? w : MeasureConstraint.Unspecified,
                reveal.Motion.Direction == RevealDirection.Bottom ? MeasureConstraint.Unspecified : h);
            return RevealLayoutMath.Measure(full, w, h, reveal.Motion.Direction, peer.Presentation);
        }
        Size natural;
        if (element is PageView)
        {
            var children = Visible(peer);
            natural = children.Length == 0 ? new(0, 0) : Measure(children[0], w, h);
        }
        else if (element is Stack stack)
        {
            var children = Visible(peer);
            var innerW = Inner(w, state.Padding);
            var innerH = Inner(h, state.Padding);
            bool vertical = stack.Axis == Axis.Vertical;
            if (vertical) PrimeVerticalLeaves(children, innerW);
            var measured = children.Select(child => Measure(child,
                vertical ? innerW : Intrinsic(innerW), vertical ? Intrinsic(innerH) : innerH)).ToArray();
            var slots = LayoutMath.AllocateStack(vertical ? innerH : innerW, state.Spacing,
                measured.Select(size => vertical ? size.Height : size.Width).ToArray(),
                children.Select(child => Read(child).Flex).ToArray());
            float cross = 0;
            for (int i = 0; i < children.Length; i++)
            {
                var mainOffer = vertical ? innerH : innerW;
                var main = mainOffer.Mode == MeasureMode.Unspecified
                    ? MeasureConstraint.Unspecified : MeasureConstraint.Exactly(slots.Slots[i].Length, mainOffer.IsUnbounded);
                var size = Measure(children[i], vertical ? innerW : main, vertical ? main : innerH);
                cross = Math.Max(cross, vertical ? size.Width : size.Height);
            }
            natural = vertical ? new(cross + state.Padding * 2, slots.Extent + state.Padding * 2)
                : new(slots.Extent + state.Padding * 2, cross + state.Padding * 2);
        }
        else if (element is Grid grid)
        {
            var children = Visible(peer);
            natural = GridLayoutMath.Measure(grid.Rows, grid.Columns, Cells(children), w, h,
                (index, cw, ch) => Measure(children[index], cw, ch)).Size;
        }
        else if (element is ScrollView)
        {
            if (peer.HasVirtualViewport)
                return new(LayoutMath.MeasureAxis(w.Size, width, state.Width, state.Fixed?.Width, state.Preferred?.Width),
                    LayoutMath.MeasureAxis(h.Size, height, state.Height, state.Fixed?.Height, state.Preferred?.Height));
            var child = Visible(peer).SingleOrDefault();
            natural = child is null ? new(0, 0) : Measure(child, w, MeasureConstraint.Unspecified);
            if (child is not null && w.Mode != MeasureMode.Unspecified && h.Mode != MeasureMode.Unspecified && natural.Height > h.Size)
            {
                float gutter = surface.Invoke<float>("scrollbarWidth");
                var content = Measure(child, w with { Size = Math.Max(0, w.Size - gutter) }, MeasureConstraint.Unspecified);
                natural = new(content.Width + gutter, content.Height);
            }
        }
        else if (element is Xui.Experimental.Portable.Image) natural = new(192, 144);
        else
        {
            if (!cache.TryNative(peer, w.Mode, w.Size, out natural))
            {
                natural = surface.Invoke<Size>("measureNative", peer.Id, w.Mode.ToString(), w.Size);
                cache.Native(peer, w.Mode, w.Size, natural);
            }
        }
        var desired = new Size(
            LayoutMath.MeasureAxis(natural.Width, width, state.Width, state.Fixed?.Width, state.Preferred?.Width),
            LayoutMath.MeasureAxis(natural.Height, height, state.Height, state.Fixed?.Height, state.Preferred?.Height));
        cache.Measure(peer, width, height, new(desired, w.IsUnbounded, h.IsUnbounded));
        return desired;
    }

    private void Arrange(Peer peer, float x, float y, float allocatedWidth, float allocatedHeight)
    {
        var state = Read(peer);
        var element = state.Element;
        float width = LayoutMath.ArrangeAxis(allocatedWidth, state.Width, state.Fixed?.Width);
        float height = LayoutMath.ArrangeAxis(allocatedHeight, state.Height, state.Fixed?.Height);
        if (element is Reveal reveal)
        {
            var child = peer.NativeChildren.Single();
            var full = Measure(child, reveal.Motion.Direction == RevealDirection.Bottom ? MeasureConstraint.Exactly(width) : MeasureConstraint.Unspecified,
                reveal.Motion.Direction == RevealDirection.Bottom ? MeasureConstraint.Unspecified : MeasureConstraint.Exactly(height));
            var arranged = RevealLayoutMath.Arrange(full, new(width, height), reveal.Motion.Direction, peer.Presentation);
            frameIds.Add(peer.Id);
            frameBounds.Add(x); frameBounds.Add(y); frameBounds.Add(arranged.ClipSize.Width); frameBounds.Add(arranged.ClipSize.Height);
            frameContainers.Add(true);
            Arrange(child, 0, 0, arranged.ContentSize.Width, arranged.ContentSize.Height);
            return;
        }
        var context = unbounded[peer];
        frameIds.Add(peer.Id);
        frameBounds.Add(x);
        frameBounds.Add(y);
        frameBounds.Add(width);
        frameBounds.Add(height);
        frameContainers.Add(element is Stack or Grid or ScrollView);
        var children = Visible(peer);
        if (element is PageView)
        {
            foreach (var child in children)
            {
                Measure(child, MeasureConstraint.Exactly(width, context.Width), MeasureConstraint.Exactly(height, context.Height));
                Arrange(child, 0, 0, width, height);
            }
        }
        else if (element is Stack stack)
        {
            bool vertical = stack.Axis == Axis.Vertical;
            float innerW = Math.Max(0, width - state.Padding * 2);
            float innerH = Math.Max(0, height - state.Padding * 2);
            bool mainUnbounded = vertical ? context.Height : context.Width;
            var main = MeasureConstraint.AtMost(vertical ? innerH : innerW, mainUnbounded);
            var cross = MeasureConstraint.Exactly(vertical ? innerW : innerH, vertical ? context.Width : context.Height);
            if (vertical) PrimeVerticalLeaves(children, cross);
            var desired = children.Select(child => Measure(child, vertical ? cross : main, vertical ? main : cross)).ToArray();
            var slots = LayoutMath.AllocateStack(MeasureConstraint.Exactly(vertical ? innerH : innerW, mainUnbounded), state.Spacing,
                desired.Select(size => vertical ? size.Height : size.Width).ToArray(),
                children.Select(child => Read(child).Flex).ToArray());
            for (int i = 0; i < children.Length; i++)
            {
                var slot = slots.Slots[i];
                Arrange(children[i], Math.Min(state.Padding, width) + (vertical ? 0 : slot.Offset),
                    Math.Min(state.Padding, height) + (vertical ? slot.Offset : 0),
                    vertical ? innerW : slot.Length, vertical ? slot.Length : innerH);
            }
        }
        else if (element is Grid grid)
        {
            var result = GridLayoutMath.Arrange(grid.Rows, grid.Columns, Cells(children), new(width, height),
                context.Width, context.Height, (index, cw, ch) => Measure(children[index], cw, ch));
            for (int i = 0; i < children.Length; i++)
            {
                var cell = result.Cells[i];
                var cellContext = result.CellContexts[i];
                Measure(children[i], MeasureConstraint.Exactly(cell.Width, cellContext.WidthUnbounded),
                    MeasureConstraint.Exactly(cell.Height, cellContext.HeightUnbounded));
                Arrange(children[i], cell.X, cell.Y, cell.Width, cell.Height);
            }
        }
        else if (element is ScrollView && children.Length != 0)
        {
            if (peer.HasVirtualViewport)
            {
                var contentSize = surface.Invoke<Size>("virtualContentSize", peer.Id);
                Measure(children[0], MeasureConstraint.Exactly(contentSize.Width), MeasureConstraint.Unspecified);
                Arrange(children[0], 0, 0, contentSize.Width, contentSize.Height);
                return;
            }
            var content = Measure(children[0], MeasureConstraint.Exactly(width, context.Width), MeasureConstraint.Unspecified);
            float contentWidth = width;
            if (content.Height > height)
            {
                contentWidth = Math.Max(0, width - surface.Invoke<float>("scrollbarWidth"));
                content = Measure(children[0], MeasureConstraint.Exactly(contentWidth, context.Width), MeasureConstraint.Unspecified);
            }
            Arrange(children[0], 0, 0, contentWidth, Math.Max(height, content.Height));
        }
    }
}

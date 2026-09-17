namespace Xui.Designer;

internal readonly record struct PreviewNodeSnapshot(
    long Version, int NodeId, string ElementType, ElementBounds Bounds, ulong? ControlId);

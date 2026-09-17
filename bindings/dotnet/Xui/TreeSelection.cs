namespace Xui;

public sealed partial class TreeView
{
    public TreeView Select(ItemKey key)
    {
        Features.Action(this, 1, key.Id, key.Version);
        return this;
    }
}

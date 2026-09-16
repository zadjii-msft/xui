namespace Xui;

public sealed unsafe partial class NavigationView
{
    /// <summary>Sets the delay for all navigation sections. Preview reports row changes; Request reports delayed hover.</summary>
    public NavigationView SetHoverDelay(uint milliseconds)
    {
        Window.Guard();
        Window.Check(Native.NavigationHoverDelay(Handle, milliseconds));
        return this;
    }

    /// <summary>Updates hover text without moving focus. Returns false when the item is no longer hovered.</summary>
    public bool SetHoverHelp(ulong id, string text)
    {
        Window.Guard();
        using var pins = new Window.Pins();
        uint applied;
        Window.Check(Native.NavigationHoverHelp(Handle, id, pins.Text(text), &applied));
        return applied != 0;
    }
}

using Android.Util;
using Android.Widget;
using PortableMutation;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private void InputApiChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var board = new MutationBoard(host);
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        var driver = new NativeDriver(backend);
        driver.Click("add-row");
        var model = (TextInput)board.RowsView.Children[0].Children[0];
        var input = (EditText)backend.FindViews("row-1-input").Single();
        Assert(host.GetSelection(model) == new TextSelection(0, 0),
            "An empty native editor reports a collapsed initial selection.");
        int changes = 0;
        model.Changed += _ => changes++;
        input.Text = "A\U0001F642B";
        Assert(host.TryFocus(model) && host.HasFocus(model) && input.HasFocus,
            "Portable focus uses the actual Android editor.");
        host.SetSelection(model, new(2, 2));
        Assert(host.GetSelection(model) == new TextSelection(1, 1) &&
            input.SelectionStart == 1 && input.SelectionEnd == 1,
            "A caret inside a UTF-16 surrogate pair clamps before the complete scalar.");
        host.SetSelection(model, new(2, 99));
        Assert(host.GetSelection(model) == new TextSelection(1, 4) &&
            input.SelectionStart == 1 && input.SelectionEnd == 4,
            "Selection clamps both the Unicode boundary and the current native text length.");
        host.SetSelection(model, new(3, 2));
        Assert(host.GetSelection(model) == new TextSelection(1, 3),
            "Reversed input offsets normalize and include the whole surrogate pair.");
        host.SetSelection(model, new(0, 2));
        Assert(host.GetSelection(model) == new TextSelection(0, 3),
            "An end offset inside a surrogate pair expands to the scalar boundary.");
        input.SetSelection(4, 1);
        Assert(host.GetSelection(model) == new TextSelection(1, 4) &&
            input.SelectionStart == 4 && input.SelectionEnd == 1,
            "Reading selection orders native endpoints without mutating native direction.");
        Assert(changes == 1 && input.Text == "A\U0001F642B",
            "Focus and selection operations never emit text changes or rewrite the editor.");
        driver.Click("add-row");
        driver.Click("add-row");
        driver.Click("reverse-rows");
        Assert(ReferenceEquals(input, backend.FindViews("row-1-input").Single()) &&
            host.HasFocus(model) && host.GetSelection(model) == new TextSelection(1, 4),
            "Portable selection still targets the same keyed editor after insertion and reordering.");
        model.Enabled = false;
        Assert(!host.TryFocus(model), "Disabled controls reject portable focus requests.");
        model.Enabled = true;
        model.Visible = false;
        Assert(!host.TryFocus(model), "Hidden controls reject portable focus requests.");
        model.Visible = true;
        Assert(!host.TryFocus(board.CountLabel), "A native nonfocusable label reports focus failure.");
        bool invalid = false;
        try { host.SetSelection(model, new(-1, 0)); }
        catch (ArgumentOutOfRangeException) { invalid = true; }
        Assert(invalid && changes == 1, "Invalid selection is rejected without a native edit.");
        host.Detach();
        bool detached = false;
        try { host.GetSelection(model); }
        catch (InvalidOperationException) { detached = true; }
        Assert(detached && input.Handle == IntPtr.Zero, "Detached input operations cannot access a retired native widget.");
        var replacement = new AndroidBackend(surface, dispatcher);
        host.Attach(replacement);
        Assert(host.TryFocus(model) && host.HasFocus(model) &&
            !ReferenceEquals(input, replacement.FindViews("row-1-input").Single()),
            "Reattachment routes focus to the new native peer for the same live model.");
        host.SetSelection(model, new(2, 2));
        Assert(host.GetSelection(model) == new TextSelection(1, 1),
            "Unicode selection clamping remains valid after reattachment.");
        board.Rows = [];
        bool retired = false;
        try { host.GetSelection(model); }
        catch (ObjectDisposedException) { retired = true; }
        Assert(retired && replacement.FindViews("row-1-input").Count == 0,
            "Removed keyed models reject selection operations rather than target a replacement.");
        host.Dispose();
        Assert(surface.ChildCount == 0, "The native input API fixture releases its surface.");
        Log.Info("Xui.Android.Orders", $"Native focus/selection: {assertions - before} Android assertions.");
    }
}

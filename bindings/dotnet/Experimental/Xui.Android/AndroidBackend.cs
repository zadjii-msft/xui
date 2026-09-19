using Android.Content;
using Android.Text;
using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using Xui.Experimental.Portable;
using NativeButton = Android.Widget.Button;
using NativeScrollView = Android.Widget.ScrollView;
using PortableButton = Xui.Experimental.Portable.Button;
using PortableScrollView = Xui.Experimental.Portable.ScrollView;
using Stack = Xui.Experimental.Portable.Stack;

namespace Xui.Experimental.Android;

public sealed class AndroidBackend : IBackend
{
    private readonly FrameLayout surface;
    private readonly AndroidDispatcher dispatcher;
    private readonly List<AndroidPeer> peers = [];
    private AndroidPeer? root;
    private bool disposed;
    internal bool Mounted => root is not null && !disposed;

    public AndroidBackend(FrameLayout surface, AndroidDispatcher dispatcher)
    {
        ArgumentNullException.ThrowIfNull(surface);
        ArgumentNullException.ThrowIfNull(dispatcher);
        this.surface = surface;
        this.dispatcher = dispatcher;
        VerifyAccess();
        if (surface.ChildCount != 0) throw new ArgumentException("The Android surface must be empty.", nameof(surface));
    }

    internal void VerifyAccess()
    {
        if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Android views require the UI thread.");
    }

    public IElementPeer Create(Element element, IControlEvents events)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(element);
        ArgumentNullException.ThrowIfNull(events);
        if (Mounted) throw new InvalidOperationException("The Android tree is already mounted.");
        var peer = new AndroidPeer(this, surface.Context!, element, events);
        peers.Add(peer);
        return peer;
    }

    public void Mount(IElementPeer root)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (this.root is not null || surface.ChildCount != 0)
            throw new InvalidOperationException("The Android surface is not empty.");
        if (root is not AndroidPeer peer || peer.Backend != this)
            throw new ArgumentException("The root belongs to another backend.", nameof(root));
        // Keep the root before AddView so failure cleanup also removes a partially mounted view.
        this.root = peer;
        surface.AddView(peer.View, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.MatchParent));
    }

    public IReadOnlyList<View> FindViews(string automationId)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(automationId);
        return peers.Where(peer => peer.Element is Control control && control.AutomationId == automationId)
            .Select(peer => peer.NativeControl).ToArray();
    }

    public void Dispose()
    {
        VerifyAccess();
        if (disposed) return;
        disposed = true;
        var mounted = root;
        root = null;
        peers.Clear();
        if (mounted is not null) surface.RemoveView(mounted.View);
    }
}

internal sealed class AndroidPeer : IElementPeer
{
    private readonly IControlEvents events;
    private readonly List<View> owned = [];
    private readonly List<AndroidPeer> children = [];
    private bool disposed;
    private bool settingText;
    private NativeButton? button;
    private EditText? input;
    private TextView? caption;
    internal AndroidBackend Backend { get; }
    internal Element Element { get; }
    internal ElementFrame View { get; private set; } = null!;
    internal View NativeControl => input ?? View.Content;

    internal AndroidPeer(AndroidBackend backend, Context context, Element element, IControlEvents events)
    {
        Backend = backend;
        Element = element;
        this.events = events;
        try
        {
            View content = element switch
            {
                Stack stack => Own(new StackLayout(context, stack)),
                Label => Own(new TextView(context)),
                PortableButton => button = Own(new NativeButton(context)),
                TextInput => CreateInput(context),
                PortableScrollView => Own(new EnabledScrollView(context) { FillViewport = false }),
                _ => throw new NotSupportedException($"Unsupported portable element: {element.Kind}.")
            };
            content.SaveEnabled = false;
            View = Own(new ElementFrame(context, element, content));
            View.SaveEnabled = false;
            View.ImportantForAccessibility = ImportantForAccessibility.No;
            View.AddView(content, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.MatchParent));
            if (element is Control)
            {
                Update(ElementProperty.Name);
                Update(ElementProperty.AutomationId);
                Update(ElementProperty.Help);
                Update(ElementProperty.Visible);
                Update(ElementProperty.Enabled);
            }
            if (element is TextInput)
            {
                Update(ElementProperty.Text);
                Update(ElementProperty.Placeholder);
                Update(ElementProperty.CaptionVisible);
            }
            if (button is not null) button.Click += OnClick;
            if (input is not null)
            {
                input.TextChanged += OnTextChanged;
                input.EditorAction += OnEditorAction;
            }
        }
        catch (Exception creationError)
        {
            try { Dispose(); }
            catch (Exception cleanupError) { throw new AggregateException(creationError, cleanupError); }
            throw;
        }
    }

    private T Own<T>(T view) where T : View
    {
        owned.Add(view);
        return view;
    }

    private View CreateInput(Context context)
    {
        var wrapper = Own(new LinearLayout(context) { Orientation = Orientation.Vertical });
        wrapper.ImportantForAccessibility = ImportantForAccessibility.No;
        caption = Own(new TextView(context));
        input = Own(new EditText(context));
        input.Id = global::Android.Views.View.GenerateViewId();
        input.SaveEnabled = false;
        input.InputType = InputTypes.ClassText;
        input.SetSingleLine(true);
        input.ImeOptions = ImeAction.Done;
        caption.LabelFor = input.Id;
        wrapper.AddView(caption, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent));
        wrapper.AddView(input, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent));
        return wrapper;
    }

    public void AddChild(IElementPeer child)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (child is not AndroidPeer peer || peer.Backend != Backend || peer.Element.Parent != Element)
            throw new ArgumentException("The native child does not belong to this element.", nameof(child));
        if (View.Content is StackLayout stack) stack.Add(peer);
        else if (View.Content is NativeScrollView scroll && scroll.ChildCount == 0)
            scroll.AddView(peer.View, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent));
        else throw new InvalidOperationException("This Android element cannot accept the child.");
        children.Add(peer);
        peer.UpdateEnabled();
    }

    public void Update(ElementProperty property)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        var control = Element as Control;
        switch (property)
        {
            case ElementProperty.Name:
                if (Element is Label || Element is PortableButton) ((TextView)View.Content).Text = control!.Name;
                else if (Element is TextInput textInput)
                {
                    caption!.Text = textInput.Name;
                    input!.ContentDescription = textInput.CaptionVisible ? null : textInput.Name;
                }
                else View.Content.ContentDescription = control!.Name;
                break;
            case ElementProperty.AutomationId:
                NativeControl.Tag = new Java.Lang.String(control!.AutomationId);
                break;
            case ElementProperty.Help:
                NativeControl.TooltipText = control!.Help;
                break;
            case ElementProperty.Enabled:
                UpdateEnabled();
                break;
            case ElementProperty.Visible:
                View.Visibility = control!.Visible ? ViewStates.Visible : ViewStates.Gone;
                break;
            case ElementProperty.Text:
                SetText(((TextInput)Element).Text);
                break;
            case ElementProperty.Placeholder:
                input!.Hint = ((TextInput)Element).Placeholder;
                break;
            case ElementProperty.CaptionVisible:
                var model = (TextInput)Element;
                caption!.Visibility = model.CaptionVisible ? ViewStates.Visible : ViewStates.Gone;
                input!.ContentDescription = model.CaptionVisible ? null : model.Name;
                break;
            case ElementProperty.Spacing:
            case ElementProperty.Padding:
            case ElementProperty.FixedSize:
            case ElementProperty.PreferredSize:
                View.RequestLayout();
                View.Content.RequestLayout();
                break;
            default:
                throw new NotSupportedException($"Unsupported Android update: {property}.");
        }
    }

    private void SetText(string value)
    {
        if (input!.Text == value) return;
        int start = Math.Clamp(input.SelectionStart, 0, value.Length);
        int end = Math.Clamp(input.SelectionEnd, 0, value.Length);
        settingText = true;
        try
        {
            input.Text = value;
            input.SetSelection(Math.Min(start, end), Math.Max(start, end));
        }
        finally { settingText = false; }
    }

    private void UpdateEnabled()
    {
        bool enabled = true;
        for (Element? element = Element; element is not null; element = element.Parent)
            if (element is Control control && !control.Enabled) enabled = false;
        View.Enabled = enabled;
        View.Content.Enabled = enabled;
        if (input is not null)
        {
            input.Enabled = enabled;
            caption!.Enabled = enabled;
        }
        foreach (var child in children) child.UpdateEnabled();
    }

    private bool Deliver(Func<bool> callback)
    {
        if (disposed || !Backend.Mounted || settingText) return false;
        try { return callback(); }
        catch (Exception error)
        {
            Log.Error("Xui.Android", error.ToString());
            throw;
        }
    }

    private void OnClick(object? sender, EventArgs args) => Deliver(events.Click);
    private void OnTextChanged(object? sender, TextChangedEventArgs args) =>
        Deliver(() => events.Change(input!.Text ?? ""));

    private void OnEditorAction(object? sender, TextView.EditorActionEventArgs args)
    {
        bool ime = args.ActionId == ImeAction.Done;
        bool enter = args.ActionId == ImeAction.ImeNull && args.Event is { KeyCode: Keycode.Enter };
        if (ime || enter)
        {
            // Consume both hardware transitions, but submit only on key-up.
            args.Handled = !disposed && Backend.Mounted;
            if (ime || args.Event!.Action == KeyEventActions.Up) Deliver(events.Submit);
        }
    }

    public void Dispose()
    {
        Backend.VerifyAccess();
        if (disposed) return;
        disposed = true;
        var failures = new List<Exception>();
        void Cleanup(Action action)
        {
            try { action(); }
            catch (Exception error) { failures.Add(error); }
        }
        if (button is not null) Cleanup(() => button.Click -= OnClick);
        if (input is not null)
        {
            Cleanup(() => input.TextChanged -= OnTextChanged);
            Cleanup(() => input.EditorAction -= OnEditorAction);
        }
        for (int i = owned.Count - 1; i >= 0; i--)
        {
            var view = owned[i];
            Cleanup(() =>
            {
                if (view.Parent is ViewGroup parent) parent.RemoveView(view);
            });
            if (view is ViewGroup group) Cleanup(group.RemoveAllViews);
            Cleanup(view.Dispose);
        }
        owned.Clear();
        children.Clear();
        if (failures.Count != 0) throw new AggregateException("Android peer cleanup failed.", failures);
    }
}

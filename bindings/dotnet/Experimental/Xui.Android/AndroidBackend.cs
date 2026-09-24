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
using PortableCheckBox = Xui.Experimental.Portable.CheckBox;
using PortableProgress = Xui.Experimental.Portable.Progress;
using PortableGrid = Xui.Experimental.Portable.Grid;
using PortableScrollView = Xui.Experimental.Portable.ScrollView;
using Stack = Xui.Experimental.Portable.Stack;

namespace Xui.Experimental.Android;

public sealed partial class AndroidBackend : IThemeBackend
{
    private readonly FrameLayout surface;
    private readonly AndroidDispatcher dispatcher;
    private readonly List<AndroidPeer> peers = [];
    private AndroidPeer? root;
    private bool disposed;
    internal bool Mounted => root is not null && !disposed;
    internal AndroidDispatcher Dispatcher => dispatcher;
    internal NativePeerMetrics? Metrics { get; set; }
    internal int DeliveredInteractionCount { get; private set; }
    internal void RecordInteraction() => DeliveredInteractionCount++;
    internal bool HasComposition => peers.Any(peer => peer.Interaction?.IsComposing == true);
    internal EditText? FocusedEditor => peers.Select(peer => peer.Editor).FirstOrDefault(editor => editor?.HasFocus == true);
    internal event Action? InteractionChanged;

    internal void NotifyInteractionChanged() => InteractionChanged?.Invoke();
    internal void FlushNativeLayout()
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (!surface.IsLayoutRequested) return;
        int width = surface.Width;
        int height = surface.Height;
        surface.Measure(global::Android.Views.View.MeasureSpec.MakeMeasureSpec(width, MeasureSpecMode.Exactly),
            global::Android.Views.View.MeasureSpec.MakeMeasureSpec(height, MeasureSpecMode.Exactly));
        surface.Layout(surface.Left, surface.Top, surface.Left + width, surface.Top + height);
    }
    internal IReadOnlyList<ElementFrame> VirtualRows(EnabledScrollView scroll) => peers
        .Where(peer => peer.View.VirtualItem is not null && NearestScroll(peer.View) == scroll)
        .Select(peer => peer.View).ToArray();

    internal static EnabledScrollView? NearestScroll(View view)
    {
        for (var parent = view.Parent as View; parent is not null; parent = parent.Parent as View)
            if (parent is EnabledScrollView scroll) return scroll;
        return null;
    }

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
        using var trace = new NativePeerTrace(Metrics, NativePeerOperation.Create);
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(element);
        ArgumentNullException.ThrowIfNull(events);
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
        InteractionChanged = null;
        var failures = new List<Exception>();
        try { if (mounted is not null) surface.RemoveView(mounted.View); }
        catch (Exception error) { failures.Add(error); }
        try { ReleaseTheme(); }
        catch (Exception error) { failures.Add(error); }
        if (failures.Count != 0) throw new AggregateException("Android backend cleanup failed.", failures);
    }

    internal void Release(AndroidPeer peer)
    {
        if (peers.Remove(peer) && peer.Element is TextInput) NotifyInteractionChanged();
    }
}

internal sealed partial class AndroidPeer : IMutableElementPeer, ITextSelectionPeer, IConstrainedElementPeer,
    IVirtualViewportPeer, IVirtualItemPeer, IInputPurposeElementPeer, IPasswordElementPeer, IPresentationPeer, ITextLayoutPeer,
    IPageViewElementPeer, IPageSelectorElementPeer, IMutationPreflightPeer, ISingleChoiceElementPeer
{
    private readonly IControlEvents events;
    private readonly List<View> owned = [];
    private readonly List<AndroidPeer> children = [];
    private bool disposed;
    private bool settingText;
    private NativeButton? button;
    private EditText? input;
    private TextView? caption;
    private NativeChoice? choice;
    private NativeProgress? progress;
    private NativeTextObserver? textObserver;
    private NativePasswordEditor? password;
    private NativeLabelLayout? labelLayout;
    private NativeTabStrip? tabs;
    private NativeNavigationList? navigation;
    private NativeSingleChoice? singleChoice;
    private bool constructed;
    private bool nativeEnabled = true;
    private NativeVirtualViewport? itemLease;
    private bool enterPressed;
    private bool suppressEnter;
    internal AndroidBackend Backend { get; }
    internal Element Element { get; }
    internal ElementFrame View { get; private set; } = null!;
    internal View NativeControl => (View?)tabs ?? navigation ?? (View?)password ?? input ?? View.Content;
    internal EditText? Editor => disposed ? null : (EditText?)password ?? input;
    internal TextInteraction? Interaction => Editor is { } editor ? NativeTextObserver.Read(editor) : null;

    private NativePasswordEditor PasswordEditor()
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        return password ?? throw new NotSupportedException("Only a native password peer exposes secret operations.");
    }

    public int PasswordLength => PasswordEditor().PasswordLength;
    public void SetPassword(ReadOnlySpan<char> value) => PasswordEditor().SetPassword(value);
    public void WithPassword(PasswordReceiver receiver) => PasswordEditor().WithPassword(receiver);
    public void ClearPassword() => PasswordEditor().SetPassword([]);
    public void ValidateChoices(IReadOnlyList<Choice> items, ulong? selected)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (singleChoice is null) throw new NotSupportedException("This native peer is not a single-choice selector.");
    }
    public void ValidateTextLayout(LabelTextLayout? layout)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (Element is not Label label) throw new NotSupportedException("Only native labels support text-layout policy.");
        layout?.ValidateText(label.Text);
    }

    public bool TryFocus()
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        var control = NativeControl;
        return control.RequestFocus() || (control.Focusable && control.RequestFocusFromTouch());
    }

    public bool HasFocus
    {
        get
        {
            Backend.VerifyAccess();
            ObjectDisposedException.ThrowIf(disposed, this);
            return NativeControl.HasFocus;
        }
    }

    public TextSelection Selection
    {
        get
        {
            var editor = SelectionEditor();
            int start = editor.SelectionStart;
            int end = editor.SelectionEnd;
            // Android can report -1 before an editor has selection spans.
            if (start == -1 && end == -1) return new(0, 0);
            if (start < 0 || end < 0) throw new InvalidOperationException("Android returned an incomplete text selection.");
            return new(Math.Min(start, end), Math.Max(start, end));
        }
        set
        {
            var editor = SelectionEditor();
            var selection = value.ClampTo(editor.Text ?? "");
            editor.SetSelection(selection.Start, selection.End);
        }
    }

    private EditText SelectionEditor()
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        return input ?? throw new NotSupportedException("Only Android text inputs expose text selection.");
    }

    public IVirtualViewportLease BeginVirtualViewport(int itemCount, float rowHeight, long sourceVersion,
        Action<VirtualViewportRequest> requested)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (View.Content is not EnabledScrollView scroll)
            throw new NotSupportedException("Only a native scroll view can own a virtual viewport.");
        return scroll.BeginVirtualViewport(Backend, itemCount, rowHeight, sourceVersion, requested);
    }

    public void SetVirtualItemInfo(VirtualItemInfo info)
    {
        using var trace = new NativePeerTrace(Backend.Metrics, NativePeerOperation.Metadata);
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (View.VirtualItem == info && itemLease is { IsActive: true } current)
        {
            current.ValidateItem(info);
            return;
        }
        var lease = AndroidBackend.NearestScroll(View)?.VirtualLease
            ?? throw new InvalidOperationException("A native virtual item must belong to an active viewport lease.");
        lease.ValidateItem(info);
        itemLease = lease;
        View.VirtualItem = info;
        View.ImportantForAccessibility = ImportantForAccessibility.Yes;
    }

    internal AndroidPeer(AndroidBackend backend, Context context, Element element, IControlEvents events)
    {
        Backend = backend;
        Element = element;
        this.events = events;
        try
        {
            View content = element switch
            {
                PageView pages => Own(new NativePageLayout(context, pages)),
                Stack stack => Own(new StackLayout(context, stack)),
                PortableGrid grid => Own(new NativeGridLayout(context, grid)),
                Label => Own(new TextView(context)),
                PortableButton => button = Own(new NativeButton(context)),
                Toggle => choice = Own(new NativeChoice(context)),
                PortableCheckBox => choice = Own(new NativeChoice(context)),
                PortableProgress => progress = Own(new NativeProgress(context)),
                TextInput => CreateInput(context),
                MultilineText multiline => input = Own(new NativeMultilineEditor(context, multiline.MaximumLength)),
                PasswordInput secret => password = Own(new NativePasswordEditor(context, secret.MaximumLength)),
                TabStrip => CreateTabs(context),
                NavigationView => navigation = Own(new NativeNavigationList(context, SelectPage, ActivatePage)),
                SingleChoice => singleChoice = Own(new NativeSingleChoice(context, id => Deliver(() =>
                    (events as ISelectionControlEvents ?? throw new InvalidOperationException("Missing native choice events.")).SelectionChanged(id)))),
                PortableScrollView => Own(new EnabledScrollView(context) { FillViewport = false }),
                _ => throw new NotSupportedException($"Unsupported portable element: {element.Kind}.")
            };
            content.SaveEnabled = false;
            View = Own(new ElementFrame(context, element, content));
            View.SaveEnabled = false;
            View.ImportantForAccessibility = ImportantForAccessibility.No;
            View.AddView(content, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.MatchParent));
            if (element is PageView) Update(ElementProperty.Visible);
            using (new NativePeerTrace(Backend.Metrics, NativePeerOperation.InitialProperties))
            {
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
                if (element is MultilineText)
                {
                    Update(ElementProperty.Text);
                    Update(ElementProperty.ReadOnly);
                }
                if (element is Toggle) Update(ElementProperty.Checked);
                if (element is PortableCheckBox)
                {
                    Update(ElementProperty.ThreeState);
                    Update(ElementProperty.CheckState);
                }
                if (element is PortableProgress) Update(ElementProperty.Range);
                if (element is SingleChoice) Update(ElementProperty.Choices);
            }
            using var listenerTrace = new NativePeerTrace(Backend.Metrics, NativePeerOperation.Listeners);
            if (choice is not null)
            {
                if (events is not IValueControlEvents values)
                    throw new ArgumentException("Android value controls require value event delivery.", nameof(events));
                choice.Changed = state => Deliver(() => element is Toggle
                    ? values.ToggleChanged(state == CheckState.Checked) : values.CheckChanged(state));
            }
            if (button is not null) button.Click += OnClick;
            if (input is not null)
            {
                input.TextChanged += OnTextChanged;
                if (element is TextInput) input.EditorAction += OnEditorAction;
                if (element is TextInput && events is ITextInteractionEvents interactionEvents)
                    textObserver = new NativeTextObserver(input, Backend.Dispatcher, interaction =>
                    {
                        bool accepted = Deliver(() => interactionEvents.InteractionChanged(interaction));
                        if (accepted) Backend.RecordInteraction();
                        Backend.NotifyInteractionChanged();
                        return accepted;
                    });
                else textObserver = new NativeTextObserver(input, Backend.Dispatcher, _ =>
                {
                    Backend.NotifyInteractionChanged();
                    return true;
                });
            }
            if (password is not null)
            {
                if (events is not IPasswordControlEvents secrets)
                    throw new ArgumentException("A native password peer requires opaque password notifications.", nameof(events));
                password.Changed = () => Deliver(secrets.PasswordChanged);
                textObserver = new NativeTextObserver(password, Backend.Dispatcher, _ =>
                {
                    Backend.NotifyInteractionChanged();
                    return true;
                });
            }
            InitializePresentation();
            if (element is Label label)
            {
                labelLayout = new NativeLabelLayout((TextView)content);
                if (label.TextLayout is not null) labelLayout.Apply(label.TextLayout);
            }
            constructed = true;
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

    private View CreateTabs(Context context)
    {
        var scroll = Own(new HorizontalScrollView(context) { FillViewport = false });
        tabs = Own(new NativeTabStrip(context, SelectPage, ActivatePage, ClosePage));
        scroll.AddView(tabs, new HorizontalScrollView.LayoutParams(ViewGroup.LayoutParams.WrapContent, ViewGroup.LayoutParams.WrapContent));
        return scroll;
    }

    private View CreateInput(Context context)
    {
        using var trace = new NativePeerTrace(Backend.Metrics, NativePeerOperation.InputConstruction);
        var wrapper = Own(new LinearLayout(context) { Orientation = Orientation.Vertical });
        wrapper.ImportantForAccessibility = ImportantForAccessibility.No;
        input = Own(new EditText(context));
        input.Id = global::Android.Views.View.GenerateViewId();
        input.SaveEnabled = false;
        input.InputType = InputTypes.ClassText;
        input.SetSingleLine(true);
        input.ImeOptions = ImeAction.Done | (ImeAction)ImeFlags.NoFullscreen;
        NativeInputPurpose.Apply(input, ((TextInput)Element).Purpose);
        if (((TextInput)Element).CaptionVisible) AddCaption(wrapper);
        wrapper.AddView(input, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent));
        return wrapper;
    }

    private void AddCaption(LinearLayout wrapper)
    {
        if (caption is not null) return;
        caption = Own(new TextView(wrapper.Context!));
        caption.Text = ((TextInput)Element).Name;
        caption.LabelFor = input!.Id;
        caption.Enabled = input.Enabled;
        wrapper.AddView(caption, 0, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent));
        if (constructed) RegisterPresentation(caption);
    }

    public void AddChild(IElementPeer child)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (child is not AndroidPeer peer || peer.Backend != Backend || peer.Element.Parent != Element)
            throw new ArgumentException("The native child does not belong to this element.", nameof(child));
        if (View.Content is IMutableNativeLayout stack) stack.Add(peer);
        else if (View.Content is NativeGridLayout grid) grid.Add(peer);
        else if (View.Content is NativeScrollView scroll && scroll.ChildCount == 0)
            scroll.AddView(peer.View, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent));
        else throw new InvalidOperationException("This Android element cannot accept the child.");
        children.Add(peer);
        peer.UpdateEnabled();
    }

    private IMutableNativeLayout MutableStack()
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        return View.Content as IMutableNativeLayout
            ?? throw new InvalidOperationException("Only an Android stack or retained page host supports mutable children.");
    }

    private AndroidPeer DirectChild(IElementPeer child)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (child is not AndroidPeer peer || peer.Backend != Backend || !children.Contains(peer))
            throw new ArgumentException("The native child does not belong to this parent.", nameof(child));
        ObjectDisposedException.ThrowIf(peer.disposed, peer);
        return peer;
    }

    public void InsertChild(int index, IElementPeer child)
    {
        using var trace = new NativePeerTrace(Backend.Metrics, NativePeerOperation.Insert);
        var stack = MutableStack();
        ArgumentNullException.ThrowIfNull(child);
        if (index < 0 || index > children.Count) throw new ArgumentOutOfRangeException(nameof(index));
        if (child is not AndroidPeer peer || peer.Backend != Backend || peer.Element.Parent != Element ||
            children.Contains(peer) || peer.View.Parent is not null)
            throw new ArgumentException("The inserted native child must be an unmounted child of this element.", nameof(child));
        ObjectDisposedException.ThrowIf(peer.disposed, peer);
        stack.Insert(index, peer);
        children.Insert(index, peer);
        peer.UpdateEnabled();
    }

    public void RemoveChild(IElementPeer child)
    {
        using var trace = new NativePeerTrace(Backend.Metrics, NativePeerOperation.Remove);
        var stack = MutableStack();
        var peer = DirectChild(child);
        stack.Remove(peer);
        children.Remove(peer);
    }

    public void ValidateMove(IElementPeer child, int index)
    {
        _ = MutableStack();
        _ = DirectChild(child);
        if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
    }

    public void MoveChild(IElementPeer child, int index)
    {
        using var trace = new NativePeerTrace(Backend.Metrics, NativePeerOperation.Move);
        var stack = MutableStack();
        var peer = DirectChild(child);
        if (index < 0 || index >= children.Count) throw new ArgumentOutOfRangeException(nameof(index));
        int previous = children.IndexOf(peer);
        if (previous == index) return;
        stack.Move(peer, index);
        children.RemoveAt(previous);
        children.Insert(index, peer);
    }

    public void Update(ElementProperty property)
    {
        using var trace = new NativePeerTrace(constructed ? Backend.Metrics : null, NativePeerOperation.Update);
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        var control = Element as Control;
        switch (property)
        {
            case ElementProperty.Name:
                if (Element is Label or PortableButton or Toggle or PortableCheckBox)
                {
                    ((TextView)View.Content).Text = control!.Name;
                    choice?.RefreshDescription();
                }
                else if (Element is TextInput textInput)
                {
                    if (caption is not null) caption.Text = textInput.Name;
                    input!.ContentDescription = textInput.CaptionVisible ? null : textInput.Name;
                }
                else NativeControl.ContentDescription = control!.Name;
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
                View.Visibility = OwnVisible && DirectPageIsPresented() ? ViewStates.Visible : ViewStates.Gone;
                if (View.Content is NativePageLayout pageVisibility) pageVisibility.ApplyPages();
                break;
            case ElementProperty.Pages:
            case ElementProperty.Closable:
            case ElementProperty.Expanded:
                ApplyPages();
                break;
            case ElementProperty.Choices:
                var selection = (SingleChoice)Element;
                singleChoice!.Apply(selection.Items, selection.Selected);
                break;
            case ElementProperty.Text:
                SetText(Element is TextInput single ? single.Text : ((MultilineText)Element).Text);
                break;
            case ElementProperty.ReadOnly:
                settingText = true;
                try { ((NativeMultilineEditor)input!).SetReadOnly(((MultilineText)Element).ReadOnly); }
                finally { settingText = false; }
                break;
            case ElementProperty.Typography:
                ApplyTypography();
                break;
            case ElementProperty.TextLayout:
                labelLayout!.Apply(((Label)Element).TextLayout);
                View.RequestLayout();
                break;
            case ElementProperty.Placeholder:
                input!.Hint = ((TextInput)Element).Placeholder;
                break;
            case ElementProperty.CaptionVisible:
                var model = (TextInput)Element;
                if (model.CaptionVisible) AddCaption((LinearLayout)View.Content);
                if (caption is not null) caption.Visibility = model.CaptionVisible ? ViewStates.Visible : ViewStates.Gone;
                input!.ContentDescription = model.CaptionVisible ? null : model.Name;
                break;
            case ElementProperty.Checked:
                choice!.SetState(((Toggle)Element).Checked ? CheckState.Checked : CheckState.Unchecked);
                break;
            case ElementProperty.CheckState:
                choice!.SetState(((PortableCheckBox)Element).State);
                break;
            case ElementProperty.ThreeState:
                choice!.ThreeState = ((PortableCheckBox)Element).ThreeState;
                break;
            case ElementProperty.Range:
            case ElementProperty.Value:
            case ElementProperty.ProgressState:
                var indicator = (PortableProgress)Element;
                progress!.SetSnapshot(indicator.Range, indicator.Value, indicator.State);
                break;
            case ElementProperty.Spacing:
            case ElementProperty.Padding:
            case ElementProperty.FixedSize:
            case ElementProperty.PreferredSize:
            case ElementProperty.Constraints:
            case ElementProperty.Tracks:
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
        var multiline = input as NativeMultilineEditor;
        bool previous = multiline?.ProgrammaticEdit ?? false;
        if (multiline is not null) multiline.ProgrammaticEdit = true;
        try
        {
            input.Text = value;
            input.SetSelection(Math.Min(start, end), Math.Max(start, end));
        }
        finally
        {
            if (multiline is not null) multiline.ProgrammaticEdit = previous;
            settingText = false;
        }
    }

    internal void UpdateEnabled()
    {
        bool enabled = true;
        for (Element? element = Element; element is not null; element = element.Parent)
        {
            if (element is Control control && !control.Enabled) enabled = false;
            if (element.Parent is PageView pages && !PageIsPresented(pages, element)) enabled = false;
        }
        if (nativeEnabled != enabled)
        {
            nativeEnabled = enabled;
            View.Enabled = enabled;
            View.Content.Enabled = enabled;
            if (input is not null)
            {
                input.Enabled = enabled;
                if (caption is not null) caption.Enabled = enabled;
            }
            if (tabs is not null) tabs.Enabled = enabled;
        }
        if (tabs is not null || navigation is not null) ApplyPages();
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
        bool ime = args.Event is null && args.ActionId == ImeAction.Done;
        bool enter = args.Event is { KeyCode: Keycode.Enter };
        if (ime || enter)
        {
            // Android 14+ can label hardware Enter as Done too; the key event takes precedence.
            args.Handled = !disposed && Backend.Mounted;
            if (ime)
            {
                if (!IsComposing()) Deliver(events.Submit);
            }
            else if (args.Event!.Action == KeyEventActions.Down)
            {
                if (!enterPressed || args.Event.RepeatCount == 0) suppressEnter = false;
                enterPressed = true;
                suppressEnter |= args.Event.IsCanceled || IsComposing();
            }
            else if (args.Event.Action == KeyEventActions.Up)
            {
                bool submit = !args.Event.IsCanceled && !suppressEnter && !IsComposing();
                enterPressed = false;
                suppressEnter = false;
                if (submit) Deliver(events.Submit);
            }
        }
    }

    private bool IsComposing()
    {
        var editable = Editor?.EditableText;
        return editable is not null && BaseInputConnection.GetComposingSpanStart(editable) >= 0;
    }

    public void Dispose()
    {
        using var trace = new NativePeerTrace(Backend.Metrics, NativePeerOperation.Dispose);
        Backend.VerifyAccess();
        if (disposed) return;
        disposed = true;
        itemLease = null;
        Backend.Release(this);
        var failures = new List<Exception>();
        void Cleanup(Action action)
        {
            try { action(); }
            catch (Exception error) { failures.Add(error); }
        }
        if (button is not null) Cleanup(() => button.Click -= OnClick);
        if (choice is not null) choice.Changed = null;
        if (textObserver is not null) Cleanup(textObserver.Dispose);
        if (password is not null) password.Changed = null;
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
            if (view is ViewGroup group && view is not AdapterView) Cleanup(group.RemoveAllViews);
            Cleanup(view.Dispose);
        }
        owned.Clear();
        typography.Clear();
        themeStates.Clear();
        children.Clear();
        if (failures.Count != 0) throw new AggregateException("Android peer cleanup failed.", failures);
    }
}

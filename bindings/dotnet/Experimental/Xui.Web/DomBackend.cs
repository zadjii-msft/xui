using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using Stack = Xui.Experimental.Portable.Stack;

namespace Xui.Experimental.Web;

/// <summary>The caller owns the imported module. The host owns this backend after attachment starts.</summary>
public sealed class DomBackend : IThemeBackend, IHostViewportBackend
{
    private readonly IJSInProcessObjectReference surface;
    private readonly DotNetObjectReference<DomBackend> layoutReference;
    private DomPeer? root;
    private bool layingOut;
    private int virtualLayoutDepth;
    private bool virtualLayoutPending;
    private int nextId;
    private int peers;
    private bool disposed;
    private readonly DomLayoutCache layoutCache = new();
    private readonly List<DomHostViewport> viewportObservers = [];
    private readonly BrowserErrorReporter imageReporter;
    private int imageDecodes;
    internal IJSInProcessObjectReference Surface => surface;
    internal ImageResourceCache ImageCache { get; } = new();
    internal ImagePixelBudget OutputPixels { get; } = new();
    private readonly ImagePixelBudget sourcePixels = new(64L * 1024 * 1024);
#if DEBUG
    public DomDiagnostics Diagnostics { get; } = new();
    public object ImageStatistics => new { decodes = imageDecodes, sourceBytes = sourcePixels.ReservedBytes,
        outputBytes = OutputPixels.ReservedBytes, encodedBytes = ImageCache.Statistics.ResidentBytes };
#endif

    public DomBackend(IJSInProcessObjectReference module, string mountId, string errorId)
    {
        ArgumentNullException.ThrowIfNull(module);
        imageReporter = new BrowserErrorReporter(module, errorId);
        layoutReference = DotNetObjectReference.Create(this);
        try
        {
            var reference = module.Invoke<IJSInProcessObjectReference>("createSurface", mountId, errorId, layoutReference);
#if DEBUG
            surface = new DiagnosticJsReference(reference, Diagnostics);
#else
            surface = reference;
#endif
        }
        catch
        {
            layoutReference.Dispose();
            ImageCache.Dispose();
            imageReporter.Dispose();
            throw;
        }
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

    private static object? ThemeValue(ThemeSettings? theme) => theme is null ? null : new
    {
        mode = theme.Mode.ToString(), foreground = theme.Resources.Foreground,
        background = theme.Resources.Background, accent = theme.Resources.Accent
    };
    public void ValidateTheme(ThemeSettings? theme)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        surface.InvokeVoid("validateTheme", ThemeValue(theme));
    }
    public void ApplyTheme(ThemeSettings? theme)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        surface.InvokeVoid("applyTheme", ThemeValue(theme));
        layoutCache.Clear();
        Reflow();
    }
    public IDisposable ObserveViewport(Action<Size> changed)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        ArgumentNullException.ThrowIfNull(changed);
        var observer = new DomHostViewport(surface, changed, subscription => viewportObservers.Remove(subscription));
        viewportObservers.Add(observer);
        return observer;
    }
    internal void ReportImageFailure(Exception error) => imageReporter.Report(error);
    internal IDisposable AdmitImageDecode(ImageDecodePlan plan)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (imageDecodes >= 2) throw new ImageResourceLimitException("The native image decode concurrency limit is full.");
        var reservation = sourcePixels.ReserveSource(plan);
        imageDecodes++;
        return new ImageAdmission(this, reservation);
    }
    private sealed class ImageAdmission(DomBackend owner, ImagePixelReservation pixels) : IDisposable
    {
        private bool disposed;
        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            pixels.Dispose();
            owner.imageDecodes--;
            owner.ReleaseIfEmpty();
        }
    }

    public void Mount(IElementPeer root)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (root is not DomPeer peer || peer.Owner != this)
            throw new ArgumentException("The root belongs to another DOM backend.", nameof(root));
        surface.InvokeVoid("mount", peer.Id);
        this.root = peer;
        Reflow();
    }

    [JSInvokable]
    public void ReflowObserved()
    {
        layoutCache.Clear();
        Reflow();
    }

    public void Reflow()
    {
        if (virtualLayoutDepth != 0)
        {
            virtualLayoutPending = true;
            return;
        }
        ReflowCore();
    }

    private void ReflowCore()
    {
        if (disposed || root is null || layingOut) return;
        layingOut = true;
        try
        {
            bool Required(DomPeer peer) => peer.Element is Grid || peer.Element.WidthConstraints.HasValue ||
                peer.Element.HeightConstraints.HasValue || peer.HasVirtualViewport || peer.Element is PageView or PageSelector or Reveal or Label { TextLayout: not null } ||
                peer.NativeChildren.Any(Required);
            bool required = Required(root);
            surface.InvokeVoid("managedLayout", required);
            if (required)
            {
                for (int pass = 0; pass < 4; pass++)
                {
                    var viewport = surface.Invoke<Size>("viewport");
                    new DomLayout(surface, layoutCache).ArrangeRoot(root, viewport);
                    if (surface.Invoke<Size>("viewport") == viewport) return;
                }

                throw new InvalidOperationException("DOM layout did not stabilize within four viewport passes.");
            }
        }
        finally { layingOut = false; }
    }

    private void BeginVirtualLayout()
    {
#if DEBUG
        Diagnostics.Mark("callback-start");
#endif
        virtualLayoutDepth++;
    }
    private void EndVirtualLayout()
    {
        virtualLayoutDepth--;
        if (virtualLayoutDepth == 0 && virtualLayoutPending)
        {
            virtualLayoutPending = false;
            ReflowCore();
        }
#if DEBUG
        Diagnostics.Mark("callback-end");
#endif
    }

    private void FlushVirtualLayout()
    {
        ReflowCore();
        virtualLayoutPending = false;
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        root = null;
        layoutCache.Clear();
        Cleanup(() =>
        {
            var observers = viewportObservers.ToArray();
            viewportObservers.Clear();
            Cleanup(observers.Select(observer => (Action)observer.Dispose).ToArray());
        }, ImageCache.Dispose, imageReporter.Dispose, () => surface.InvokeVoid("unmount"), layoutReference.Dispose, ReleaseIfEmpty);
    }

    private void ReleaseIfEmpty()
    {
        if (disposed && peers == 0 && imageDecodes == 0) surface.Dispose();
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

    public sealed class DomPeer : IMutableElementPeer, ITextSelectionPeer, IConstrainedElementPeer, IVirtualViewportPeer, IVirtualItemPeer,
        IInputPurposeElementPeer, IPasswordElementPeer, IPresentationPeer,
        IPageViewElementPeer, IPageSelectorElementPeer, ITextLayoutPeer, ISingleChoiceElementPeer, IImageElementPeer, IRevealElementPeer
    {
        internal DomBackend Owner { get; }
        internal int Id { get; }
        internal Element Element => element;
        internal List<DomPeer> NativeChildren { get; } = [];
        internal DomPeer? NativeParent { get; private set; }
        private readonly Element element;
        private readonly IControlEvents events;
        private readonly DotNetObjectReference<DomPeer> reference;
        private bool disposed;
        private int textRevision;
        private DomVirtualViewport? viewportLease;
        private VirtualItemInfo? virtualItem;
        private DomImage? image;
        internal bool HasVirtualViewport => viewportLease is not null;

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
            var multiline = element as MultilineText;
            var password = element as PasswordInput;
            var progress = element as Progress;
            if (element is Toggle or CheckBox && events is not IValueControlEvents)
                throw new ArgumentException("Value controls require a value event sink.", nameof(events));
            Owner.surface.InvokeVoid("create", Id, new
            {
                kind = element.Kind.ToString(),
                axis = stack?.Axis.ToString(),
                flex = element.Flex,
                fixedSize = element.FixedSize,
                preferredSize = element.PreferredSize,
                spacing = stack?.SpacingValue ?? 0,
                padding = stack?.PaddingValue ?? 0,
                name = control?.Name ?? (element as PageView)?.Name ?? (element as Reveal)?.Name ?? "",
                automationId = control?.AutomationId ?? "",
                help = control?.Help ?? "",
                enabled = control?.Enabled ?? true,
                visible = control?.Visible ?? (element as PageView)?.Visible ?? true,
                text = input?.Text ?? multiline?.Text ?? "",
                placeholder = input?.Placeholder ?? "",
                captionVisible = input?.CaptionVisible ?? true,
                trackInteraction = input is not null && events is ITextInteractionEvents,
                isChecked = (element as Toggle)?.Checked ?? false,
                checkState = ((element as CheckBox)?.State ?? CheckState.Unchecked).ToString(),
                threeState = (element as CheckBox)?.ThreeState ?? false,
                range = progress?.Range ?? new NumericRange(0, 100),
                value = progress?.Value ?? 0,
                progressState = (progress?.State ?? ProgressState.Determinate).ToString(),
                purpose = (input?.Purpose ?? InputPurpose.Normal).ToString(),
                maximumLength = multiline?.MaximumLength ?? password?.MaximumLength ?? 65536,
                readOnly = multiline?.ReadOnly ?? false,
                typography = TypographyValue(control?.Typography),
                pages = PagesValue(element is PageView pages ? pages : (element as PageSelector)?.Pages),
                expanded = (element as NavigationView)?.Expanded ?? true,
                closable = (element as TabStrip)?.Closable ?? false,
                textLayout = TextLayoutValue((element as Label)?.TextLayout),
                choices = ChoicesValue((element as SingleChoice)?.Items ?? [], (element as SingleChoice)?.Selected),
                reveal = RevealValue(element as Reveal)
            }, reference);
            if (element is Xui.Experimental.Portable.Image picture)
            {
                image = new DomImage(Owner, Id, picture, (IImageControlEvents)events);
                image.Start();
            }
        }

        private static object? TypographyValue(Typography? typography) => typography is null ? null : new
        {
            size = typography.ResolvedFontSize, weight = typography.ResolvedFontWeight
        };
        private static object RevealValue(Reveal? value) => new
        {
            open = value?.Open ?? false, duration = value?.Motion.DurationMilliseconds ?? 0,
            direction = (value?.Motion.Direction ?? RevealDirection.Bottom).ToString()
        };
        public bool CanSetOpen(bool open) => Owner.surface.Invoke<bool>("canRevealOpen", Id, open);
        public void ValidateMotion(RevealMotion motion)
        {
            ArgumentNullException.ThrowIfNull(motion);
            Owner.surface.InvokeVoid("validateReveal", new { open = false, duration = motion.DurationMilliseconds, direction = motion.Direction.ToString() });
        }
        public RevealPresentation Presentation => Owner.surface.Invoke<RevealPresentation>("revealPresentation", Id);
        private static object PagesValue(PageView? pages) => new
        {
            items = pages?.Pages.Select(page => new { id = page.Id.ToString(System.Globalization.CultureInfo.InvariantCulture), title = page.Title, enabled = page.Enabled }).ToArray() ?? [],
            selected = pages?.Selected?.ToString(System.Globalization.CultureInfo.InvariantCulture)
        };
        private static object? TextLayoutValue(LabelTextLayout? value) => value is null ? null : new
        {
            wrapping = value.Wrapping, maximumLines = value.MaximumLines, overflow = value.Overflow.ToString()
        };
        private static object ChoicesValue(IReadOnlyList<Choice> items, ulong? selected) => new
        {
            items = items.Select(item => new { id = item.Id.ToString(System.Globalization.CultureInfo.InvariantCulture), text = item.Text, enabled = item.Enabled }).ToArray(),
            selected = selected?.ToString(System.Globalization.CultureInfo.InvariantCulture)
        };
        public void ValidateChoices(IReadOnlyList<Choice> items, ulong? selected)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            Owner.surface.InvokeVoid("validateChoices", ChoicesValue(items, selected));
        }
        public void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? selected)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            Owner.surface.InvokeVoid("validatePages", Id, new
            {
                items = pages.Select(page => new { id = page.Id.ToString(System.Globalization.CultureInfo.InvariantCulture), title = page.Title, enabled = page.Enabled }).ToArray(),
                selected = selected?.ToString(System.Globalization.CultureInfo.InvariantCulture)
            });
        }
        public void ValidateVisibility(bool visible)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            Owner.surface.InvokeVoid("validatePageVisibility", Id, visible);
        }
        public void ConnectPages(IPageViewElementPeer pages)
        {
            if (pages is not DomPeer peer || peer.Owner != Owner) throw new ArgumentException("The linked page host belongs to another backend.", nameof(pages));
            Owner.surface.InvokeVoid("connectPages", Id, peer.Id);
        }
        public void ValidateTextLayout(LabelTextLayout? value)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            Owner.surface.InvokeVoid("validateTextLayout", Id, TextLayoutValue(value));
        }
        public void ValidateImage(PackagedImageSource? source, ImageDecodeOptions options)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            ArgumentNullException.ThrowIfNull(options);
            if (source is not null) source.Manifest.Get(source.AssetId);
            Owner.surface.InvokeVoid("validateImage");
        }
        public void CancelImage() => image?.Cancel();
        public void ValidateTypography(Typography? typography)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            Owner.surface.InvokeVoid("validateTypography", element.Kind.ToString(), TypographyValue(typography));
        }
        public int PasswordLength
        {
            get
            {
                ObjectDisposedException.ThrowIf(disposed, this);
                return Owner.surface.Invoke<int>("passwordLength", Id);
            }
        }
        public void SetPassword(ReadOnlySpan<char> password)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (element is not PasswordInput secret) throw new NotSupportedException("This peer is not a password input.");
            FormValues.ValidatePassword(password, secret.MaximumLength);
            Owner.surface.InvokeVoid("setPassword", Id, new string(password), checked(++textRevision));
        }
        public void WithPassword(PasswordReceiver receiver)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            ArgumentNullException.ThrowIfNull(receiver);
            if (element is not PasswordInput secret) throw new NotSupportedException("This peer is not a password input.");
            string value = Owner.surface.Invoke<string>("readPassword", Id);
            FormValues.ValidatePassword(value, secret.MaximumLength);
            receiver(value);
        }
        public void ClearPassword()
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            Owner.surface.InvokeVoid("clearPassword", Id, checked(++textRevision));
        }

        public void AddChild(IElementPeer child)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (child is not DomPeer peer || peer.Owner != Owner)
                throw new ArgumentException("The child belongs to another DOM backend.", nameof(child));
            Owner.surface.InvokeVoid("addChild", Id, peer.Id);
            NativeChildren.Add(peer);
            peer.NativeParent = this;
            Owner.layoutCache.Invalidate(this);
        }

        private DomPeer OwnedChild(IElementPeer child)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (child is not DomPeer peer || peer.Owner != Owner || peer.disposed)
                throw new ArgumentException("The child must be a live peer of this DOM backend.", nameof(child));
            return peer;
        }

        public void InsertChild(int index, IElementPeer child)
        {
            var peer = OwnedChild(child);
            Owner.surface.InvokeVoid("insertChild", Id, index, peer.Id);
            NativeChildren.Insert(index, peer);
            peer.NativeParent = this;
            Owner.layoutCache.Invalidate(this);
            Owner.Reflow();
        }

        public void RemoveChild(IElementPeer child)
        {
            var peer = OwnedChild(child);
            Owner.surface.InvokeVoid("removeChild", Id, peer.Id);
            NativeChildren.Remove(peer);
            Owner.layoutCache.Invalidate(this);
            peer.NativeParent = null;
            Owner.Reflow();
        }

        public void ValidateMove(IElementPeer child, int index) =>
            Owner.surface.InvokeVoid("validateMove", Id, OwnedChild(child).Id, index);

        public void MoveChild(IElementPeer child, int index)
        {
            var peer = OwnedChild(child);
            Owner.surface.InvokeVoid("moveChild", Id, peer.Id, index);
            NativeChildren.Remove(peer);
            NativeChildren.Insert(index, peer);
            Owner.layoutCache.Invalidate(this);
            Owner.Reflow();
        }

        public bool TryFocus()
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            return Owner.surface.Invoke<bool>("tryFocus", Id);
        }

        public bool HasFocus
        {
            get
            {
                ObjectDisposedException.ThrowIf(disposed, this);
                return Owner.surface.Invoke<bool>("hasFocus", Id);
            }
        }

        public TextSelection Selection
        {
            get
            {
                ObjectDisposedException.ThrowIf(disposed, this);
                return Owner.surface.Invoke<TextSelection>("getSelection", Id);
            }
            set
            {
                ObjectDisposedException.ThrowIf(disposed, this);
                Owner.surface.InvokeVoid("setSelection", Id, value.Start, value.End);
            }
        }

        public IVirtualViewportLease BeginVirtualViewport(int itemCount, float rowHeight, long sourceVersion,
            Action<VirtualViewportRequest> requested)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (element is not ScrollView) throw new NotSupportedException("A DOM virtual viewport requires a ScrollView.");
            if (viewportLease is { IsDisposed: false }) throw new InvalidOperationException("The scroll view already owns a viewport lease.");
            var next = new DomVirtualViewport(Owner.surface, Id, itemCount, rowHeight, sourceVersion, requested,
                Owner.BeginVirtualLayout, Owner.EndVirtualLayout, Owner.ReflowCore, Owner.FlushVirtualLayout);
            viewportLease = next;
            try { Owner.Reflow(); }
            catch (Exception error)
            {
                try { next.Dispose(); }
                catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
                throw;
            }
            return next;
        }

        public void SetVirtualItemInfo(VirtualItemInfo info)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            info.Validate();
            if (virtualItem == info) return;
            void InvalidateTree(DomPeer peer)
            {
                Owner.layoutCache.Invalidate(peer);
                foreach (var child in peer.NativeChildren) InvalidateTree(child);
            }
            InvalidateTree(this);
            Owner.surface.InvokeVoid("setVirtualItemInfo", Id, new
            {
                key = info.Key, index = info.Index, count = info.Count,
                sourceVersion = info.SourceVersion.ToString(System.Globalization.CultureInfo.InvariantCulture)
            });
            virtualItem = info;
        }

        public void Update(ElementProperty property)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            Owner.layoutCache.Invalidate(this);
            if (property == ElementProperty.ImageRequest)
            {
                (image ?? throw new InvalidOperationException("Missing image ownership.")).Start();
                return;
            }
            if (element is Stack or Grid or ScrollView)
            {
                void InvalidateChildren(DomPeer parent)
                {
                    foreach (var child in parent.NativeChildren)
                    {
                        Owner.layoutCache.Invalidate(child);
                        InvalidateChildren(child);
                    }
                }
                InvalidateChildren(this);
            }
            if (property is ElementProperty.Constraints or ElementProperty.Tracks)
            {
                Owner.Reflow();
                return;
            }
            object? value = property switch
            {
                ElementProperty.Name => ((Control)element).Name,
                ElementProperty.AutomationId => ((Control)element).AutomationId,
                ElementProperty.Enabled => ((Control)element).Enabled,
                ElementProperty.Visible => element is PageView pages ? pages.Visible : ((Control)element).Visible,
                ElementProperty.Help => ((Control)element).Help,
                ElementProperty.Text => element is TextInput textInput ? textInput.Text : ((MultilineText)element).Text,
                ElementProperty.Placeholder => ((TextInput)element).Placeholder,
                ElementProperty.CaptionVisible => ((TextInput)element).CaptionVisible,
                ElementProperty.Spacing => ((Stack)element).SpacingValue,
                ElementProperty.Padding => ((Stack)element).PaddingValue,
                ElementProperty.FixedSize => element.FixedSize,
                ElementProperty.PreferredSize => element.PreferredSize,
                ElementProperty.Checked => ((Toggle)element).Checked,
                ElementProperty.CheckState => ((CheckBox)element).State.ToString(),
                ElementProperty.ThreeState => ((CheckBox)element).ThreeState,
                ElementProperty.Range => new { range = ((Progress)element).Range, value = ((Progress)element).Value },
                ElementProperty.Value => ((Progress)element).Value,
                ElementProperty.ProgressState => ((Progress)element).State.ToString(),
                ElementProperty.ReadOnly => ((MultilineText)element).ReadOnly,
                ElementProperty.Typography => TypographyValue(((Control)element).Typography),
                ElementProperty.Pages => PagesValue(element is PageView pageView ? pageView : ((PageSelector)element).Pages),
                ElementProperty.Expanded => ((NavigationView)element).Expanded,
                ElementProperty.Closable => ((TabStrip)element).Closable,
                ElementProperty.TextLayout => TextLayoutValue(((Label)element).TextLayout),
                ElementProperty.Choices => ChoicesValue(((SingleChoice)element).Items, ((SingleChoice)element).Selected),
                ElementProperty.RevealState => RevealValue((Reveal)element),
                _ => throw new ArgumentOutOfRangeException(nameof(property))
            };
            if (property is ElementProperty.Text or ElementProperty.Checked or ElementProperty.CheckState or ElementProperty.ThreeState or ElementProperty.Choices)
                checked { textRevision++; }
            Owner.surface.InvokeVoid("update", Id, property.ToString(), value, textRevision);
            Owner.Reflow();
        }

        [JSInvokable]
        public bool Interaction(bool hasFocus, bool isComposing)
        {
            if (disposed || Owner.disposed) return false;
            if (element is not TextInput || events is not ITextInteractionEvents sink)
                throw new InvalidOperationException("The control does not accept native text interaction snapshots.");
            return sink.InteractionChanged(new(hasFocus, isComposing));
        }

        [JSInvokable]
        public bool ImagePresentationFailed(string generation)
        {
            if (disposed) return false;
            if (events is not IImagePresentationEvents failure) throw new NotSupportedException("Missing terminal image failure capability.");
            return failure.ImagePresentationFailed(long.Parse(generation, System.Globalization.CultureInfo.InvariantCulture),
                new InvalidOperationException("The native image presentation failed after decoding."));
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
                "change" when element is MultilineText && text is not null =>
                    revision == textRevision && events.Change(text),
                "password" when element is PasswordInput && text is null =>
                    revision == textRevision && ((IPasswordControlEvents)events).PasswordChanged(),
                "submit" when element is TextInput && text is null => events.Submit(),
                "toggle" when element is Toggle && text is "true" or "false" =>
                    revision == textRevision && ((IValueControlEvents)events).ToggleChanged(text == "true"),
                "check" when element is CheckBox && text is "Unchecked" or "Checked" or "Indeterminate" =>
                    revision == textRevision && ((IValueControlEvents)events).CheckChanged(Enum.Parse<CheckState>(text)),
                "page-select" when element is PageSelector && text is not null => ((IPageControlEvents)events).PageSelected(ulong.Parse(text, System.Globalization.CultureInfo.InvariantCulture)),
                "page-activate" when element is PageSelector && text is not null => ((IPageControlEvents)events).PageActivated(ulong.Parse(text, System.Globalization.CultureInfo.InvariantCulture)),
                "page-close" when element is TabStrip && text is not null => ((IPageControlEvents)events).PageCloseRequested(ulong.Parse(text, System.Globalization.CultureInfo.InvariantCulture)),
                "choice" when element is SingleChoice && text is not null =>
                    revision == textRevision && ((ISelectionControlEvents)events).SelectionChanged(ulong.Parse(text, System.Globalization.CultureInfo.InvariantCulture)),
                _ => throw new ArgumentException("Invalid DOM event payload.", nameof(kind))
            };
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            Owner.layoutCache.Invalidate(this);
            Cleanup(() => image?.Dispose(), () => viewportLease?.Dispose(), () => Owner.surface.InvokeVoid("destroy", Id), reference.Dispose, NativeChildren.Clear, () =>
            {
                Owner.peers--;
                Owner.ReleaseIfEmpty();
            });
        }
    }
}

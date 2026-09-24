using Android.Content;
using Android.Text;
using Android.Views;
using Android.Widget;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal sealed class NativeTabStrip : TabWidget
{
    private readonly List<Header> headers = [];
    private readonly Func<ulong, bool> select;
    private readonly Func<ulong, bool> activate;
    private readonly Func<ulong, bool> close;
    private bool updating;
    private NativeThemePalette? palette;

    // The framework TabWidget is retained intentionally for real native tab semantics on API 26+.
#pragma warning disable CA1422
    internal NativeTabStrip(Context context, Func<ulong, bool> select, Func<ulong, bool> activate, Func<ulong, bool> close)
        : base(context)
    {
        this.select = select;
        this.activate = activate;
        this.close = close;
        Orientation = Orientation.Horizontal;
    }
#pragma warning restore CA1422

    internal IReadOnlyList<ulong> Ids => Enumerable.Range(0, ChildCount).Select(index =>
        (ulong)(((GetChildAt(index)?.Tag as Java.Lang.Long)?.LongValue())
            ?? throw new InvalidOperationException("A native tab lost its stable identity."))).ToArray();
    internal ulong? SelectedId
    {
        get
        {
            for (int i = 0; i < ChildCount; i++)
                if (GetChildAt(i) is { Selected: true } tab) return (ulong)((Java.Lang.Long)tab.Tag!).LongValue();
            return null;
        }
    }

    internal void Apply(IReadOnlyList<PageEntry> entries, ulong? selected, bool closable)
    {
        updating = true;
        try
        {
            foreach (var old in headers.Where(header => !entries.Any(entry => entry.Id == header.Entry.Id)).ToArray())
            {
                RemoveView(old.View);
                headers.Remove(old);
                old.Dispose();
            }
            for (int index = 0; index < entries.Count; index++)
            {
                var entry = entries[index];
                var header = headers.FirstOrDefault(item => item.Entry.Id == entry.Id);
                if (header is null)
                {
                    header = new Header(Context!, entry, this);
                    headers.Insert(index, header);
                    AddView(header.View, index, new LinearLayout.LayoutParams(LayoutParams.WrapContent, LayoutParams.WrapContent));
                    header.Connect();
                }
                else
                {
                    int previous = headers.IndexOf(header);
                    if (previous != index)
                    {
                        DetachViewFromParent(previous);
                        AttachViewToParent(header.View, index, header.View.LayoutParameters);
                        headers.RemoveAt(previous);
                        headers.Insert(index, header);
                    }
                }
                header.Entry = entry;
                header.Title.Text = entry.Title;
                header.View.ContentDescription = entry.Title;
                header.View.Enabled = entry.Enabled && Enabled;
                header.Close.Enabled = entry.Enabled && Enabled;
                header.Close.Visibility = closable ? ViewStates.Visible : ViewStates.Gone;
                header.Close.ContentDescription = "Close " + entry.Title;
                header.ApplyTheme(palette);
            }
            int active = selected is ulong id ? headers.FindIndex(header => header.Entry.Id == id) : -1;
#pragma warning disable CA1422
            if (active >= 0) SetCurrentTab(active);
#pragma warning restore CA1422
            foreach (var header in headers) header.View.Selected = header.Entry.Id == selected;
            RequestLayout();
        }
        finally { updating = false; }
    }

    public override void OnFocusChange(View? view, bool focused)
    {
        if (!focused || updating || !Enabled) return;
        var header = headers.FirstOrDefault(header => ReferenceEquals(header.View, view));
        if (header is not null && header.Entry.Enabled) select(header.Entry.Id);
    }

    public override bool DispatchKeyEvent(KeyEvent? key)
    {
        if (key is null || !Enabled) return base.DispatchKeyEvent(key);
        bool navigation = key.KeyCode is Keycode.DpadLeft or Keycode.DpadRight or Keycode.MoveHome or Keycode.MoveEnd;
        if (!navigation) return base.DispatchKeyEvent(key);
        if (key.Action != KeyEventActions.Down) return true;
        var available = headers.Where(header => header.Entry.Enabled).ToArray();
        if (available.Length == 0) return true;
        int current = Array.FindIndex(available, header => header.View.HasFocus || header.View.Selected);
        int next = key.KeyCode switch
        {
            Keycode.MoveHome => 0,
            Keycode.MoveEnd => available.Length - 1,
            Keycode.DpadLeft => Math.Max(0, current - 1),
            _ => Math.Min(available.Length - 1, current + 1)
        };
        if (select(available[next].Entry.Id)) available[next].View.RequestFocusFromTouch();
        return true;
    }

    internal void ApplyTheme(NativeThemePalette? value)
    {
        palette = value;
        foreach (var header in headers) header.ApplyTheme(value);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            foreach (var header in headers) header.Dispose();
            headers.Clear();
            palette = null;
        }
        base.Dispose(disposing);
    }

    private sealed class Header : IDisposable
    {
        private readonly NativeTabStrip owner;
        private readonly NativeThemeState textTheme;
        private readonly NativeThemeState closeTheme;
        internal PageEntry Entry { get; set; }
        internal LinearLayout View { get; }
        internal TextView Title { get; }
        internal global::Android.Widget.Button Close { get; }

        internal Header(Context context, PageEntry entry, NativeTabStrip owner)
        {
            this.owner = owner;
            Entry = entry;
            int padding = LayoutMath.Pixels(8, context.Resources!.DisplayMetrics!.Density);
            View = new LinearLayout(context) { Orientation = Orientation.Horizontal };
            View.Tag = Java.Lang.Long.ValueOf((long)entry.Id);
            View.SetGravity(GravityFlags.CenterVertical);
            View.SetPadding(padding, padding, padding, padding);
            Title = new TextView(context);
            Title.SetMaxLines(1);
            Title.SetMaxWidth(LayoutMath.Pixels(240, context.Resources!.DisplayMetrics!.Density));
            Title.Ellipsize = TextUtils.TruncateAt.End;
            Close = new global::Android.Widget.Button(context) { Text = "Close" };
            View.AddView(Title, new LinearLayout.LayoutParams(LayoutParams.WrapContent, LayoutParams.WrapContent));
            View.AddView(Close, new LinearLayout.LayoutParams(LayoutParams.WrapContent, LayoutParams.WrapContent));
            textTheme = new NativeThemeState(Title);
            closeTheme = new NativeThemeState(Close);
        }
        internal void Connect()
        {
            View.Focusable = true;
            View.DescendantFocusability = DescendantFocusability.BeforeDescendants;
            View.Click += Activate;
            View.FocusChange += Focused;
            Close.Click += CloseRequested;
        }
        private void Focused(object? sender, View.FocusChangeEventArgs args)
        {
            if (args.HasFocus && !owner.updating && owner.Enabled && Entry.Enabled) owner.select(Entry.Id);
        }
        private void Activate(object? sender, EventArgs args)
        {
            if (!owner.updating && owner.Enabled && Entry.Enabled) owner.activate(Entry.Id);
        }
        private void CloseRequested(object? sender, EventArgs args)
        {
            if (!owner.updating && owner.Enabled && Entry.Enabled && Close.Visibility == ViewStates.Visible) owner.close(Entry.Id);
        }
        internal void ApplyTheme(NativeThemePalette? palette) { textTheme.Apply(palette); closeTheme.Apply(palette); }
        public void Dispose()
        {
            View.Click -= Activate;
            View.FocusChange -= Focused;
            Close.Click -= CloseRequested;
            View.RemoveAllViews();
            Title.Dispose();
            Close.Dispose();
            View.Dispose();
        }
    }
}

internal sealed class NativeNavigationList : ListView
{
    private readonly PageAdapter adapter;
    private readonly Func<ulong, bool> select;
    private readonly Func<ulong, bool> activate;
    private bool updating;
    private bool expanded = true;

    internal NativeNavigationList(Context context, Func<ulong, bool> select, Func<ulong, bool> activate) : base(context)
    {
        this.select = select;
        this.activate = activate;
        ChoiceMode = ChoiceMode.Single;
        adapter = new PageAdapter(context);
        Adapter = adapter;
        ItemClick += OnActivated;
        ItemSelected += OnSelected;
    }

    internal ulong? SelectedId => CheckedItemPosition >= 0 && CheckedItemPosition < adapter.Count
        ? adapter.Entries[CheckedItemPosition].Id : null;
    internal IReadOnlyList<ulong> Ids => adapter.Entries.Select(entry => entry.Id).ToArray();
    internal bool Expanded => expanded;

    internal void Apply(IReadOnlyList<PageEntry> entries, ulong? selected, bool expanded)
    {
        updating = true;
        try
        {
            this.expanded = expanded;
            adapter.SetEntries(entries);
            ClearChoices();
            int index = selected is ulong id ? adapter.Entries.ToList().FindIndex(entry => entry.Id == id) : -1;
            if (index >= 0) SetItemChecked(index, true);
            SetSelection(index);
            RequestLayout();
        }
        finally { updating = false; }
    }

    protected override void OnMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        int desired = LayoutMath.Pixels(expanded ? 200 : 64, Resources!.DisplayMetrics!.Density);
        var offer = NativeMeasure.Offer(widthMeasureSpec);
        if (offer.Mode != MeasureMode.Exactly)
            widthMeasureSpec = NativeMeasure.Spec(MeasureConstraint.Exactly(
                offer.Mode == MeasureMode.Unspecified ? desired : Math.Min(desired, offer.Size)));
        base.OnMeasure(widthMeasureSpec, heightMeasureSpec);
    }

    private void OnActivated(object? sender, ItemClickEventArgs args)
    {
        if (!updating && Enabled && args.Id > 0 && adapter.Entries.Any(entry => entry.Id == (ulong)args.Id && entry.Enabled))
            activate((ulong)args.Id);
    }
    private void OnSelected(object? sender, ItemSelectedEventArgs args)
    {
        if (!updating && Enabled && HasFocus && args.Id > 0 &&
            adapter.Entries.Any(entry => entry.Id == (ulong)args.Id && entry.Enabled))
            select((ulong)args.Id);
    }
    internal void ApplyTheme(NativeThemePalette? palette) => adapter.ApplyTheme(palette);
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            ItemClick -= OnActivated;
            ItemSelected -= OnSelected;
            Adapter = null;
            adapter.Dispose();
        }
        base.Dispose(disposing);
    }

    private sealed class PageAdapter(Context context) : BaseAdapter
    {
        internal IReadOnlyList<PageEntry> Entries { get; private set; } = [];
        private readonly Dictionary<TextView, NativeThemeState> themes = [];
        private NativeThemePalette? palette;
        public override int Count => Entries.Count;
        public override bool HasStableIds => true;
        public override Java.Lang.Object? GetItem(int position) => Java.Lang.Long.ValueOf((long)Entries[position].Id);
        public override long GetItemId(int position) => (long)Entries[position].Id;
        public override bool AreAllItemsEnabled() => Entries.All(entry => entry.Enabled);
        public override bool IsEnabled(int position) => position >= 0 && position < Count && Entries[position].Enabled;
        internal void SetEntries(IReadOnlyList<PageEntry> entries)
        {
            if (Entries.SequenceEqual(entries)) return;
            Entries = entries.ToArray();
            NotifyDataSetChanged();
        }
        public override View GetView(int position, View? convertView, ViewGroup? parent)
        {
            TextView text;
            if (convertView is TextView recycled) text = recycled;
            else
            {
                text = (TextView)(LayoutInflater.From(context)?.Inflate(global::Android.Resource.Layout.SimpleListItemActivated1, parent, false)
                    ?? throw new InvalidOperationException("Android could not create its native navigation list row."));
                text.SetMaxLines(1);
                text.Ellipsize = TextUtils.TruncateAt.End;
                themes.Add(text, new NativeThemeState(text));
            }
            text.Text = Entries[position].Title;
            text.ContentDescription = Entries[position].Title;
            text.Enabled = Entries[position].Enabled;
            themes[text].Apply(palette);
            return text;
        }
        internal void ApplyTheme(NativeThemePalette? value)
        {
            palette = value;
            foreach (var state in themes.Values) state.Apply(value);
            NotifyDataSetChanged();
        }
        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                foreach (var text in themes.Keys) text.Dispose();
                themes.Clear();
                Entries = [];
                palette = null;
            }
            base.Dispose(disposing);
        }
    }
}

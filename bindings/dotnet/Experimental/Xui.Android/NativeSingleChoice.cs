using Android.Content;
using Android.Views;
using Android.Widget;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal sealed class NativeSingleChoice : Spinner
{
    private readonly ChoiceAdapter choices;
    private readonly Func<ulong, bool> changed;
    private ulong? applied;
    private bool updating;
    private long revision;
    private readonly global::Android.Graphics.Drawables.Drawable? originalPopup;

    internal NativeSingleChoice(Context context, Func<ulong, bool> changed) : base(context)
    {
        this.changed = changed;
        originalPopup = PopupBackground;
        choices = new ChoiceAdapter(context);
        Adapter = choices;
        ItemSelected += SelectionChanged;
    }
    internal ulong? SelectedId => SelectedItemId > 0 ? (ulong)SelectedItemId : null;
    internal void Apply(IReadOnlyList<Choice> items, ulong? selected)
    {
        updating = true;
        try
        {
            applied = selected;
            revision++;
            choices.Set(items, selected is null);
            int index = selected is ulong id ? choices.Index(id) : 0;
            SetSelection(index, false);
        }
        finally { updating = false; }
    }
    private void SelectionChanged(object? sender, ItemSelectedEventArgs args)
    {
        if (updating || !Enabled || args.Id <= 0 || applied == (ulong)args.Id) return;
        var item = choices.Items.FirstOrDefault(item => item.Id == (ulong)args.Id);
        if (item.Id == 0 || !item.Enabled) { Restore(); return; }
        long before = revision;
        if (changed(item.Id))
        {
            if (before == revision) applied = item.Id;
        }
        else Restore();
    }
    internal void ApplyTheme(NativeThemePalette? palette)
    {
        if (palette is null) SetPopupBackgroundDrawable(originalPopup);
        else SetPopupBackgroundDrawable(new global::Android.Graphics.Drawables.ColorDrawable(new global::Android.Graphics.Color(palette.Background)));
        choices.ApplyTheme(palette);
    }
    private void Restore()
    {
        updating = true;
        try { SetSelection(applied is ulong id ? choices.Index(id) : 0, false); }
        finally { updating = false; }
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            ItemSelected -= SelectionChanged;
            Adapter = null;
            choices.Dispose();
        }
        base.Dispose(disposing);
    }

    private sealed class ChoiceAdapter(Context context) : BaseAdapter
    {
        internal IReadOnlyList<Choice> Items { get; private set; } = [];
        private bool placeholder;
        private readonly HashSet<View> views = [];
        private readonly Dictionary<TextView, (NativeThemeState Theme, global::Android.Graphics.Drawables.Drawable? Background)> themes = [];
        private NativeThemePalette? palette;
        public override int Count => Items.Count + (placeholder ? 1 : 0);
        public override bool HasStableIds => true;
        internal int Index(ulong id)
        {
            for (int i = 0; i < Items.Count; i++) if (Items[i].Id == id) return i + (placeholder ? 1 : 0);
            throw new InvalidOperationException("The selected native choice is missing.");
        }
        private Choice? Item(int position) => placeholder && position == 0 ? null : Items[position - (placeholder ? 1 : 0)];
        public override Java.Lang.Object? GetItem(int position) => Java.Lang.Long.ValueOf(GetItemId(position));
        public override long GetItemId(int position) => Item(position) is { } item ? (long)item.Id : -1;
        public override bool AreAllItemsEnabled() => !placeholder && Items.All(item => item.Enabled);
        public override bool IsEnabled(int position) => Item(position)?.Enabled == true;
        internal void Set(IReadOnlyList<Choice> items, bool absent)
        {
            if (placeholder == absent && Items.SequenceEqual(items)) return;
            placeholder = absent;
            Items = items.ToArray();
            NotifyDataSetChanged();
        }
        private View Row(int position, View? recycled, ViewGroup? parent, int layout)
        {
            var view = recycled as TextView ?? (TextView)(LayoutInflater.From(context)?.Inflate(layout, parent, false)
                ?? throw new InvalidOperationException("Android could not create its native spinner row."));
            views.Add(view);
            if (!themes.ContainsKey(view)) themes.Add(view, (new NativeThemeState(view), view.Background));
            var item = Item(position);
            view.Text = item?.Text ?? "No selection";
            view.Enabled = item?.Enabled == true;
            ApplyTheme(view);
            return view;
        }
        private void ApplyTheme(TextView view)
        {
            var baseline = themes[view];
            baseline.Theme.Apply(palette);
            if (palette is null) view.Background = baseline.Background;
            else view.SetBackgroundColor(new global::Android.Graphics.Color(palette.Background));
        }
        internal void ApplyTheme(NativeThemePalette? value)
        {
            palette = value;
            foreach (var view in themes.Keys) ApplyTheme(view);
            NotifyDataSetChanged();
        }
        public override View GetView(int position, View? convertView, ViewGroup? parent) =>
            Row(position, convertView, parent, global::Android.Resource.Layout.SimpleSpinnerItem);
        public override View GetDropDownView(int position, View? convertView, ViewGroup? parent) =>
            Row(position, convertView, parent, global::Android.Resource.Layout.SimpleSpinnerDropDownItem);
        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                foreach (var view in views) view.Dispose();
                views.Clear();
                themes.Clear();
                palette = null;
                Items = [];
            }
            base.Dispose(disposing);
        }
    }
}

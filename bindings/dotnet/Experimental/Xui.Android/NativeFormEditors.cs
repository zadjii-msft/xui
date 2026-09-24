using System.Buffers;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Android.Content;
using Android.OS;
using Android.Text;
using Android.Text.Method;
using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal static class NativeInputPurpose
{
    internal static void Apply(EditText input, InputPurpose purpose) => input.SetRawInputType(purpose switch
    {
        InputPurpose.Normal => InputTypes.ClassText,
        InputPurpose.Email => InputTypes.ClassText | InputTypes.TextVariationEmailAddress,
        InputPurpose.Url => InputTypes.ClassText | InputTypes.TextVariationUri,
        InputPurpose.Telephone => InputTypes.ClassPhone,
        InputPurpose.Number => InputTypes.ClassNumber | InputTypes.NumberFlagSigned | InputTypes.NumberFlagDecimal,
        _ => throw new ArgumentOutOfRangeException(nameof(purpose))
    });
}

internal sealed class NativeFormFilter(EditText owner, int maximumLength, bool password) : Java.Lang.Object, IInputFilter
{
    private readonly WeakReference<EditText> editor = new(owner);

    public Java.Lang.ICharSequence? FilterFormatted(Java.Lang.ICharSequence? source, int start, int end,
        ISpanned? destination, int destinationStart, int destinationEnd)
    {
        if (source is null || destination is null) throw new InvalidOperationException("Android supplied an invalid form edit.");
        if (editor.TryGetTarget(out var field) && field is NativeMultilineEditor { ReadOnly: true, ProgrammaticEdit: false })
            return Reject(destination, destinationStart, destinationEnd, "This editor is read-only.");
        long total = (long)destination.Length() - (destinationEnd - destinationStart) + end - start;
        if (total > (password ? maximumLength : (long)maximumLength * 2))
            return Reject(destination, destinationStart, destinationEnd, "The edit exceeds this field's length limit.");
        char[] buffer = ArrayPool<char>.Shared.Rent(Math.Max(1, (int)total));
        try
        {
            TextUtils.GetChars(destination, 0, destinationStart, buffer, 0);
            TextUtils.GetChars(source, start, end, buffer, destinationStart);
            TextUtils.GetChars(destination, destinationEnd, destination.Length(), buffer, destinationStart + end - start);
            if (password) FormValues.ValidatePassword(buffer.AsSpan(0, (int)total), maximumLength);
            else _ = FormValues.NormalizeMultiline(new string(buffer, 0, (int)total), maximumLength);
            if (editor.TryGetTarget(out var current)) current.Error = null;
            if (password) return null;
            bool carriageReturn = false;
            for (int i = start; i < end; i++) carriageReturn |= source.CharAt(i) == '\r';
            if (!carriageReturn) return null;
            var normalized = new SpannableStringBuilder(source, start, end);
            for (int i = normalized.Length() - 1; i >= 0; i--)
            {
                if (normalized.CharAt(i) != '\r') continue;
                if (i + 1 < normalized.Length() && normalized.CharAt(i + 1) == '\n') normalized.Delete(i, i + 1);
                else normalized.Replace(i, i + 1, "\n");
            }
            return normalized;
        }
        catch (ArgumentException error)
        {
            return Reject(destination, destinationStart, destinationEnd, error.Message);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(MemoryMarshal.AsBytes(buffer.AsSpan()));
            ArrayPool<char>.Shared.Return(buffer);
        }
    }

    private Java.Lang.ICharSequence Reject(ISpanned destination, int start, int end, string reason)
    {
        if (editor.TryGetTarget(out var current)) current.Error = reason;
        Log.Warn("Xui.Android", password ? "Rejected an invalid password edit." : "Rejected an invalid multiline edit.");
        return destination.SubSequenceFormatted(start, end)
            ?? throw new InvalidOperationException("Android could not preserve the rejected edit's original range.");
    }
}

internal sealed class NativeMultilineEditor : EditText
{
    private readonly IKeyListener? editingKeys;
    private readonly NativeFormFilter? filter;
    internal int MaximumLength { get; }
    internal bool ReadOnly { get; private set; }
    internal bool ProgrammaticEdit { get; set; }

    internal NativeMultilineEditor(Context context, int maximumLength) : base(context)
    {
        MaximumLength = maximumLength;
        try
        {
            InputType = InputTypes.ClassText | InputTypes.TextFlagMultiLine;
            SetSingleLine(false);
            Gravity = GravityFlags.Top | GravityFlags.Start;
            ImeOptions = ImeAction.None | (ImeAction)ImeFlags.NoEnterAction | (ImeAction)ImeFlags.NoFullscreen;
            editingKeys = KeyListener;
            filter = new NativeFormFilter(this, maximumLength, password: false);
            SetFilters([filter]);
        }
        catch (Exception creationError)
        {
            try { Dispose(); }
            catch (Exception cleanupError) { throw new AggregateException(creationError, cleanupError); }
            throw;
        }
    }

    internal void SetReadOnly(bool value)
    {
        if (ReadOnly == value) return;
        int start = SelectionStart;
        int end = SelectionEnd;
        bool previous = ProgrammaticEdit;
        ProgrammaticEdit = true;
        try
        {
            ReadOnly = value;
            SetTextIsSelectable(value);
            KeyListener = value ? null : editingKeys;
            if (!value) SetRawInputType(InputTypes.ClassText | InputTypes.TextFlagMultiLine);
            if (start >= 0 && end >= 0)
            {
                var range = new TextSelection(start, end).ClampTo(Text ?? "");
                SetSelection(range.Start, range.End);
            }
        }
        finally { ProgrammaticEdit = previous; }
    }

    public override IInputConnection? OnCreateInputConnection(EditorInfo? attributes) =>
        ReadOnly ? null : base.OnCreateInputConnection(attributes);

    public override bool OnTextContextMenuItem(int id) =>
        ReadOnly && id is global::Android.Resource.Id.Cut or global::Android.Resource.Id.Paste
            ? false : base.OnTextContextMenuItem(id);

    public override bool PerformAccessibilityAction(global::Android.Views.Accessibility.Action action, Bundle? arguments) =>
        ReadOnly && action is global::Android.Views.Accessibility.Action.SetText or
            global::Android.Views.Accessibility.Action.Cut or global::Android.Views.Accessibility.Action.Paste
            ? false : base.PerformAccessibilityAction(action, arguments);

    public override void OnInitializeAccessibilityNodeInfo(global::Android.Views.Accessibility.AccessibilityNodeInfo? info)
    {
        base.OnInitializeAccessibilityNodeInfo(info);
        if (info is not null)
        {
            info.Editable = !ReadOnly;
            if (ReadOnly)
            {
                info.RemoveAction(global::Android.Views.Accessibility.AccessibilityNodeInfo.AccessibilityAction.ActionSetText);
                info.RemoveAction(global::Android.Views.Accessibility.AccessibilityNodeInfo.AccessibilityAction.ActionCut);
                info.RemoveAction(global::Android.Views.Accessibility.AccessibilityNodeInfo.AccessibilityAction.ActionPaste);
            }
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (!disposing) { base.Dispose(false); return; }
        var failures = new List<Exception>();
        void Cleanup(System.Action action)
        {
            try { action(); }
            catch (Exception error) { failures.Add(error); }
        }
        if (Handle != IntPtr.Zero) Cleanup(() => SetFilters([]));
        if (filter is not null) Cleanup(filter.Dispose);
        Cleanup(() => base.Dispose(true));
        if (failures.Count != 0) throw new AggregateException("Native multiline cleanup failed.", failures);
    }
}

internal sealed class NativePasswordEditor : EditText
{
    private readonly NativeFormFilter? filter;
    private readonly PasswordWatcher? watcher;
    private bool setting;
    internal int MaximumLength { get; }
    internal Func<bool>? Changed { get; set; }
    internal int PasswordLength => EditableText?.Length() ?? 0;

    internal NativePasswordEditor(Context context, int maximumLength) : base(context)
    {
        MaximumLength = maximumLength;
        try
        {
            InputType = InputTypes.ClassText | InputTypes.TextVariationPassword | InputTypes.TextFlagNoSuggestions;
            SetSingleLine(true);
            TransformationMethod = PasswordTransformationMethod.Instance;
            SaveEnabled = false;
            ImportantForAutofill = ImportantForAutofill.No;
            ImeOptions = ImeAction.None | (ImeAction)ImeFlags.NoFullscreen | (ImeAction)ImeFlags.NoPersonalizedLearning;
            filter = new NativeFormFilter(this, maximumLength, password: true);
            SetFilters([filter]);
            watcher = new PasswordWatcher(this);
            AddTextChangedListener(watcher);
            EditorAction += SuppressSubmit;
        }
        catch (Exception creationError)
        {
            try { Dispose(); }
            catch (Exception cleanupError) { throw new AggregateException(creationError, cleanupError); }
            throw;
        }
    }

    private void SuppressSubmit(object? sender, TextView.EditorActionEventArgs args) => args.Handled = true;

    public override bool OnTextContextMenuItem(int id) =>
        id is global::Android.Resource.Id.Copy or global::Android.Resource.Id.Cut ? false : base.OnTextContextMenuItem(id);

    public override bool PerformAccessibilityAction(global::Android.Views.Accessibility.Action action, Bundle? arguments) =>
        action is global::Android.Views.Accessibility.Action.Copy or global::Android.Views.Accessibility.Action.Cut
            ? false : base.PerformAccessibilityAction(action, arguments);

    public override void OnInitializeAccessibilityNodeInfo(global::Android.Views.Accessibility.AccessibilityNodeInfo? info)
    {
        base.OnInitializeAccessibilityNodeInfo(info);
        info?.RemoveAction(global::Android.Views.Accessibility.AccessibilityNodeInfo.AccessibilityAction.ActionCopy);
        info?.RemoveAction(global::Android.Views.Accessibility.AccessibilityNodeInfo.AccessibilityAction.ActionCut);
    }

    internal void SetPassword(ReadOnlySpan<char> value)
    {
        FormValues.ValidatePassword(value, MaximumLength);
        char[] buffer = ArrayPool<char>.Shared.Rent(Math.Max(1, value.Length));
        using var native = new Java.Lang.StringBuilder(value.Length);
        try
        {
            value.CopyTo(buffer);
            native.Append(buffer, 0, value.Length);
            setting = true;
            var editable = EditableText ?? throw new InvalidOperationException("The native password editor has no editable buffer.");
            editable.Replace(0, editable.Length(), native);
        }
        finally
        {
            setting = false;
            for (int i = 0; i < native.Length(); i++) native.SetCharAt(i, '\0');
            CryptographicOperations.ZeroMemory(MemoryMarshal.AsBytes(buffer.AsSpan()));
            ArrayPool<char>.Shared.Return(buffer);
        }
    }

    internal void WithPassword(PasswordReceiver receiver)
    {
        ArgumentNullException.ThrowIfNull(receiver);
        var editable = EditableText ?? throw new InvalidOperationException("The native password editor has no editable buffer.");
        int length = editable.Length();
        if (length > MaximumLength) throw new InvalidOperationException("The native password exceeds its configured limit.");
        char[] buffer = ArrayPool<char>.Shared.Rent(Math.Max(1, length));
        try
        {
            TextUtils.GetChars(editable, 0, length, buffer, 0);
            FormValues.ValidatePassword(buffer.AsSpan(0, length), MaximumLength);
            receiver(buffer.AsSpan(0, length));
        }
        finally
        {
            CryptographicOperations.ZeroMemory(MemoryMarshal.AsBytes(buffer.AsSpan()));
            ArrayPool<char>.Shared.Return(buffer);
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (!disposing) { base.Dispose(false); return; }
        var failures = new List<Exception>();
        void Cleanup(System.Action action)
        {
            try { action(); }
            catch (Exception error) { failures.Add(error); }
        }
        Changed = null;
        if (Handle != IntPtr.Zero)
        {
            Cleanup(() => SetPassword([]));
            if (watcher is not null) Cleanup(() => RemoveTextChangedListener(watcher));
            Cleanup(() => EditorAction -= SuppressSubmit);
            Cleanup(() => SetFilters([]));
        }
        if (watcher is not null) Cleanup(watcher.Dispose);
        if (filter is not null) Cleanup(filter.Dispose);
        Cleanup(() => base.Dispose(true));
        if (failures.Count != 0) throw new AggregateException("Native password cleanup failed.", failures);
    }

    private sealed class PasswordWatcher(NativePasswordEditor owner) : Java.Lang.Object, ITextWatcher
    {
        public void BeforeTextChanged(Java.Lang.ICharSequence? value, int start, int count, int after) { }
        public void OnTextChanged(Java.Lang.ICharSequence? value, int start, int before, int count) { }
        public void AfterTextChanged(IEditable? value)
        {
            if (!owner.setting) owner.Changed?.Invoke();
        }
    }
}

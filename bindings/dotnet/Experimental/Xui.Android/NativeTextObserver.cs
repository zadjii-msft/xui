using Android.Text;
using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

internal sealed class NativeTextObserver : IDisposable
{
    private EditText? input;
    private readonly AndroidDispatcher dispatcher;
    private Func<TextInteraction, bool>? changed;
    private readonly SpanListener spans;
    private readonly TextListener? text;
    private TextInteraction? delivered;
    private bool posted;
    private bool disposed;

    internal NativeTextObserver(EditText input, AndroidDispatcher dispatcher, Func<TextInteraction, bool> changed)
    {
        this.input = input;
        this.dispatcher = dispatcher;
        this.changed = changed;
        spans = new SpanListener(this);
        try
        {
            text = new TextListener(this);
            input.AddTextChangedListener(text);
            input.FocusChange += FocusChanged;
            AttachSpan();
            Schedule();
        }
        catch (Exception creationError)
        {
            try { Dispose(); }
            catch (Exception cleanupError) { throw new AggregateException(creationError, cleanupError); }
            throw;
        }
    }

    internal static TextInteraction Read(EditText input)
    {
        var editable = input.EditableText;
        return new(input.HasFocus, editable is not null && BaseInputConnection.GetComposingSpanStart(editable) >= 0);
    }

    private void FocusChanged(object? sender, View.FocusChangeEventArgs args) => Schedule();

    private void DetachSpan()
    {
        if (!disposed) input!.EditableText?.RemoveSpan(spans);
    }

    private void AttachSpan()
    {
        if (disposed) return;
        var editable = input!.EditableText;
        editable?.SetSpan(spans, 0, editable.Length(), SpanTypes.InclusiveInclusive);
    }

    private void Schedule()
    {
        if (disposed || posted) return;
        posted = true;
        try { dispatcher.Post(Deliver); }
        catch
        {
            posted = false;
            throw;
        }
    }

    private void Deliver()
    {
        posted = false;
        if (disposed) return;
        try
        {
            var current = Read(input!);
            if (delivered != current && changed!(current)) delivered = current;
        }
        catch (Exception error)
        {
            Log.Error("Xui.Android", error.ToString());
            throw;
        }
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        changed = null;
        var editor = input!;
        input = null;
        var failures = new List<Exception>();
        void Cleanup(Action action)
        {
            try { action(); }
            catch (Exception error) { failures.Add(error); }
        }
        if (text is not null) Cleanup(() => editor.RemoveTextChangedListener(text));
        Cleanup(() => editor.FocusChange -= FocusChanged);
        Cleanup(() => editor.EditableText?.RemoveSpan(spans));
        if (text is not null) Cleanup(text.Dispose);
        Cleanup(spans.Dispose);
        if (failures.Count != 0) throw new AggregateException("Android text observer cleanup failed.", failures);
    }

    private sealed class SpanListener(NativeTextObserver owner) : Java.Lang.Object, ISpanWatcher, INoCopySpan
    {
        public void OnSpanAdded(ISpannable? text, Java.Lang.Object? what, int start, int end) => owner.Schedule();
        public void OnSpanChanged(ISpannable? text, Java.Lang.Object? what, int oldStart, int oldEnd, int newStart, int newEnd) => owner.Schedule();
        public void OnSpanRemoved(ISpannable? text, Java.Lang.Object? what, int start, int end) => owner.Schedule();
    }

    private sealed class TextListener(NativeTextObserver owner) : Java.Lang.Object, ITextWatcher
    {
        public void BeforeTextChanged(Java.Lang.ICharSequence? value, int start, int count, int after) => owner.DetachSpan();
        public void OnTextChanged(Java.Lang.ICharSequence? value, int start, int before, int count) { }
        public void AfterTextChanged(IEditable? value)
        {
            owner.AttachSpan();
            owner.Schedule();
        }
    }
}

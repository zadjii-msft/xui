using Android.Text;
using Android.Text.Method;
using Android.Util;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using PortableDemo;
using Xui.Experimental.Android;
using Xui.Experimental.AndroidOrderDemo;
using Xui.Experimental.Portable;

namespace Xui.Experimental.AndroidOrderDeviceTests;

public sealed partial class TestActivity
{
    private async Task FormsChecks(OrderSurface surface, AndroidDispatcher dispatcher)
    {
        int before = assertions;
        using var host = new Host(dispatcher);
        var form = new FormsWorkbench(host);
        var backend = new AndroidBackend(surface, dispatcher);
        host.Attach(backend);
        for (int attempt = 0; !surface.IsAttachedToWindow && attempt < 100; attempt++) await Task.Delay(10);
        Assert(surface.IsAttachedToWindow, "The Forms fixture uses an actual attached native window.");
        var driver = new FormsDriver(backend);
        using var stream = typeof(TestActivity).Assembly.GetManifestResourceStream("FormsScenarios.json")
            ?? throw new InvalidOperationException("The shared Forms corpus is missing.");
        using var reader = new StreamReader(stream);
        int shared = FormsScenarioRunner.Run(reader.ReadToEnd(), driver);
        assertions += shared;
        var number = (EditText)backend.FindViews("forms-number").Single();
        Assert(number.KeyListener is not DigitsKeyListener, "Number purpose does not install a native digit-filtering key listener.");
        driver.Change("forms-number", " -12,5 raw \u674e ");
        Assert(number.Text == " -12,5 raw \u674e " && form.Number == number.Text,
            "Native advisory number input preserves nonnumeric text, whitespace and Unicode.");
        var notes = (NativeMultilineEditor)backend.FindViews("forms-notes").Single();
        driver.Change("forms-notes", "first\r\nsecond\rlast");
        Assert(notes.Text == "first\nsecond\nlast" && form.Notes == notes.Text,
            "Native multiline edits canonicalize CRLF and CR before updating the model.");
        driver.Change("forms-notes", "line");
        notes.RequestFocus();
        notes.SetSelection(4);
        using (var down = new KeyEvent(KeyEventActions.Down, Keycode.Enter))
        using (var up = new KeyEvent(KeyEventActions.Up, Keycode.Enter))
        {
            notes.DispatchKeyEvent(down);
            notes.DispatchKeyEvent(up);
        }
        Assert(notes.Text == "line\n" && form.Notes == "line\n", "Native Enter inserts a newline instead of submitting the multiline editor.");
        int noteChanges = 0;
        form.NotesInput.Changed += _ => noteChanges++;
        form.Notes = "programmatic\r\nvalue";
        Assert(noteChanges == 0 && notes.Text == "programmatic\nvalue",
            "Programmatic canonical multiline updates are silent.");
        notes.SetSelection(1, 4);
        using var composing = notes.EditableText!;
        BaseInputConnection.SetComposingSpans(composing);
        int start = BaseInputConnection.GetComposingSpanStart(composing);
        int end = BaseInputConnection.GetComposingSpanEnd(composing);
        form.Email = "unrelated";
        Assert(ReferenceEquals(notes, backend.FindViews("forms-notes").Single()) && notes.HasFocus &&
            notes.SelectionStart == 1 && notes.SelectionEnd == 4 &&
            BaseInputConnection.GetComposingSpanStart(composing) == start &&
            BaseInputConnection.GetComposingSpanEnd(composing) == end,
            "Unrelated form updates retain native multiline identity, selection and composition.");
        BaseInputConnection.RemoveComposingSpans(composing);
        form.Locked = true;
        string frozen = notes.Text!;
        using (var keyDown = new KeyEvent(KeyEventActions.Down, Keycode.A))
        using (var keyUp = new KeyEvent(KeyEventActions.Up, Keycode.A))
        {
            notes.DispatchKeyEvent(keyDown);
            notes.DispatchKeyEvent(keyUp);
        }
        using (var info = notes.CreateAccessibilityNodeInfo()!)
            Assert(notes.KeyListener is null && !info.Editable && notes.Text == frozen && noteChanges == 0,
                $"Read-only native editability: listenerNull={notes.KeyListener is null}, editable={info.Editable}, textPreserved={notes.Text == frozen}, callbacks={noteChanges}.");
        form.Locked = false;
        bool invalid = false;
        try { form.Notes = new string('x', 65); }
        catch (ArgumentException) { invalid = true; }
        Assert(invalid && notes.Text == frozen, "Oversized programmatic multiline text fails before native mutation.");

        var secret = (NativePasswordEditor)backend.FindViews("forms-password").Single();
        int notices = form.Notices;
        form.Secret.SetPassword("A\U0001F642B");
        Assert(form.Secret.Length == 4 && form.Notices == notices, "Opaque programmatic password writes preserve UTF-16 length and remain silent.");
        int reads = 0;
        form.Secret.WithPassword(value => { reads++; Assert(value.Length == 4, "Scoped password read exposes only its requested bounded buffer."); });
        Assert(reads == 1, "A password receiver runs exactly once.");
        bool receiverFailed = false;
        try { form.Secret.WithPassword(_ => throw new InvalidOperationException("Expected receiver failure.")); }
        catch (InvalidOperationException) { receiverFailed = true; }
        Assert(receiverFailed && host.IsAttached && form.Secret.Length == 4, "Receiver errors propagate without retaining a buffer or replacing the editor.");
        foreach (string invalidSecret in new[] { "\0", "\r", "\n", "\ud800", new string('x', 33) })
        {
            invalid = false;
            try { form.Secret.SetPassword(invalidSecret); }
            catch (ArgumentException) { invalid = true; }
            Assert(invalid && form.Secret.Length == 4, "Invalid opaque password input is rejected before native mutation.");
        }
        using (var info = secret.CreateAccessibilityNodeInfo()!)
            Assert(info.Password && secret.TransformationMethod is PasswordTransformationMethod &&
                secret.ImportantForAutofill == ImportantForAutofill.No,
                $"Native password boundary: AXpassword={info.Password}, masked={secret.TransformationMethod is PasswordTransformationMethod}, autofill={secret.ImportantForAutofill}.");
        Assert(!secret.OnTextContextMenuItem(global::Android.Resource.Id.Copy) &&
            !secret.OnTextContextMenuItem(global::Android.Resource.Id.Cut),
            "Native password copy and cut are rejected without accessing the clipboard.");
        form.Secret.Visible = false;
        form.Secret.Enabled = false;
        Assert(form.Secret.Length == 4, "Hiding or disabling the password does not retire its secret.");
        form.Secret.Visible = true;
        form.Secret.Enabled = true;
        using var detach = new PasswordDetachProbe();
        secret.AddOnAttachStateChangeListener(detach);
        Func<bool>? stale = secret.Changed;
        host.Detach();
        Assert(secret.Handle == IntPtr.Zero && detach.Length == 0,
            "Password retirement clears before native unmount and releases the editor.");
        var replacement = new AndroidBackend(surface, dispatcher);
        host.Attach(replacement);
        Assert(form.Secret.Length == 0 && stale is not null && !stale() && form.Notices == notices,
            "A new attachment starts empty and stale password notices cannot reach it.");
        host.Dispose();
        Assert(surface.ChildCount == 0, "The Forms fixture releases its final attachment.");
        Log.Info("Xui.Android.Orders", $"Forms: {shared} shared expectations; {assertions - before - shared} native editor assertions. No password value logged.");
    }

    private sealed class PasswordDetachProbe : Java.Lang.Object, View.IOnAttachStateChangeListener
    {
        internal int? Length { get; private set; }
        public void OnViewAttachedToWindow(View? view) { }
        public void OnViewDetachedFromWindow(View? view)
        {
            if (view is NativePasswordEditor editor) Length = editor.PasswordLength;
            view?.RemoveOnAttachStateChangeListener(this);
        }
    }

    private sealed class FormsDriver(AndroidBackend backend) : IFormsScenarioDriver
    {
        private readonly NativeDriver controls = new(backend);
        public void Click(string id) => controls.Click(id);
        public void Change(string id, string value)
        {
            var editor = (EditText)backend.FindViews(id).Single();
            if (editor is NativePasswordEditor) throw new InvalidOperationException("Opaque password values cannot use the text driver.");
            using var info = new EditorInfo();
            using var connection = editor.OnCreateInputConnection(info)
                ?? throw new InvalidOperationException("The native editor has no editable input connection.");
            editor.SetSelection(0, editor.Text?.Length ?? 0);
            using var text = new Java.Lang.String(value);
            if (!connection.CommitText(text, 1)) throw new InvalidOperationException("The native input connection rejected its edit.");
        }
        public void SeedPassword(string id, int codeUnits)
        {
            var editor = (NativePasswordEditor)backend.FindViews(id).Single();
            char[] value = new char[codeUnits];
            Array.Fill(value, 'x');
            using var text = new Java.Lang.StringBuilder(codeUnits);
            try
            {
                text.Append(value);
                using var info = new EditorInfo();
                using var connection = editor.OnCreateInputConnection(info)
                    ?? throw new InvalidOperationException("The native password editor has no input connection.");
                if (!connection.CommitText(text, 1)) throw new InvalidOperationException("The native password edit was rejected.");
            }
            finally
            {
                Array.Clear(value);
                for (int i = 0; i < text.Length(); i++) text.SetCharAt(i, '\0');
            }
        }
        public string Text(string id)
        {
            if (backend.FindViews(id).Single() is NativePasswordEditor)
                throw new InvalidOperationException("Opaque password values cannot be projected as text.");
            return controls.Text(id);
        }
        public string Purpose(string id)
        {
            var editor = (EditText)backend.FindViews(id).Single();
            using var info = new EditorInfo();
            using var connection = editor.OnCreateInputConnection(info);
            var type = info.InputType;
            if ((type & InputTypes.MaskClass) == InputTypes.ClassNumber) return "Number";
            if ((type & InputTypes.MaskClass) == InputTypes.ClassPhone) return "Telephone";
            return (type & InputTypes.MaskVariation) switch
            {
                InputTypes.TextVariationEmailAddress => "Email",
                InputTypes.TextVariationUri => "Url",
                _ => "Normal"
            };
        }
        public bool ReadOnly(string id) => ((EditText)backend.FindViews(id).Single()).KeyListener is null;
        public int PasswordLength(string id) => ((NativePasswordEditor)backend.FindViews(id).Single()).PasswordLength;
    }
}

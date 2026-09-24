using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeFormsScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = new FormsWorkbench(host);
        var backend = new WindowsBackend(surface, dispatcher);
        Check(backend.SupportsForms, "The native Forms contract is required.");
        host.Attach(backend);
        application.Show(window);
        var ui = new Driver(application, window, host, () => backend);
        var driver = new FormsDriver(ui, host, sample, backend);
        bool closed = false;
        var work = Task.Run(() =>
        {
            try
            {
                Interlocked.Add(ref assertions, FormsScenarioRunner.Run(Resource("FormsScenarios.json"), driver));
                ui.Ui(() =>
                {
                    Check(host.TryFocus(sample.Secret), "The native password editor could not receive focus.");
                    nint editor = GetFocus();
                    int before = sample.Notices;
                    sample.Secret.SetPassword("synthetic".AsSpan());
                    Check(sample.Secret.Length == 9 && sample.Notices == before, "Programmatic password assignment emitted a notice.");
                    sample.Secret.Visible = false;
                    sample.Secret.Visible = true;
                    sample.Secret.Enabled = false;
                    sample.Secret.Enabled = true;
                    Check(sample.Secret.Length == 9, "Hiding or disabling a password cleared attachment-owned data.");
                    bool receiverRan = false;
                    sample.Secret.WithPassword(value =>
                    {
                        receiverRan = true;
                        Check(value.SequenceEqual("synthetic".AsSpan()), "The scoped password receiver returned incorrect synthetic data.");
                    });
                    Check(receiverRan, "The scoped password receiver did not execute.");
                    Throws<InvalidOperationException>(() => sample.Secret.WithPassword(_ => host.Detach()));
                    sample.Secret.SetPassword(ReadOnlySpan<char>.Empty);
                    Check(sample.Secret.Length == 0 && sample.Notices == before, "Password clearing was not silent.");
                    Check(IsWindow(editor), "Programmatic secret updates replaced the native editor.");
                    sample.Notes = "canonical\r\nnewlines";
                    Check(sample.NotesInput.Text == "canonical\nnewlines" && sample.Notes == "canonical\r\nnewlines",
                        "Multiline normalization did not stay at the element boundary.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(editor), "Forms detach leaked native editors.");
                    Throws<InvalidOperationException>(() => _ = sample.Secret.Length);
                    backend = new WindowsBackend(surface, dispatcher);
                    host.Attach(backend);
                    Check(sample.Secret.Length == 0, "A replacement attachment restored retired secret data.");
                    sample.Secret.SetPassword("closing synthetic".AsSpan());
                    closed = true;
                    window.Close();
                    host.Dispose();
                    Check(HandleCount(window) == baseline, "Closing-window secret cleanup leaked its arena.");
                });
            }
            finally
            {
                if (!closed) ui.Ui(() =>
                {
                    if (window.State is WindowState.Created or WindowState.Open) window.Close();
                });
            }
        });
        RunNativeWork(application, work);
    }

    private sealed class FormsDriver(Driver ui, P.Host host, FormsWorkbench sample, WindowsBackend backend) : IFormsScenarioDriver
    {
        public void Click(string id) => ui.Click(id);
        public void Change(string id, string value)
        {
            if (id != "forms-notes") { ui.Change(id, value); return; }
            ui.Ui(() =>
            {
                Check(host.TryFocus(sample.NotesInput), "The multiline editor could not receive focus.");
                Check(SendText(GetFocus(), 0x000c, 0, value) != 0, "Native multiline replacement failed.");
            });
            ui.Wait(() => sample.Notes == value);
        }
        public void SeedPassword(string id, int codeUnits)
        {
            if (id != "forms-password") throw new ArgumentException("Unknown password fixture.");
            ui.Ui(() =>
            {
                Check(host.TryFocus(sample.Secret), "The password fixture could not receive native focus.");
                nint editor = GetFocus();
                for (int i = 0; i < codeUnits; i++) SendMessage(editor, 0x0102, (nuint)'x', 1);
            });
            ui.Wait(() => sample.Secret.Length == codeUnits && sample.SecretLength == codeUnits);
        }
        public string Text(string id) => id == "forms-notes"
            ? ui.Ui(() => P.FormValues.NormalizeMultiline(((MultilineText)backend.FindElements(id).Single()).Text, 64))
            : ui.Text(id);
        public string Purpose(string id) => ui.Ui(() => ((TextInput)backend.FindElements(id).Single()).Purpose.ToString());
        public bool ReadOnly(string id) => ui.Ui(() => ((MultilineText)backend.FindElements(id).Single()).ReadOnly);
        public int PasswordLength(string id) => ui.Ui(() => checked((int)((PasswordInput)backend.FindElements(id).Single()).Length));
    }

    private static void NativeWorkshopScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = new WorkshopRegistration(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var ui = new Driver(application, window, host, () => backend);
        var driver = new WorkshopDriver(ui, host, sample, backend);
        var work = Task.Run(() =>
        {
            try
            {
                Interlocked.Add(ref assertions, WorkshopScenarioRunner.Run(Resource("WorkshopScenarios.json"), driver));
                ui.Ui(() =>
                {
                    var input = (MultilineText)backend.FindControls("workshop-goals").Single();
                    Check(host.TryFocus(sample.GoalsInput), "The shared multiline goals editor could not receive focus.");
                    nint editor = GetFocus();
                    Check(GetWindowRect(editor, out var bounds) && bounds.Bottom - bounds.Top > 80,
                        "The shared multiline editor has no usable native viewport.");
                    Check(input.ReadOnly == sample.State.Reviewing, "Shared review did not preserve native read-only state.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(editor), "Workshop teardown leaked native editors.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }

    private sealed class WorkshopDriver(Driver ui, P.Host host, WorkshopRegistration sample, WindowsBackend backend)
        : IWorkshopScenarioDriver
    {
        public void Click(string id) => ui.Click(id);
        public void Change(string id, string value)
        {
            if (id != "workshop-goals") { ui.Change(id, value); return; }
            ui.Ui(() =>
            {
                Check(host.TryFocus(sample.GoalsInput), "The workshop editor did not receive native focus.");
                Check(SendText(GetFocus(), 0x000c, 0, value) != 0, "Native workshop notes replacement failed.");
            });
            ui.Wait(() => sample.GoalsInput.Text == P.FormValues.NormalizeMultiline(value, 500));
        }
        public string Text(string id) => id == "workshop-goals"
            ? ui.Ui(() => P.FormValues.NormalizeMultiline(((MultilineText)backend.FindControls(id).Single()).Text, 500))
            : ui.Text(id);
        public bool Enabled(string id) => ui.Enabled(id);
        public bool Visible(string id) => ui.Visible(id);
        public bool Checked(string id) => id == "workshop-consent"
            ? ui.Ui(() => ((CheckBox)backend.FindControls(id).Single()).State == CheckState.Checked) : ui.Checked(id);
        public bool ReadOnly(string id) => ui.Ui(() => ((MultilineText)backend.FindControls(id).Single()).ReadOnly);
        public int MaximumLength(string id) => ui.Ui(() =>
        {
            Check(host.TryFocus(sample.GoalsInput), "The workshop limit fixture could not focus its native editor.");
            return checked((int)SendMessage(GetFocus(), 0x00d5, 0, 0));
        });
        public string Purpose(string id) => ui.Ui(() => ((TextInput)backend.FindControls(id).Single()).Purpose.ToString());
    }
}

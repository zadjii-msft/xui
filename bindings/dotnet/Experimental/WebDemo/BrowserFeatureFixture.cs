#if DEBUG
using Microsoft.JSInterop;
using PortableDemo;
using Xui.Experimental.Portable;
using Xui.Experimental.Web;

internal sealed class BrowserFeatureFixture : IDisposable
{
    private readonly Host host;
    private readonly IJSInProcessObjectReference module;
    private readonly FormsWorkbench? forms;
    private readonly PresentationWorkbench? presentation;
    private readonly RevealWorkbench? reveal;
    private readonly KeyedStack? secretRows;
    private PortableMutation.SecretRow? secretRow;
    private PasswordInput Secret => forms?.Secret ?? secretRow!.Secret;
    private bool disposed;

    internal BrowserFeatureFixture(IJSInProcessObjectReference module, string kind)
    {
        this.module = module;
        host = new Host(new BrowserDispatcher(error => module.InvokeVoid("reportError", "errors", error.ToString())));
        try
        {
            if (kind == "forms") forms = new FormsWorkbench(host);
            else if (kind == "presentation") presentation = PresentationWorkbench.Create(host);
            else if (kind == "reveal") reveal = new RevealWorkbench(host);
            else if (kind == "secret-rows")
            {
                using (var build = host.BeginBuild())
                {
                    secretRows = host.KeyedStack(Axis.Vertical);
                    host.SetContent(secretRows);
                    build.Complete();
                }
                AddSecretRow();
            }
            else throw new ArgumentException("Unknown native feature fixture.", nameof(kind));
            Attach();
        }
        catch { host.Dispose(); throw; }
    }
    private void Attach() => host.Attach(new DomBackend(module, "app", "errors"));
    internal object Command(string command, string? value)
    {
        switch (command)
        {
            case "detach": host.Detach(); return new { detached = true };
            case "attach": Attach(); return new { attached = true };
            case "dispose": Dispose(); return new { disposed = true };
            case "secret-seed":
                Secret.SetPassword(new string('x', int.Parse(value!)));
                return new { length = Secret.Length, notices = forms?.Notices ?? 0 };
            case "secret-length": return new { length = Secret.Length };
            case "secret-read":
                int length = -1;
                Secret.WithPassword(secret => length = secret.Length);
                return new { length };
            case "secret-remove": secretRows!.Reconcile([]); return new { removed = true };
            case "secret-add": AddSecretRow(); return new { length = Secret.Length };
            case "notes-set":
                forms!.NotesInput.Text = value!;
                return new { text = forms.NotesInput.Text, authored = forms.Notes };
            case "reveal-state": return host.GetRevealPresentation(reveal!.Drawer);
            case "clear-typography":
                var targets = new Control[]
                {
                    presentation!.TitleLabel, presentation.DraftInput, presentation.AccentToggle,
                    presentation.AccentCheckBox, Find(presentation.Root, "presentation-bold")
                };
                foreach (var target in targets) target.Typography = null;
                return new { cleared = targets.Length };
            default: throw new ArgumentException("Unknown native feature command.", nameof(command));
        }
    }
    private void AddSecretRow() => secretRows!.Reconcile([
        KeyedItem.Create("secret", owner => secretRow = new PortableMutation.SecretRow(owner))
    ]);
    private static Control Find(Element root, string id)
        => FindOptional(root, id) ?? throw new InvalidOperationException("Missing fixture control.");
    private static Control? FindOptional(Element root, string id)
    {
        if (root is Control control && control.AutomationId == id) return control;
        foreach (var child in root.Children)
        {
            var found = FindOptional(child, id);
            if (found is not null) return found;
        }
        return null;
    }
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        host.Dispose();
    }
}
#endif

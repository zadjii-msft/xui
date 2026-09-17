using System.Text;

namespace Xui.Designer;

internal sealed class DesignerRecoveryDialog
{
    private readonly DesignerDocumentStore document;
    private readonly Action recovered;
    private readonly Action<string> report;
    private IReadOnlyList<DesignerRecovery> drafts = [];
    private int selected = -1;
    private bool updating;
    private bool deleteConfirmed;

    internal ContentDialog View { get; }
    internal ComboBox Choices { get; }
    internal DesignerRecoveryLayout Layout { get; }
    internal Guid? SelectedId => Selected?.Id;
    private DesignerRecovery? Selected => selected >= 0 && selected < drafts.Count ? drafts[selected] : null;

    internal DesignerRecoveryDialog(Window window, DesignerDocumentStore document, Action recovered, Action<string> report)
    {
        this.document = document;
        this.recovered = recovered;
        this.report = report;
        Choices = window.ComboBox("Recovery drafts");
        Layout = new DesignerRecoveryLayout(window, Choices, attach: false);
        View = window.ContentDialog("Recovery drafts", Layout.Root);
        View.Primary.Text = "Recover copy";
        View.Primary.Enabled = false;
        Choices.Event += value =>
        {
            if (updating || value.Kind != EventKind.Selection) return;
            selected = value.Value > 0 && value.Value <= (ulong)drafts.Count ? checked((int)value.Value - 1) : -1;
            deleteConfirmed = false;
            Layout.ConfirmDelete.Checked = false;
            RefreshDetails();
        };
        Layout.ConfirmDelete.Event += value =>
        {
            if (value.Kind != EventKind.Change) return;
            deleteConfirmed = value.Value != 0;
            RefreshDetails();
        };
        Layout.Refresh.Click += Refresh;
        Layout.Delete.Click += DeleteSelected;
        View.OnResult(accepted =>
        {
            if (!accepted || Selected is not { } draft) return;
            try
            {
                document.Recover(draft.Id);
            }
            catch (Exception error) when (ExpectedFileError(error))
            {
                report(Display(error.Message));
                return;
            }
            recovered();
        });
    }

    internal void Show(Control anchor)
    {
        Refresh();
        View.Show(anchor);
    }

    internal void Refresh()
    {
        Guid? previous = SelectedId;
        try
        {
            drafts = document.ListRecovery();
        }
        catch (Exception error) when (ExpectedFileError(error))
        {
            drafts = [];
            selected = -1;
            deleteConfirmed = false;
            Choices.Enabled = false;
            View.Primary.Enabled = false;
            Layout.ConfirmDelete.Checked = false;
            Layout.ConfirmDelete.Enabled = false;
            Layout.Delete.Enabled = false;
            Layout.Details.Text = Display(error.Message);
            Layout.SourcePreview.Text = "";
            report(Display(error.Message));
            return;
        }
        selected = previous is { } id ? drafts.ToList().FindIndex(draft => draft.Id == id) : -1;
        if (selected < 0 && drafts.Count > 0) selected = 0;
        var choices = drafts.Select((draft, index) => new Choice(
            (ulong)index + 1,
            Display($"{(draft.Error is null ? "" : "Error: ")}{Path.GetFileName(draft.OriginalPath ?? draft.SourcePath)} - {draft.UpdatedAt.ToLocalTime():g}"))).ToArray();
        updating = true;
        try
        {
            Choices.SetItems(choices, selected < 0 ? null : (ulong)selected + 1);
            deleteConfirmed = false;
            Layout.ConfirmDelete.Checked = false;
        }
        finally { updating = false; }
        RefreshDetails();
    }

    internal void DeleteSelected()
    {
        if (Selected is not { } draft || !deleteConfirmed)
        {
            View.SetValidationMessage("Select a draft and confirm its deletion.");
            return;
        }
        try
        {
            document.DeleteRecovery(draft.Id);
        }
        catch (Exception error) when (ExpectedFileError(error))
        {
            View.SetValidationMessage(Display(error.Message));
            report(Display(error.Message));
            return;
        }
        Refresh();
    }

    private void RefreshDetails()
    {
        var draft = Selected;
        Choices.Enabled = drafts.Count > 0;
        Layout.ConfirmDelete.Enabled = draft is not null;
        Layout.Delete.Enabled = draft is not null && deleteConfirmed;
        View.Primary.Enabled = draft is { Error: null } && !document.IsDirty;
        View.SetValidationMessage(document.IsDirty
            ? "Save or discard current edits before recovering a draft."
            : draft?.Error is { } error ? Display(error) : "");
        Layout.Details.Text = draft is null ? "No recovery drafts." : Display(
            $"Original: {draft.OriginalPath ?? "Untitled document"}\nDraft: {draft.SourcePath}\nUpdated: {draft.UpdatedAt.ToLocalTime():g}");
        Layout.SourcePreview.Text = draft is null ? "" : Display(draft.Preview);
    }

    private static string Display(string value) => Encoding.UTF8.GetString(
        Encoding.UTF8.GetBytes(value.Replace('\0', '\uFFFD')));

    private static bool ExpectedFileError(Exception error) => error is IOException or InvalidDataException or
        UnauthorizedAccessException or ArgumentException or NotSupportedException or System.Security.SecurityException or
        System.Text.Json.JsonException or InvalidOperationException;
}

using System.Globalization;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class CustomizationSettingRow
{
    private readonly Action<string> apply;
    private readonly Label error;
    private readonly Label? rangeValue;
    private readonly Stack editors;
    private readonly Stack resetSlot;
    private bool updating;
    private string? saved;
    private string defaultValue = "";
    private int positionLimit;
    private float fontSize;

    internal CustomizationSettingRow(Window window, CustomizationSetting setting, Action<string> apply, Action reset,
        ButtonIcon icon = ButtonIcon.None)
    {
        Setting = setting;
        this.apply = apply;
        var row = window.Stack(Axis.Horizontal).Padding(10).Spacing(16);
        if (icon != ButtonIcon.None)
        {
            Icon = window.InfoBadge(setting.Name + " icon").SetIcon(icon).FixedSize(24, 32);
            Icon.SetControlStyle(ExplorerStyles.CommandIcon);
            row.Add(Icon);
        }
        var description = window.Stack().Spacing(3);
        description.Add(window.Label(setting.DisplayName));
        if (setting.Kind != SettingKind.Position && !setting.Id.StartsWith("key:", StringComparison.Ordinal))
        {
            var help = window.Label(setting.Help);
            help.SetPresentationFontSize(12);
            description.Add(help);
        }
        error = window.Label("").SetAutomationId($"setting-{setting.Id}-error").Visible(false);
        error.SetPresentationFontSize(12);
        description.Add(error);
        row.Add(description, flex: 1);
        editors = window.Stack(Axis.Horizontal).Spacing(8).FixedSize(364, 40);
        var field = window.Stack(Axis.Horizontal).Spacing(8);
        editors.Add(field, flex: 1);
        switch (setting.Kind)
        {
            case SettingKind.Text:
                Text = window.TextInput(setting.Name).SetCaptionVisible(false)
                    .SetPlaceholder(setting.Id.StartsWith("key:", StringComparison.Ordinal) ? "No shortcut" : setting.Name);
                Text.SetAutomationId($"setting-{setting.Id}-value").Help(setting.Help).PreferredSize(210, 36);
                Text.Submitted += CommitText;
                Text.Changed += _ => UpdateResetVisibility();
                field.Add(Text, flex: 1);
                Save = window.Button("Save").FixedSize(52, 36).SetAutomationId($"setting-{setting.Id}-save");
                Save.Click += CommitText;
                field.Add(Save);
                break;
            case SettingKind.Toggle:
                field.Add(window.Stack(), flex: 1);
                Toggle = window.ToggleSwitch(setting.Name).FixedSize(48, 36)
                    .SetAutomationId($"setting-{setting.Id}-value").Help(setting.Help);
                Toggle.Changed += value => Change(value.ToString());
                field.Add(Toggle);
                break;
            case SettingKind.Number:
                field.Add(window.Stack(), flex: 1);
                Number = window.NumericInput(setting.Name).SetRange(new(setting.Minimum, setting.Maximum))
                    .FixedSize(200, 36).SetAutomationId($"setting-{setting.Id}-value").Help(setting.Help);
                Number.OnChange(value => Change(value.ToString(CultureInfo.InvariantCulture)));
                field.Add(Number);
                break;
            case SettingKind.Slider:
                Slider = window.RangeInput(setting.Name).SetRange(new(setting.Minimum, setting.Maximum, 1, 4))
                    .SetAutomationId($"setting-{setting.Id}-value").Help(setting.Help).PreferredSize(200, 36);
                rangeValue = window.Label("").FixedSize(44, 36);
                Slider.OnChange(value => Change(value.ToString(CultureInfo.InvariantCulture)));
                field.Add(Slider, flex: 1).Add(rangeValue);
                break;
            case SettingKind.Choice:
                Choice = window.ComboBox(setting.Name).SetAutomationId($"setting-{setting.Id}-value")
                    .Help(setting.Help).PreferredSize(260, 36);
                Choice.Event += e =>
                {
                    if (e.Kind == EventKind.Selection && e.Value > 0 && e.Value <= (ulong)setting.Choices!.Length)
                        Change(setting.Choices[(int)e.Value - 1]);
                };
                field.Add(Choice, flex: 1);
                break;
            case SettingKind.Position:
                field.Add(window.Stack(), flex: 1);
                Number = window.NumericInput(setting.Name + " position").SetRange(new(1, 256))
                    .FixedSize(164, 36).SetAutomationId($"setting-{setting.Id}-position").Help("Position among visible entries.");
                Number.OnChange(value => Change(value.ToString(CultureInfo.InvariantCulture)));
                Toggle = window.ToggleSwitch("Show " + setting.Name).FixedSize(48, 36)
                    .SetAutomationId($"setting-{setting.Id}-visible").Help("Show or hide this entry.");
                Toggle.Changed += value => Change(value ? positionLimit.ToString(CultureInfo.InvariantCulture) : "0");
                field.Add(Number).Add(Toggle);
                break;
        }
        Reset = window.Button("Reset " + setting.Name + " to default").SetText("").SetIcon(ButtonIcon.Undo)
            .SetStyle(ExplorerStyles.IconButton).FixedSize(36, 36).SetAutomationId($"setting-{setting.Id}-reset")
            .Help("Restore the default for " + setting.Name).Visible(false);
        Reset.SetPresentationFontSize(12);
        Save?.SetPresentationFontSize(12);
        // The row supplies the label; retain the native accessible name without a duplicate header.
        Number?.SetControlStyleValues(StylePart.Header, new() { HeaderHeight = 0 });
        Choice?.SetControlStyleValues(StylePart.Header, new() { HeaderHeight = 0 });
        Toggle?.SetControlStyleValues(StylePart.Root, new() { Padding = new(0, 0, 0, 0) });
        Reset.Click += reset;
        resetSlot = window.Stack().FixedSize(36, 40).Add(Reset);
        editors.Add(resetSlot);
        row.Add(editors);
        Root = window.Reveal(row, setting.Name).SetDuration(0).SetLayout(RevealLayout.Expand);
        Root.SetAutomationId($"setting-{setting.Id}").SetOpen(true);
    }

    internal CustomizationSetting Setting { get; }
    internal Reveal Root { get; }
    internal TextInput? Text { get; }
    internal ToggleSwitch? Toggle { get; }
    internal NumericInput? Number { get; }
    internal RangeInput? Slider { get; }
    internal ComboBox? Choice { get; }
    internal Button? Save { get; }
    internal Button Reset { get; }
    internal InfoBadge? Icon { get; }
    internal bool ResetVisible { get; private set; }
    internal string Error => error.Text;

    private void Change(string value)
    {
        if (!updating) apply(value);
    }
    private void CommitText()
    {
        if (Text is not null) Change(Text.Text);
    }

    internal void SetError(string message)
    {
        error.SetText(message).Visible(message.Length != 0);
        UpdateResetVisibility();
    }

    private void UpdateResetVisibility()
    {
        bool visible = saved is not null && (saved != defaultValue ||
            (Text is not null && Text.Text.Trim() != defaultValue));
        if (visible == ResetVisible) return;
        ResetVisible = visible;
        if (!visible && Reset.Focused)
            ((Control?)Text ?? (Control?)Toggle ?? (Control?)Number ?? (Control?)Slider ?? Choice)?.Focus();
        Reset.Visible(visible);
    }

    internal void Synchronize(ExplorerCustomization options, bool discardDraft = false)
    {
        string value = Setting.Get(options);
        updating = true;
        try
        {
            defaultValue = Setting.DefaultValue(options);
            if (fontSize != options.FontSize)
            {
                fontSize = options.FontSize;
                float height = Math.Max(40, fontSize + 18);
                editors.FixedSize(364, height);
                Text?.PreferredSize(210, height);
                Save?.FixedSize(52, height);
                resetSlot.FixedSize(36, height);
                Reset.FixedSize(36, height);
                Number?.FixedSize(Setting.Kind == SettingKind.Position ? 164 : 200, height);
                Slider?.PreferredSize(200, height);
                Choice?.PreferredSize(260, height);
                Toggle?.FixedSize(48, height);
                rangeValue?.FixedSize(44, height);
            }
            if (Setting.Kind == SettingKind.Position)
            {
                int limit = Setting.PositionLimit!(options);
                if (limit != positionLimit)
                {
                    positionLimit = limit;
                    Number!.SetRange(new(0, Math.Max(1, limit)));
                }
            }
            if (!discardDraft && saved == value) { UpdateResetVisibility(); return; }
            saved = value;
            switch (Setting.Kind)
            {
                case SettingKind.Text: Text!.Text = value; break;
                case SettingKind.Toggle: Toggle!.SetChecked(bool.Parse(value)); break;
                case SettingKind.Number: Number!.SetValue(double.Parse(value, CultureInfo.InvariantCulture)); break;
                case SettingKind.Slider:
                    Slider!.SetValue(double.Parse(value, CultureInfo.InvariantCulture));
                    rangeValue!.Text = value;
                    break;
                case SettingKind.Choice:
                    Choice!.SetItems(Setting.Choices!.Select((choice, index) =>
                        new Choice((ulong)index + 1, CultureInfo.CurrentCulture.TextInfo.ToTitleCase(choice))).ToArray(),
                        (ulong)Array.IndexOf(Setting.Choices!, value) + 1);
                    break;
                case SettingKind.Position:
                    int position = int.Parse(value, CultureInfo.InvariantCulture);
                    Number!.SetEnabled(position > 0).SetValue(position);
                    Toggle!.SetChecked(position > 0);
                    break;
            }
            UpdateResetVisibility();
        }
        finally { updating = false; }
    }
}

using System.Runtime.InteropServices;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeTypographyScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Owned typography fixtures", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        P.Control[] models;
        P.TextInput input;
        using (var build = host.BeginBuild())
        {
            input = host.TextInput("Distinct native caption");
            input.Text = "Native typography draft";
            input.SetHeight(P.AxisConstraints.Auto);
            models = [host.Label("Typography label"), host.Button("Typography button"),
                input, host.Toggle("Typography toggle"), host.CheckBox("Typography check")];
            var root = host.Stack(P.Axis.Vertical).Spacing(10);
            for (int i = 0; i < models.Length; i++)
            {
                models[i].AutomationId = $"typography-{i}";
                root.Add(models[i]);
            }
            host.SetContent(root);
            build.Complete();
        }
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        var native = models.Select((_, index) => backend.FindControls($"typography-{index}").Single()).ToArray();
        var original = new Dictionary<(int, StylePart), PartStyleValues>();
        for (int i = 0; i < native.Length; i++)
        {
            StylePart[] parts = i == 2 ? [StylePart.Text, StylePart.Header] : [StylePart.Label];
            foreach (var part in parts)
            {
                var local = new PartStyleValues
                {
                    FontFamily = "Segoe UI",
                    FontSize = part == StylePart.Header ? 12 : 16,
                    FontWeight = part == StylePart.Header ? 700u : 400u,
                    Foreground = new ThemeColor(0x112233, 0xeeddcc)
                };
                native[i].SetControlStyleValues(part, local);
                original.Add((i, part), native[i].GetControlStyleValues(part));
            }
        }
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                int changes = 0;
                nint editor = 0;
                nint caption = 0;
                NativeFont originalFont = default;
                NativeFont originalCaptionFont = default;
                float labelHeight = 0;
                float[] originalHeights = [];
                ui.Ui(() =>
                {
                    input.Changed += _ => changes++;
                    Check(host.TryFocus(input), "The typography input did not receive native focus.");
                    host.SetSelection(input, new(2, 7));
                    editor = GetFocus();
                    caption = FindTypographyCaption(GetAncestor(editor, 2));
                    originalFont = ReadNativeFont(editor);
                    originalCaptionFont = ReadNativeFont(caption);
                    labelHeight = native[0].GetBounds().Height;
                    originalHeights = native.Select(control => control.GetBounds().Height).ToArray();
                });
                foreach (float size in new[] { 8f, 28f, 32f })
                {
                    foreach (uint weight in new uint[] { 400, 700 })
                    {
                        ui.Ui(() =>
                        {
                            foreach (var model in models) model.Typography = new P.Typography(fontSize: size, fontWeight: weight);
                        });
                        ui.Wait(() => ReadNativeFont(editor).Weight == weight &&
                            Math.Abs(Math.Abs(ReadNativeFont(editor).Height) - size * GetDpiForWindow(editor) / 96f) <= 1 &&
                            ReadNativeFont(caption).Weight == weight &&
                            Math.Abs(Math.Abs(ReadNativeFont(caption).Height) - size * GetDpiForWindow(caption) / 96f) <= 1);
                        ui.Ui(() =>
                        {
                            foreach (var ((index, part), local) in original)
                            {
                                var actual = native[index].GetControlStyleValues(part);
                                Check(actual == local with { FontSize = size, FontWeight = weight },
                                    "Typography overwrote unrelated native style values or missed a supported text target.");
                            }
                            Check(ReadNativeFont(editor).FaceName == originalFont.FaceName,
                                "Typography replaced an independently inherited native font family.");
                            Check(GetFocus() == editor && host.GetSelection(input) == new P.TextSelection(2, 7) &&
                                input.Text == "Native typography draft" && changes == 0,
                                "Typography changed the native editor identity, text, selection, or user event count.");
                            if (size == 28)
                            {
                                Check(native[0].GetBounds().Height > labelHeight,
                                    "DirectWrite label measurement did not adopt the larger native font.");
                                foreach (int index in new[] { 1, 3, 4 })
                                    Check(native[index].GetBounds().Height > originalHeights[index],
                                        $"The native {native[index].GetType().Name} layout did not adopt the larger text metrics.");
                                Check(GetWindowRect(editor, out var bounds) &&
                                    bounds.Bottom - bounds.Top >= Math.Abs(ReadNativeFont(editor).Height),
                                    "The larger native font was clipped by the editor allocation.");
                                Check(GetWindowRect(caption, out var headerBounds) &&
                                    headerBounds.Bottom - headerBounds.Top >= Math.Abs(ReadNativeFont(caption).Height),
                                    "The larger native font was clipped by the caption allocation.");
                            }
                        });
                    }
                }
                ui.Ui(() =>
                {
                    foreach (var model in models) model.Typography = null;
                });
                ui.Wait(() => ReadNativeFont(editor).Weight == originalFont.Weight &&
                    ReadNativeFont(editor).Height == originalFont.Height &&
                    ReadNativeFont(caption).Weight == originalCaptionFont.Weight &&
                    ReadNativeFont(caption).Height == originalCaptionFont.Height);
                ui.Ui(() =>
                {
                    Check(ReadNativeFont(editor).FaceName == originalFont.FaceName &&
                        ReadNativeFont(caption).FaceName == originalCaptionFont.FaceName,
                        "Clearing typography did not restore the native editor and caption font families.");
                    foreach (var ((index, part), local) in original)
                        Check(native[index].GetControlStyleValues(part) == local,
                            "Clearing typography did not restore each native part's exact original local record.");
                    var changedBaseline = original[(2, StylePart.Header)] with { FontSize = 18, FontWeight = 400 };
                    native[2].SetControlStyleValues(StylePart.Header, changedBaseline);
                    input.Typography = new P.Typography(fontSize: 24, fontWeight: 700);
                    input.Typography = null;
                    Check(native[2].GetControlStyleValues(StylePart.Header) == changedBaseline,
                        "Reapplying typography reused an obsolete local baseline.");
                    input.Typography = new P.Typography(fontSize: 14);
                });
                ui.Wait(() => Math.Abs(ReadNativeFont(editor).Height + 14 * GetDpiForWindow(editor) / 96f) <= 1);
                ui.Ui(() =>
                {
                    var before = ReadNativeFont(editor);
                    SendMessage(editor, 0x010d, 0, 0);
                    input.Typography = new P.Typography(fontSize: 28, fontWeight: 700);
                    Check(ReadNativeFont(editor).Height == before.Height && GetFocus() == editor,
                        "Typography rewrote the native font or focus during active composition.");
                    SendMessage(editor, 0x010e, 0, 0);
                });
                ui.Wait(() => ReadNativeFont(editor).Weight == 700 &&
                    Math.Abs(Math.Abs(ReadNativeFont(editor).Height) - 28 * GetDpiForWindow(editor) / 96f) <= 1);
                ui.Ui(() =>
                {
                    Check(host.GetSelection(input) == new P.TextSelection(2, 7) && changes == 0,
                        "Composition-time typography changed native selection or text.");
                    SendMessage(editor, 0x010d, 0, 0);
                    input.Typography = new P.Typography(fontSize: 32, fontWeight: 700);
                    input.Typography = null;
                    Check(ReadNativeFont(editor).Weight == 700 &&
                        Math.Abs(Math.Abs(ReadNativeFont(editor).Height) - 28 * GetDpiForWindow(editor) / 96f) <= 1,
                        "Clearing typography replaced the active composition font before composition ended.");
                    SendMessage(editor, 0x010e, 0, 0);
                });
                ui.Wait(() => ReadNativeFont(editor).Weight == originalFont.Weight &&
                    ReadNativeFont(editor).Height == originalFont.Height);
                nint replacementEditor = 0;
                ui.Ui(() =>
                {
                    Check(ReadNativeFont(editor).FaceName == originalFont.FaceName,
                        "Composition-time typography reset changed the native font family.");
                    Check(native[2].GetControlStyleValues(StylePart.Text) == original[(2, StylePart.Text)] &&
                        native[2].GetControlStyleValues(StylePart.Header) == original[(2, StylePart.Header)] with { FontSize = 18, FontWeight = 400 },
                        "Clearing typography during composition lost the independently captured native records.");
                    Throws<NotSupportedException>(() => host.Theme = P.ThemeSettings.System);
                    Check(host.Theme is null && host.IsAttached, "A borrowed surface accepted window-wide theme authority.");
                    SendMessage(editor, 0x010d, 0, 0);
                    input.Typography = new P.Typography(fontSize: 24, fontWeight: 700);
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(editor), "Typography teardown during composition leaked native content.");
                    Throws<XuiException>(() => native[2].GetControlStyleValues(StylePart.Text));
                    backend = new WindowsBackend(surface, dispatcher);
                    host.Attach(backend);
                    Check(host.TryFocus(input), "A reattached input could not restore its authored typography.");
                    replacementEditor = GetFocus();
                });
                ui.Wait(() => ReadNativeFont(replacementEditor).Weight == 700 &&
                    Math.Abs(Math.Abs(ReadNativeFont(replacementEditor).Height) - 24 * GetDpiForWindow(replacementEditor) / 96f) <= 1);
                ui.Ui(() =>
                {
                    Check(input.Text == "Native typography draft" && !IsWindow(editor) && IsWindow(replacementEditor),
                        "Typography reattachment revived a stale native editor or lost retained text.");
                    host.Detach();
                    Check(HandleCount(window) == baseline, "Reattached typography leaked native resources.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }

    private static nint FindTypographyCaption(nint window)
    {
        nint result = 0;
        EnumChildWindows(window, (child, _) =>
        {
            var name = new System.Text.StringBuilder(128);
            var text = new System.Text.StringBuilder(128);
            GetClassName(child, name, name.Capacity);
            GetTypographyWindowText(child, text, text.Capacity);
            if (name.ToString().Equals("Static", StringComparison.OrdinalIgnoreCase) &&
                text.ToString() == "Distinct native caption") result = child;
            return true;
        }, 0);
        if (result == 0) throw new InvalidOperationException("The native typography caption was not found.");
        return result;
    }

    private static NativeFont ReadNativeFont(nint window)
    {
        nint font = SendMessage(window, 0x0031, 0, 0);
        if (font == 0 || GetFontObject(font, Marshal.SizeOf<NativeFont>(), out var value) == 0)
            throw new InvalidOperationException("The native editor did not expose its actual font.");
        return value;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private struct NativeFont
    {
        public int Height, Width, Escapement, Orientation, Weight;
        public byte Italic, Underline, StrikeOut, CharSet, OutPrecision, ClipPrecision, Quality, PitchAndFamily;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string FaceName;
    }
    [DllImport("gdi32.dll", EntryPoint = "GetObjectW")]
    private static extern int GetFontObject(nint value, int size, out NativeFont font);
    [DllImport("user32.dll", EntryPoint = "GetWindowTextW", CharSet = CharSet.Unicode)]
    private static extern int GetTypographyWindowText(nint window, System.Text.StringBuilder text, int capacity);
}

using System.Runtime.InteropServices;
using System.Text;

namespace Xui.Designer;

internal sealed partial class DesignerApplication
{
    private async Task SelectionSmoke()
    {
        const string fixture = """
            component SelectionFixture {
                state string Caption = "Do not execute";
                view {
                    VStack(spacing: 8) {
                        Text("Find target");
                        Button(Caption, size: (180, 40), enabled: true, click: Activate);
                        TextInput("Preview input", text: "Preview text");
                    }
                }
                code csharp {
                    private void Activate() {
                        Caption = "Activated";
                    }
                }
            }
            """;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(180));
        int assertions = 0;
        string lastState = "Waiting for the first UI action.";
        try
        {
            var compiled = PreviewCompiler.Compile(fixture, timeout.Token);
            if (!compiled.Success) throw new InvalidOperationException("Selection fixture compilation failed: " + compiled.Diagnostics);
            await Ready();
            await Ui(() => editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, fixture));
            await Ready();
            string source = "";
            int buttonId = -1;
            await Ui(() =>
            {
                source = editor.Text;
                buttonId = workspace.Document!.Root!.Children[1].Id;
                sourceSearch.HandleKey(new('F', KeyModifiers.Control, 0));
                sourceSearch.Layout.Query.Text = "Find target";
                sourceSearch.Refresh();
                editor.Selection = new(0, 0);
                sourceSearch.Layout.Next.Invoke();
                Require(editor.Selection.Start == (ulong)source.IndexOf("Find target", StringComparison.Ordinal) &&
                    workspace.Hierarchy.Selection?.Kind == "Text" && editor.Focused,
                    "The integrated Find toolbar selects exact source and its hierarchy control.");
                Require(editor.GetBounds().Height >= 150 && sourceSearch.Layout.Query.GetBounds().Width >= 80,
                    "The full application keeps usable source and query geometry.");
                view.Pick.Invoke();
            });
            await Until(() => pickControls);
            await Ui(() =>
            {
                Require(view.PickStatus.Contains("Keyboard and accessibility", StringComparison.Ordinal),
                    "Pick mode reports its pointer-only contract.");
                SelectionNative.Click("Do not execute");
            });
            await Until(() => workspace.Hierarchy.Selection?.Id == buttonId);
            await Until(() => view.OutlineStatus.StartsWith("Outline: Button.", StringComparison.Ordinal));
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[1];
                Require(editor.Selection == new TextSelection((ulong)button.Span.Start, (ulong)button.Span.End) && editor.Focused,
                    "A real native preview click selects the complete authored control and focuses source.");
                Require(editor.Text == source && preview.AppliedVersion == version &&
                    ButtonText() == "Do not execute",
                    "Picking consumes the authored pointer action without changing source or retiring preview.");
                var selection = editor.Selection;
                Require(!workspace.SelectFromPreview(source, int.MaxValue) && editor.Selection == selection,
                    "An unknown preview node cannot move source selection.");
                Require(!workspace.SelectFromPreview(source + "\r", buttonId) && editor.Selection == selection,
                    "A mismatched preview source cannot reuse current hierarchy IDs.");
                long staleVersion = version;
                editor.ReplaceRange(new(0, 0), source, "// Shift\r");
                Require(view.OutlineStatus.StartsWith("Outline cleared.", StringComparison.Ordinal),
                    "A source revision invalidates the displayed outline feedback immediately.");
                selection = editor.Selection;
                OnPreviewPicked(new(staleVersion, buttonId));
                Require(editor.Selection == selection, "An old preview version cannot select from a newer source revision.");
            });
            await Ready();
            await Ui(() =>
            {
                source = editor.Text;
                editor.Selection = new(0, 0);
                SelectionNative.Click("Do not execute");
            });
            await Until(() => editor.Selection.Start == (ulong)workspace.Document!.Root!.Children[1].Span.Start);
            await Ui(() =>
            {
                Require(pickControls && workspace.Hierarchy.Selection?.Kind == "Button",
                    "Pick mode continues across a successful native preview replacement.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() =>
            {
                Require(!editor.Text.StartsWith("// Shift", StringComparison.Ordinal),
                    "Preview picking preserves the preceding native source undo operation.");
                view.Pick.Invoke();
            });
            await Until(() => !pickControls);
            await Ui(() => SelectionNative.Click("Do not execute"));
            await Until(() => ButtonText() == "Activated");
            long styledVersion = 0;
            await Ui(() =>
            {
                styledVersion = version;
                view.StyleToggle.Invoke();
            });
            await Until(() => window.Style == VisualStyle.Classic);
            await Ui(() =>
            {
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Classic style preserves the applied preview and its authored state without recompilation.");
                Require(workspace.Hierarchy.Tree.GetControlStyleValues(StylePart.Root, effective: true).RowHeight == 28 &&
                    workspace.Hierarchy.Tree.GetControlStyleValues(StylePart.Row, effective: true).Padding == new Insets(4, 2, 4, 2),
                    "Classic style retains the compact hierarchy rows.");
                view.StyleToggle.Invoke();
            });
            await Until(() => window.Style == VisualStyle.WinUI);
            await Ui(() => Require(version == styledVersion && ButtonText() == "Activated",
                "Switching back to WinUI preserves the same preview state."));
            await Ui(() => viewport.SelectPreset(DesignerViewportPreset.Compact));
            await Until(() => viewport.Preset == DesignerViewportPreset.Compact);
            await Ui(() =>
            {
                viewport.RefreshDimensions();
                var bounds = preview.View.GetBounds();
                Require(bounds.Width == 360 && bounds.Height == 640,
                    "Compact preview applies its exact dimensions in the complete Designer shell.");
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Viewport resizing preserves the preview version and authored control state without recompilation.");
                view.Pick.Invoke();
            });
            await Until(() => pickControls);
            await Ui(() => SelectionNative.Click("Do not execute"));
            await Until(() => workspace.Hierarchy.Selection?.Id == buttonId);
            await Ui(() =>
            {
                Require(ButtonText() == "Activated" && editor.Selection.Start ==
                    (ulong)workspace.Document!.Root!.Children[1].Span.Start,
                    "Native pointer picking remains source-aligned inside a fixed-size viewport.");
                viewport.SelectPreset(DesignerViewportPreset.Fit);
            });
            await Until(() => viewport.Preset == DesignerViewportPreset.Fit);
            await Ui(() =>
            {
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Returning to Fit retains the same live preview and authored state.");
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen && !pickControls);
            await Ui(() =>
            {
                Require(commandPalette.Surface.Editor.Focused && version == styledVersion && ButtonText() == "Activated",
                    "The Commands button leaves pointer picking before opening the native palette without resetting preview state.");
                commandPalette.Surface.Invoke((ulong)DesignerCommandId.Replace);
            });
            await Until(() => !commandPalette.IsOpen && sourceSearch.Layout.ReplaceOpen && sourceSearch.Layout.Replacement.Focused);
            await Ui(() =>
            {
                Require(window.KeyHandler!(new('P', KeyModifiers.Control | KeyModifiers.Shift, editor.Id)),
                    "Ctrl+Shift+P opens Designer command discovery.");
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                Require(!window.KeyHandler!(new('S', KeyModifiers.Control, commandPalette.Surface.Editor.Id)),
                    "Source and file shortcuts do not intercept keys inside the command palette.");
                SelectionNative.Key(0x1B);
            });
            await Until(() => !commandPalette.IsOpen);
            await Ui(() => Require(sourceSearch.Layout.FindOpen && sourceSearch.Layout.ReplaceOpen,
                "Escape dismisses the command palette without closing the underlying Find panel."));
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusHierarchySearch));
            await Until(() => !commandPalette.IsOpen && workspace.Hierarchy.Layout.Query.Focused);
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[1];
                var key = workspace.Hierarchy.Key(button);
                workspace.Hierarchy.Layout.Query.Text = "button Caption";
                workspace.Hierarchy.RefreshSearch();
                workspace.Hierarchy.Layout.NextMatch.Invoke();
                Require(workspace.Hierarchy.Selection?.Id == buttonId &&
                    editor.Selection == new TextSelection((ulong)button.Span.Start, (ulong)button.Span.End) &&
                    workspace.Hierarchy.Tree.Selection.Focused == key,
                    "Hierarchy search selects exact source and the existing native tree row through the command palette.");
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Hierarchy search preserves the live preview version and authored state.");
                Require(window.KeyHandler!(new(0x1B, KeyModifiers.None, workspace.Hierarchy.Layout.Query.Id)) &&
                    workspace.Hierarchy.Layout.Query.Text == "" && sourceSearch.Layout.FindOpen && sourceSearch.Layout.ReplaceOpen,
                    "Escape in hierarchy search clears only that query instead of closing source Find.");
            });
            await Ui(() => view.Pick.Invoke());
            await Until(() => pickControls);
            await Ui(view.GoToLine.Invoke);
            await Until(() => sourceGoTo.IsPending && !pickControls);
            int destination = 0;
            await Ui(() =>
            {
                destination = editor.Text.IndexOf("Find target", StringComparison.Ordinal) + 3;
                var line = Microsoft.CodeAnalysis.Text.SourceText.From(editor.Text).Lines.GetLineFromPosition(destination);
                sourceGoTo.Layout.Line.Text = (line.LineNumber + 1).ToString(System.Globalization.CultureInfo.InvariantCulture);
                sourceGoTo.Layout.Column.Text = (destination - line.Start + 1).ToString(System.Globalization.CultureInfo.InvariantCulture);
                sourceGoTo.Refresh();
                sourceGoTo.View.Primary.Invoke();
            });
            await Until(() => !sourceGoTo.IsPending && editor.Focused);
            await Ui(() =>
            {
                Require(editor.Selection == new TextSelection((ulong)destination, (ulong)destination) &&
                    workspace.Hierarchy.Selection?.Kind == "Text",
                    "Go to line leaves pointer picking and synchronizes the hierarchy without expanding the exact caret.");
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Source navigation preserves the compiled preview and authored control state.");
                view.Path.Focus();
                Require(!window.KeyHandler!(new('G', KeyModifiers.Control, view.Path.Id)),
                    "Ctrl+G in a different text field does not open source navigation.");
                editor.Focus();
                Require(window.KeyHandler!(new('G', KeyModifiers.Control, editor.Id)),
                    "Ctrl+G in source opens the location dialog.");
            });
            await Until(() => sourceGoTo.IsPending);
            await Ui(() => sourceGoTo.View.CancelButton.Invoke());
            await Until(() => !sourceGoTo.IsPending);
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.GoToLine));
            await Until(() => sourceGoTo.IsPending);
            await Ui(() =>
            {
                Require(!commandPalette.IsOpen && sourceGoTo.Layout.Line.Focused,
                    "The Go to command opens its native dialog after the command palette closes.");
                sourceGoTo.View.CancelButton.Invoke();
            });
            await Until(() => !sourceGoTo.IsPending);
            await Ui(() =>
            {
                source = editor.Text;
                sourceSearch.HandleKey(new('H', KeyModifiers.Control, 0));
                sourceSearch.Layout.Query.Text = "Find target";
                sourceSearch.Layout.Replacement.Text = "Updated target";
                sourceSearch.Layout.ReplaceAll.Invoke();
                Require(editor.Text.Contains("Updated target", StringComparison.Ordinal) && version > styledVersion,
                    "Integrated replacement updates source and supersedes the preview through the normal edit pipeline.");
            });
            await Ready();
            await Ui(() =>
            {
                Require(preview.AppliedVersion == version && workspace.Document!.Source == editor.Text,
                    "Replacement publishes a matching hierarchy and compiled preview.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() =>
            {
                Require(editor.Text == source && preview.AppliedVersion == version,
                    "One native undo restores the pre-replacement source and its preview.");
                sourceSearch.Layout.Close.Invoke();
            });
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.New));
            await Until(() => fileActions.Discard.IsPending);
            await Ui(() =>
            {
                Require(!commandPalette.IsOpen && editor.Text == source,
                    "New from the command palette keeps dirty source behind the existing discard confirmation.");
                fileActions.Discard.View.CancelButton.Invoke();
            });
            await Until(() => !fileActions.Discard.IsPending);
            await Ui(() => Require(editor.Text == source, "Canceling a command-palette file action preserves the complete source."));
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusPalette));
            await Until(() => !commandPalette.IsOpen);
            await Ui(() =>
            {
                var field = workspace.Inspector.Layout.PaletteFilter.GetBounds();
                var panel = view.InspectorPanel.GetBounds();
                Require(workspace.Inspector.Layout.PaletteFilter.Focused &&
                    field.Y >= panel.Y && field.Y + field.Height <= panel.Y + panel.Height,
                    "The control-palette focus command reveals its native field inside the inspector.");
            });
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[1];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(button));
                workspace.Inspector.ChooseArgument("size");
                workspace.Inspector.Layout.DimensionMode.Invoke();
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusProperty));
            await Until(() => !commandPalette.IsOpen && workspace.Inspector.Layout.DimensionWidth.Focused);
            await Ui(() =>
            {
                Require(workspace.Inspector.IsDimensionMode,
                    "Property navigation preserves the active dimension mode and focuses its width field.");
                workspace.Inspector.Layout.DimensionWidth.Text = "240";
                workspace.Inspector.Layout.DimensionHeight.Text = "52";
                workspace.Inspector.Layout.Apply.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                Require(preview.TryReadNode(version, buttonId, out var resized) &&
                    resized.Bounds.Width == 240 && resized.Bounds.Height == 52,
                    "Structured dimensions change the actual native preview control through the source compiler.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && preview.TryReadNode(version, buttonId, out var restored) &&
                restored.Bounds.Width == 180 && restored.Bounds.Height == 40,
                "One source undo restores the original native preview dimensions."));
            await Ui(() =>
            {
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(workspace.Document!.Root!.Children[1]));
                workspace.Inspector.ChooseArgument("enabled");
                workspace.Inspector.Layout.BooleanMode.Invoke();
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusProperty));
            await Until(() => !commandPalette.IsOpen && workspace.Inspector.Layout.BooleanValue.Focused);
            await Ui(() =>
            {
                Require(workspace.Inspector.IsBooleanMode && SelectionNative.Enabled("Do not execute"),
                    "Property navigation focuses the active boolean toggle while the authored preview remains enabled.");
                workspace.Inspector.Layout.BooleanValue.Invoke();
                Require(editor.Text == source && SelectionNative.Enabled("Do not execute"),
                    "A boolean draft does not change source or the current native preview.");
                workspace.Inspector.Layout.Apply.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                Require(!SelectionNative.Enabled("Do not execute") && editor.Text.Contains("enabled: false", StringComparison.Ordinal),
                    "Applying a boolean property disables the actual native preview button through source compilation.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && SelectionNative.Enabled("Do not execute"),
                "One native source undo restores the enabled preview button."));
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[1];
                editor.Selection = new((ulong)button.Span.Start, (ulong)button.Span.End);
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                Require(!window.KeyHandler!(new(0xBF, KeyModifiers.Control, commandPalette.Surface.Editor.Id)),
                    "The source comment shortcut does not intercept command palette input.");
                commandPalette.Surface.Invoke((ulong)DesignerCommandId.ToggleComment);
            });
            await Ready();
            await Ui(() =>
            {
                Require(!commandPalette.IsOpen && editor.Focused && editor.Text.Contains("// Button(Caption", StringComparison.Ordinal) &&
                    workspace.Document!.Root!.Children.Count == 2 && SelectionNative.PeerCount("Do not execute") == 0,
                    "The palette comment action changes source, hierarchy, and the actual native preview through the normal compiler pipeline.");
                Require(window.KeyHandler!(new(0xBF, KeyModifiers.Control, editor.Id)),
                    "Ctrl+/ toggles comments in the focused source editor.");
            });
            await Ready();
            await Ui(() =>
            {
                Require(editor.Text == source && workspace.Document!.Root!.Children.Count == 3 &&
                    SelectionNative.PeerCount("Do not execute") == 1,
                    "The source shortcut restores the selected control and its live native preview.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() =>
            {
                Require(editor.Text.Contains("// Button(Caption", StringComparison.Ordinal) && SelectionNative.PeerCount("Do not execute") == 0,
                    "One source undo restores the previous commented document and preview.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && SelectionNative.PeerCount("Do not execute") == 1,
                "The preceding palette comment has its own native undo operation."));
            await Ui(() =>
            {
                Require(!pickControls, "Disabling Pick controls restores actual authored pointer behavior.");
                view.Live.Invoke();
                view.Pick.Invoke();
            });
            await Until(() => view.PickStatus.StartsWith("Render the current source", StringComparison.Ordinal));
            await Ui(() =>
            {
                Require(!pickControls && preview.AppliedVersion != version,
                    "A non-current preview reports a refusal without partially enabling picking.");
                Require(view.OutlineStatus.StartsWith("Outline cleared.", StringComparison.Ordinal),
                    "A paused source revision does not retain current-outline feedback.");
                Require(editor.Text.Contains("SelectionFixture", StringComparison.Ordinal), "A picking refusal preserves source.");
            });
            Console.WriteLine($"Designer source/preview selection assertions: {assertions} passed.");

            string ButtonText()
            {
                if (!preview.TryReadNode(version, buttonId, out var node) || node.ControlId is not { } control)
                    throw new InvalidOperationException("The current preview button has no native control identity.");
                return SelectionNative.ReadText(control);
            }
        }
        catch (OperationCanceledException error) when (timeout.IsCancellationRequested)
        {
            Console.Error.WriteLine("Selection smoke deadline: " + lastState);
            throw new TimeoutException("Selection smoke deadline: " + lastState, error);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            throw;
        }
        finally { window.Post(window.Close); }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException("Designer selection smoke: " + message);
            assertions++;
        }

        async Task Ui(Action action)
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!window.Post(() =>
            {
                try { action(); done.SetResult(); }
                catch (Exception error) { done.SetException(error); }
            })) throw new InvalidOperationException("The selection smoke window rejected an action.");
            await done.Task.WaitAsync(TimeSpan.FromSeconds(30), timeout.Token);
        }

        Task Ready() => Until(() =>
        {
            if (view.Status.Text is "Error. See diagnostics." or "Source has errors. The last valid preview is unchanged.")
                throw new InvalidOperationException("Selection fixture did not render: " + diagnostics.Text);
            return workspace.IsCurrent && !workspace.IsBusy && preview.AppliedVersion == version;
        });

        async Task Until(Func<bool> condition)
        {
            bool ready = false;
            while (!ready)
            {
                await Ui(() =>
                {
                    ready = condition();
                    string state = $"version={version}, applied={preview.AppliedVersion}, hierarchyCurrent={workspace.IsCurrent}, busy={workspace.IsBusy}, picking={pickControls}, pickStatus={view.PickStatus}, " +
                        $"status={view.Status.Text}, diagnostics={diagnostics.Text}";
                    lastState = state;
                });
                if (!ready) await Task.Delay(25, timeout.Token);
            }
        }
    }

    private static class SelectionNative
    {
        internal static int PeerCount(string text) => Peers(text).Count;

        internal static bool Enabled(string text)
        {
            var peers = Peers(text);
            if (peers.Count != 1) throw new InvalidOperationException($"Expected one preview peer named '{text}', found {peers.Count}.");
            return IsWindowEnabled(peers.Single());
        }

        internal static string ReadText(ulong control)
        {
            int status = TextCopy(control, null, 0, out uint length);
            if (status is not (0 or 6)) throw new InvalidOperationException($"Native preview text length failed: {status}.");
            var bytes = new byte[length];
            status = TextCopy(control, bytes, length, out _);
            if (status != 0) throw new InvalidOperationException($"Native preview text read failed: {status}.");
            return Encoding.UTF8.GetString(bytes);
        }

        internal static void Click(string text)
        {
            var peers = Peers(text);
            if (peers.Count != 1) throw new InvalidOperationException($"Expected one preview peer named '{text}' on the owning UI thread, found {peers.Count}.");
            nint peer = peers.Single();
            if (!GetClientRect(peer, out var bounds) || bounds.Right <= 0 || bounds.Bottom <= 0)
                throw new InvalidOperationException("The preview peer has no usable client bounds.");
            nint point = ((bounds.Bottom / 2) << 16) | ((bounds.Right / 2) & 0xffff);
            SendMessageW(peer, 0x201, 1, point);
            SendMessageW(peer, 0x202, 0, point);
        }

        internal static void Key(uint key)
        {
            if (!PostMessageW(GetFocus(), 0x0100, key, 0))
                throw new InvalidOperationException("The preview smoke could not post a native key.");
        }

        private static HashSet<nint> Peers(string text)
        {
            var peers = new HashSet<nint>();
            EnumThreadWindows(GetCurrentThreadId(), (root, _) =>
            {
                EnumChildWindows(root, (child, _) =>
                {
                    var caption = new StringBuilder(128);
                    GetWindowTextW(child, caption, caption.Capacity);
                    if (caption.ToString() == text) peers.Add(child);
                    return true;
                }, 0);
                return true;
            }, 0);
            return peers;
        }

        private delegate bool EnumWindow(nint window, nint context);
        [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
        [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
        [DllImport("user32.dll")] private static extern bool EnumThreadWindows(uint thread, EnumWindow callback, nint context);
        [DllImport("user32.dll")] private static extern bool EnumChildWindows(nint parent, EnumWindow callback, nint context);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(nint window, StringBuilder text, int count);
        [DllImport("user32.dll")] private static extern bool GetClientRect(nint window, out Rect bounds);
        [DllImport("user32.dll")] private static extern bool IsWindowEnabled(nint window);
        [DllImport("user32.dll")] private static extern nint SendMessageW(nint window, uint message, nint first, nint second);
        [DllImport("user32.dll")] private static extern nint GetFocus();
        [DllImport("user32.dll")] private static extern bool PostMessageW(nint window, uint message, nuint first, nint second);
        [DllImport("xui", EntryPoint = "xui_text_copy")] private static extern int TextCopy(ulong control, [Out] byte[]? bytes, uint capacity, out uint count);
    }
}

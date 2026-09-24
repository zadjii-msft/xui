using System.Text;
using System.Text.Json;
using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private const string InterruptedStatus =
        "Storage operation interrupted; Load draft to check stored data. Current edits kept.";

    private static void SessionChecks()
    {
        SessionCodecChecks();
        IdleSessionChecks();
        InterruptedSessionChecks();
    }

    private static void SessionCodecChecks()
    {
        Assert(!JsonSerializer.IsReflectionEnabledByDefault, "Session metadata is source-generated without reflection.");
        var empty = new ProfileWorkspaceSession(1, new(), null, ProfilePage.Edit, false);
        const string literal = """{"Version":1,"Draft":{"DisplayName":"","Role":"","Location":"","Focus":""},"SavedProfile":null,"Page":0,"Interrupted":false}""";
        Assert(ProfileWorkspaceSessionCodec.Serialize(empty) == literal, "Session JSON has an independent literal version-one shape.");
        Assert(ProfileWorkspaceSessionCodec.Restore(literal) == empty, "Completely blank drafts are valid session state.");
        Throws<JsonException>(() => ProfileWorkspaceCodec.Serialize(empty.Draft));
        Throws<JsonException>(() => ProfileWorkspaceCodec.Restore(literal));
        var confirmed = new ProfileSnapshot("Previously saved", "Engineer", "London", "Native input");
        var incomplete = new ProfileSnapshot(" \t ", "  Zo\u00eb \u674e  ", "", " unfinished ");
        var session = new ProfileWorkspaceSession(1, incomplete, confirmed, ProfilePage.Edit, true);
        string json = ProfileWorkspaceSessionCodec.Serialize(session);
        Assert(ProfileWorkspaceSessionCodec.Restore(json) == session, "Session preserves invalid-for-save draft text and confirmed data exactly.");
        using (var document = JsonDocument.Parse(json))
        {
            Assert(document.RootElement.EnumerateObject().Select(property => property.Name)
                .SequenceEqual(["Version", "Draft", "SavedProfile", "Page", "Interrupted"]),
                "Checkpoint contains no controller, task, host, busy flag, navigation ID, focus, or error object.");
        }
        Assert(new ProfileWorkspaceSession(1, confirmed, null, ProfilePage.Preview, false).Page == ProfilePage.Preview,
            "Valid logical preview route is supported.");
        Throws<JsonException>(() => _ = new ProfileWorkspaceSession(1, incomplete, null, ProfilePage.Preview, false));
        Throws<JsonException>(() => _ = new ProfileWorkspaceSession(1, confirmed, incomplete, ProfilePage.Edit, false));
        Throws<JsonException>(() => _ = new ProfileWorkspaceSession(2, confirmed, null, ProfilePage.Edit, false));
        Throws<JsonException>(() => _ = new ProfileWorkspaceSession(1, confirmed, null, (ProfilePage)5, false));
        Throws<JsonException>(() => _ = new ProfileWorkspaceSession(1, null!, null, ProfilePage.Edit, false));
        Throws<ArgumentNullException>(() => ProfileWorkspaceSessionCodec.Serialize(null!));

        foreach (string invalid in new[]
        {
            "", "{", "null", "{}", "[]",
            literal.Replace("\"Version\":1", "\"Version\":2"),
            literal.Replace("\"Version\":1,", ""),
            literal.Replace("\"SavedProfile\":null,", ""),
            literal.Replace(",\"Page\":0", ""),
            literal.Replace(",\"Interrupted\":false", ""),
            literal.Replace("\"DisplayName\":\"\",", ""),
            literal.Replace("\"Page\":0", "\"Page\":1"),
            literal.Replace("\"Page\":0", "\"Page\":99"),
            literal.Replace("\"Page\":0", "\"Page\":\"Edit\""),
            literal.Replace("\"Interrupted\":false", "\"Interrupted\":\"false\""),
            literal.Replace("\"Draft\":{\"DisplayName\":\"\",\"Role\":\"\",\"Location\":\"\",\"Focus\":\"\"}", "\"Draft\":null"),
            literal.Replace("\"Version\":1", "\"Version\":1,\"Unexpected\":true"),
            literal.Replace("\"Role\":\"\"", "\"Role\":\"\",\"Secret\":\"not allowed\""),
            literal.Replace("\"SavedProfile\":null", "\"SavedProfile\":{\"DisplayName\":\"\",\"Role\":\"\",\"Location\":\"\",\"Focus\":\"\"}"),
            literal.Replace("\"Version\":1", "\"Version\":1,\"Version\":1"),
            literal.Replace("\"Focus\":\"\"", "\"Focus\":\"\",\"Focus\":\"duplicate\""),
            ProfileWorkspaceCodec.Serialize(confirmed)
        })
            Throws<JsonException>(() => ProfileWorkspaceSessionCodec.Restore(invalid));
    }

    private static void IdleSessionChecks()
    {
        var storage = new MemoryStorage();
        using var original = new Harness(storage);
        var blank = original.Controller.CaptureSession();
        Assert(blank == new ProfileWorkspaceSession(1, new(), null, ProfilePage.Edit, false),
            "Capturing a new editor is valid and does not invent saved data.");
        original.SetProfile();
        original.Controller.Save();
        original.Finish();
        var confirmed = original.Controller.State.Draft;
        byte[] savedBytes = storage.Data!.ToArray();
        original.Controller.SetDisplayName(" \t ");
        original.Controller.SetFocus("  Unfinished \u674e  ");
        var checkpoint = original.Controller.CaptureSession();
        Assert(checkpoint.Draft.DisplayName == " \t " && checkpoint.SavedProfile == confirmed &&
            checkpoint.Page == ProfilePage.Edit && !checkpoint.Interrupted, "Capture keeps unsaved invalid edits separate from saved profile.");
        var oldKey = original.Controller.State.PageKey;
        var oldLifetime = original.App.Lifetime;
        var oldEntry = original.Controller.CurrentPageLifetime;
        var oldInput = original.Backend.Find("profile-name");
        original.Host.Dispose();
        Assert(oldLifetime.Token.IsCancellationRequested && oldEntry.IsCancellationRequested &&
            oldInput.Disposed && !oldInput.Events.Change("late"), "Original workspace, navigation, and input lifetime retire before replacement.");
        Throws<ObjectDisposedException>(() => original.Controller.CaptureSession());
        int calls = storage.Reads + storage.Writes + storage.Deletes;
        using var restored = new Harness(storage, ProfileWorkspaceSessionCodec.Restore(ProfileWorkspaceSessionCodec.Serialize(checkpoint)));
        var state = restored.Controller.State;
        Assert(storage.Reads + storage.Writes + storage.Deletes == calls && storage.Data!.SequenceEqual(savedBytes),
            "Capture, session serialization, construction, and attachment do no storage I/O.");
        Assert(state.Draft == checkpoint.Draft && state.SavedProfile == confirmed &&
            state.Page == ProfilePage.Edit && state.PageKey != oldKey && restored.Controller.NavigationDepth == 1,
            "Recreated editor gets new navigation identity and exact model data.");
        Assert(!restored.App.Lifetime.Token.IsCancellationRequested && !restored.Controller.CurrentPageLifetime.IsCancellationRequested &&
            !state.Busy && !state.CancelRequested && !state.Interrupted && restored.Controller.LastOperation.IsCompleted &&
            restored.Controller.LastError is null, "Fresh workspace has idle ownership rather than copied live resources.");
        Assert(((TextInput)restored.Backend.Find("profile-name").Element).Text == " \t " &&
            ((TextInput)restored.Backend.Find("profile-focus").Element).Text == "  Unfinished \u674e  " &&
            !restored.App.SaveButton.Enabled && !restored.Backend.Find("profile-preview").Events.Click(),
            "Incomplete drafts are displayed with the same validation, not normalized or saved.");
        Assert(state.Status == "Session restored. Stored draft was not read or changed." && state.Error == "",
            "Idle restore states explicitly that storage was untouched.");

        restored.Controller.SetDisplayName("Draft for preview");
        restored.Controller.Preview();
        var previewCheckpoint = restored.Controller.CaptureSession();
        var previewKey = restored.Controller.State.PageKey;
        var previewEntry = restored.Controller.CurrentPageLifetime;
        restored.Host.Dispose();
        Assert(previewEntry.IsCancellationRequested, "Old preview entry retires on disposal.");
        using var preview = new Harness(storage, previewCheckpoint);
        Assert(preview.Controller.State.Page == ProfilePage.Preview && preview.Controller.NavigationDepth == 2 &&
            preview.Controller.State.PageKey != previewKey && preview.Controller.State.SavedProfile == confirmed,
            "Preview recreation gets fresh edit/preview history and last confirmed saved data.");
        Assert(((Label)preview.Backend.Find("profile-preview-name").Element).Text == "Draft for preview" &&
            !preview.Backend.Contains("profile-name"), "Logical route recreates actual preview rather than an edit fallback.");
        var newPreviewToken = preview.Controller.CurrentPageLifetime;
        preview.Controller.Back();
        Assert(newPreviewToken.IsCancellationRequested && preview.Controller.State.Page == ProfilePage.Edit &&
            preview.Controller.NavigationDepth == 1 &&
            ((TextInput)preview.Backend.Find("profile-name").Element).Text == "Draft for preview",
            "Restored preview Back reveals the same draft in a new edit entry.");
        Assert(storage.Reads + storage.Writes + storage.Deletes == calls, "Restore and subsequent navigation never auto-save or auto-load.");
        preview.Controller.Preview();
        preview.Controller.SetDisplayName(" ");
        Throws<JsonException>(() => preview.Controller.CaptureSession());
        Assert(preview.Controller.State.Page == ProfilePage.Preview && preview.Controller.State.Draft.DisplayName == " ",
            "An inconsistent externally-authored preview checkpoint is rejected explicitly, never silently rerouted or normalized.");
    }

    private static void InterruptedSessionChecks()
    {
        foreach (string operation in new[] { "Save", "Load", "Delete" })
        {
            var storage = new MemoryStorage();
            using var original = new Harness(storage);
            original.SetProfile();
            original.Controller.Save();
            original.Finish();
            var confirmed = original.Controller.State.Draft;
            original.Controller.SetDisplayName("Unsaved replacement");
            original.Controller.Preview();
            var sideEffect = new TaskCompletionSource<OperationResult<bool>>(TaskCreationOptions.RunContinuationsAsynchronously);
            var read = new TaskCompletionSource<OperationResult<StoredValue>>(TaskCreationOptions.RunContinuationsAsynchronously);
            byte[]? pendingBytes = null;
            storage.Write = (data, _) => { pendingBytes = data.ToArray(); return sideEffect.Task; };
            storage.Read = _ => read.Task;
            storage.Delete = _ => sideEffect.Task;
            switch (operation)
            {
                case "Save": original.Controller.Save(); break;
                case "Load": original.Controller.Load(); break;
                case "Delete": original.Controller.Delete(); break;
            }
            Task pending = original.Controller.LastOperation;
            var checkpoint = original.Controller.CaptureSession();
            Assert(original.Controller.State.Busy && checkpoint.Interrupted &&
                checkpoint.Draft.DisplayName == "Unsaved replacement" && checkpoint.SavedProfile == confirmed,
                $"{operation} capture records interrupted work without substituting its unconfirmed result.");
            original.Controller.Cancel();
            var cancelCheckpoint = original.Controller.CaptureSession();
            Assert(original.Controller.State.CancelRequested && cancelCheckpoint == checkpoint,
                $"{operation} cancellation request is reduced to an interruption marker.");
            var token = original.App.Lifetime.Token;
            string oldKey = original.Controller.State.PageKey;
            original.Host.Dispose();
            Assert(token.IsCancellationRequested && storage.LastToken.IsCancellationRequested, $"{operation} old owner cancels provider token.");
            int calls = storage.Reads + storage.Writes + storage.Deletes;
            using var restored = new Harness(storage, ProfileWorkspaceSessionCodec.Restore(ProfileWorkspaceSessionCodec.Serialize(cancelCheckpoint)));
            Assert(!restored.Controller.State.Busy && !restored.Controller.State.CancelRequested &&
                restored.Controller.State.Interrupted && restored.Controller.State.Status == InterruptedStatus &&
                restored.Controller.State.Error == "" && restored.Controller.LastOperation.IsCompleted &&
                restored.Controller.LastError is null, $"{operation} restore is idle with an explicit storage-uncertainty warning.");
            Assert(restored.Controller.State.PageKey != oldKey && restored.Controller.NavigationDepth == 2 &&
                restored.Controller.State.Page == ProfilePage.Preview && restored.Controller.State.SavedProfile == confirmed,
                $"{operation} restores new route identity and previously confirmed data, not the operation's intended result.");
            Assert(restored.Controller.CaptureSession().Interrupted, $"{operation} interruption survives another recreation before checking storage.");
            Assert(storage.Reads + storage.Writes + storage.Deletes == calls, $"{operation} recreation does not retry or issue any provider operation.");
            Assert(restored.App.LoadButton.Enabled && restored.App.SaveButton.Enabled &&
                !restored.App.CancelButton.Visible && !restored.App.CancelButton.Enabled &&
                ((Label)restored.Backend.Find("profile-status").Element).Text == InterruptedStatus,
                $"{operation} recreated UI is idle with no inherited cancellation control or fake running operation.");

            if (operation == "Save") storage.Data = pendingBytes;
            if (operation == "Delete") storage.Data = null;
            if (operation == "Load")
                read.SetResult(OperationResult<StoredValue>.Completed(new(true,
                    Encoding.UTF8.GetBytes(ProfileWorkspaceCodec.Serialize(new("Late stale profile", "", "", ""))))));
            else
                sideEffect.SetResult(OperationResult<bool>.Completed(true));
            original.Dispatcher.Complete(pending);
            restored.Dispatcher.Drain();
            Assert(restored.Controller.State.Status == InterruptedStatus &&
                restored.Controller.State.Draft.DisplayName == "Unsaved replacement" &&
                restored.Controller.State.SavedProfile == confirmed && original.Unhandled.Count == 0 &&
                restored.Unhandled.Count == 0, $"{operation} retired work cannot update replacement component or claim rollback.");
            storage.Read = null;
            restored.Controller.Load();
            restored.Finish();
            Assert(!restored.Controller.State.Interrupted && !restored.Controller.CaptureSession().Interrupted,
                $"{operation} explicit successful storage check clears the interruption marker.");
            if (operation == "Save")
                Assert(restored.Controller.State.SavedProfile?.DisplayName == "Unsaved replacement" &&
                    restored.Controller.State.Status == "Saved draft loaded.", "Load sees the save that committed during recreation.");
            else if (operation == "Delete")
                Assert(restored.Controller.State.SavedProfile is null && restored.Controller.State.Draft.DisplayName == "Unsaved replacement" &&
                    restored.Controller.State.Status == "No saved draft found. Current edits kept.", "Load sees the delete that committed during recreation.");
            else
                Assert(restored.Controller.State.Draft == confirmed && restored.Controller.State.SavedProfile == confirmed,
                    "Explicit load uses actual provider values, not the retired stale read result.");
        }

        var noWaitStorage = new MemoryStorage();
        using var queued = new Harness(noWaitStorage);
        queued.SetProfile();
        queued.Controller.Save();
        Assert(noWaitStorage.Data is not null && queued.Controller.CaptureSession().Interrupted,
            "A provider commit awaiting UI delivery is still captured as interrupted, never guessed successful.");
        queued.Finish();
        Assert(!queued.Controller.CaptureSession().Interrupted, "Completed and observed save captures as idle.");

        var unavailable = new MemoryStorage
        {
            Read = _ => Task.FromResult(OperationResult<StoredValue>.Denied())
        };
        using var stillInterrupted = new Harness(unavailable,
            new ProfileWorkspaceSession(1, new(), null, ProfilePage.Edit, true));
        stillInterrupted.Controller.Load();
        stillInterrupted.Finish();
        Assert(stillInterrupted.Controller.State.Status == "Load denied." &&
            stillInterrupted.Controller.CaptureSession().Interrupted,
            "Denied storage access does not erase unresolved interruption from a future checkpoint.");
        stillInterrupted.Controller.SetDisplayName("Edited after interruption");
        Assert(stillInterrupted.Controller.CaptureSession().Interrupted,
            "Editing cannot pretend to resolve uncertain persisted data.");
    }
}

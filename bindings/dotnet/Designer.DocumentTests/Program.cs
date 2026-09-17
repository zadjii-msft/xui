using System.Text;
using Xui.Designer;

var root = Path.Combine(Path.GetTempPath(), "XuiDesignerDocuments-" + Guid.NewGuid().ToString("N"));
var drafts = Path.Combine(root, "drafts");
Directory.CreateDirectory(root);
int assertions = 0;
void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
    assertions++;
}
void Reject<T>(Action action, string message) where T : Exception
{
    try { action(); }
    catch (T) { assertions++; return; }
    throw new InvalidOperationException(message);
}

const string initial = "component Demo {\n  view { Text(\"Hello\"); }\n}";
const string edited = "component Demo {\n  view { Text(\"Edited\"); }\n}";
try
{
    var document = new DesignerDocumentStore(drafts, initial);
    Require(document.IsUntitled && !document.IsDirty && document.Source == initial, "An untouched template is replaceable.");
    document.PersistRecovery();
    document.New(initial);
    Require(!Directory.Exists(drafts), "Untouched documents do not need a recovery directory.");
    var first = Path.Combine(root, "first.xui");
    var second = Path.Combine(root, "second.xui");
    document.UpdateSource(initial.Replace('\n', '\r'));
    Require(!document.IsDirty, "Native paragraph normalization does not mark a document dirty.");
    document.Save(first);
    Require(document.FilePath == first && !document.IsDirty && File.ReadAllText(first) == initial, "Save writes LF UTF-8 and updates identity.");
    Require(!File.ReadAllBytes(first).AsSpan().StartsWith(new byte[] { 0xEF, 0xBB, 0xBF }), "Save does not emit a BOM.");
    document.UpdateSource(edited);
    document.PersistRecovery();
    Require(File.Exists(document.RecoveryPath), "An edited document has a recovery source.");
    Reject<InvalidOperationException>(() => document.New(initial), "New must protect unsaved source.");
    Reject<InvalidOperationException>(() => document.Open(first), "Open must protect unsaved source.");
    Require(document.Source == edited, "Failed replacement keeps current source.");

    var otherInstance = new DesignerDocumentStore(drafts, initial);
    var catalog = otherInstance.ListRecovery();
    Require(catalog.Count == 1 && catalog[0].Id == document.RecoveryId && catalog[0].Error is null, "Recovery is discoverable across instances.");
    Require(catalog[0].OriginalPath == first && !catalog[0].Legacy, "Recovery preserves original file identity.");
    Require(document.ListRecovery().Count == 0, "The active document does not list its own draft.");
    File.WriteAllText(document.RecoveryPath, initial);
    Require(otherInstance.ListRecovery()[0].Error is not null, "A partial recovery update cannot attach stale file identity to new source.");
    Reject<InvalidDataException>(() => otherInstance.Recover(document.RecoveryId), "Recovery rejects mismatched source and metadata.");
    Require(otherInstance.Source == initial && !otherInstance.IsDirty, "A partial snapshot cannot replace current source.");
    document.PersistRecovery();
    otherInstance.Recover(catalog[0].Id);
    Require(otherInstance.IsDirty && otherInstance.FilePath == first && otherInstance.Source == edited, "Recovery restores source and original destination.");
    otherInstance.Save();
    Require(File.ReadAllText(first) == edited && !otherInstance.IsDirty, "Recovery can save against its original disk identity.");
    Require(File.Exists(document.RecoveryPath), "Recovering a copy does not delete another instance's draft.");
    Reject<IOException>(() => document.Save(), "Another instance's save must cause a conflict.");
    Require(File.ReadAllText(first) == edited, "A save conflict leaves disk content unchanged.");
    document.Save(second);
    Require(!document.IsDirty && document.FilePath == second && !File.Exists(document.RecoveryPath), "Save As writes a new destination and clears own recovery.");

    File.WriteAllText(first, initial);
    Reject<IOException>(() => document.Save(first), "Save As must not overwrite an unrelated existing file.");
    document.Open(first);
    File.WriteAllText(first, initial, new UTF8Encoding(true));
    Reject<IOException>(() => document.Save(), "Even an encoding-only external change must not be overwritten.");
    document.Open(first);
    Require(document.Source == initial, "Open supports a UTF-8 BOM.");
    document.UpdateSource(edited);
    document.Save();
    Require(File.ReadAllText(first) == edited, "Save after explicit reopen can replace the loaded version.");
    File.Delete(first);
    document.Save();
    Require(File.Exists(first), "Save can recreate a deleted destination without replacing another file.");

    var legacyId = Guid.NewGuid();
    File.WriteAllText(Path.Combine(drafts, legacyId.ToString("N") + ".xui"), initial);
    var legacy = document.ListRecovery().Single(entry => entry.Id == legacyId);
    Require(legacy.Legacy && legacy.Error is null && legacy.OriginalPath is null, "First-version plain .xui recovery drafts remain discoverable.");
    document.Recover(legacyId);
    Require(document.IsUntitled && document.IsDirty, "Legacy recovery requires an explicit save destination.");
    Reject<InvalidOperationException>(() => document.Save(), "Untitled recovery cannot pick a destination silently.");
    document.New(initial, discardChanges: true);
    Require(!document.IsDirty && document.IsUntitled, "Explicit discard starts a clean template.");
    document.UpdateSource(edited);
    document.PersistRecovery();
    document.UpdateSource(initial);
    document.PersistRecovery();
    Require(!File.Exists(document.RecoveryPath), "Undoing all changes clears this document's draft.");

    var badId = Guid.NewGuid();
    var badPath = Path.Combine(drafts, badId.ToString("N") + ".xui");
    File.WriteAllText(badPath, initial);
    File.WriteAllText(Path.ChangeExtension(badPath, ".json"), "{broken");
    Require(document.ListRecovery().Single(entry => entry.Id == badId).Error is not null, "Corrupt recovery metadata appears as an explicit error.");
    Reject<System.Text.Json.JsonException>(() => document.Recover(badId), "Recover must not silently ignore corrupt metadata.");
    Require(document.Source == initial && !document.IsDirty, "A failed recovery keeps the current document.");
    document.DeleteRecovery(badId);
    Require(!File.Exists(badPath) && !File.Exists(Path.ChangeExtension(badPath, ".json")), "Explicit recovery deletion removes only the selected source and metadata.");
    Require(File.Exists(Path.Combine(drafts, legacyId.ToString("N") + ".xui")), "Explicit deletion preserves other recovery entries.");

    Reject<InvalidDataException>(() => document.UpdateSource(new string('x', DesignerDocumentStore.MaximumSourceLength + 1)), "Oversized source must fail.");
    Reject<InvalidDataException>(() => document.UpdateSource("bad\0source"), "NUL source must fail.");
    Reject<EncoderFallbackException>(() => document.UpdateSource("bad\uD800source"), "Malformed Unicode must fail.");
    Require(document.Source == initial, "Invalid source does not mutate the document.");
    File.WriteAllBytes(first, [0xFF]);
    Reject<DecoderFallbackException>(() => document.Open(first), "Open must reject invalid UTF-8.");
    File.WriteAllText(first, new string('x', DesignerDocumentStore.MaximumSourceLength + 1));
    Reject<InvalidDataException>(() => document.Open(first), "Open must reject oversized source.");
    Reject<ArgumentException>(() => document.Save(Path.Combine(root, "wrong.txt")), "Only .xui save destinations are allowed.");
    Reject<InvalidOperationException>(() => document.Recover(document.RecoveryId), "A document cannot recover its own active draft.");
    var blocked = new DesignerDocumentStore(drafts, initial);
    blocked.UpdateSource(edited);
    Directory.CreateDirectory(blocked.RecoveryPath);
    var blockedDestination = Path.Combine(root, "cleanup-error.xui");
    try
    {
        blocked.Save(blockedDestination);
        throw new InvalidOperationException("Recovery cleanup failure must be reported.");
    }
    catch (IOException error)
    {
        Require(error.Message.StartsWith("The file was saved,", StringComparison.Ordinal), "A cleanup error must distinguish successful disk save.");
    }
    Require(!blocked.IsDirty && File.ReadAllText(blockedDestination) == edited, "A cleanup error preserves the successful save identity.");
    Directory.Delete(blocked.RecoveryPath);
    Require(Directory.GetFiles(root, "*.tmp", SearchOption.AllDirectories).Length == 0, "No temporary writes remain.");
    Console.WriteLine($"Designer document assertions: {assertions} passed.");
}
catch (Exception error)
{
    Console.Error.WriteLine(error);
    Environment.ExitCode = 1;
}
finally
{
    Directory.Delete(root, recursive: true);
}

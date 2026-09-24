using Android.App;
using Android.Content;
using OpenableColumns = Android.Provider.IOpenableColumns;
using Xui.Experimental.Portable;

namespace Xui.Experimental.Android;

/// <summary>Single-file SAF selection with an Activity-owned, nonpersistent URI grant.</summary>
/// <remarks>
/// Use one instance and a unique request code per Activity lifetime. Forward OnActivityResult
/// to HandleActivityResult and dispose on Activity destruction. Canceled native requests keep
/// their slot until the result returns; cancellation cannot claim to dismiss external UI.
/// No filesystem path or persistable URI permission is acquired.
/// </remarks>
public sealed class AndroidFilePicker : IFilePicker, IDisposable
{
    private readonly WeakReference<Activity> activity;
    private readonly FileSelectionAndroidProtocol protocol;
    private readonly int requestCode;

    public AndroidFilePicker(Activity activity, IUiDispatcher dispatcher, int requestCode, Action<Exception> reportError)
    {
        ArgumentNullException.ThrowIfNull(activity);
        ArgumentNullException.ThrowIfNull(dispatcher);
        ArgumentNullException.ThrowIfNull(reportError);
        if (requestCode is < 0 or > 65535) throw new ArgumentOutOfRangeException(nameof(requestCode));
        if (!dispatcher.CheckAccess()) throw new InvalidOperationException("Create the Android picker on the UI thread.");
        this.activity = new(activity);
        this.requestCode = requestCode;
        protocol = new(new AndroidFilePickerBridge(this.activity, dispatcher, requestCode), reportError);
    }

    public CapabilityAvailability GetAvailability(ServiceCapability capability) => protocol.GetAvailability(capability);
    public Task<OperationResult<PickedFile>> OpenAsync(FileSelectionOptions options, CancellationToken cancellationToken = default) =>
        protocol.OpenAsync(options, cancellationToken);

    public bool HandleActivityResult(int requestCode, Result resultCode, Intent? data)
    {
        if (requestCode != this.requestCode) return false;
        return protocol.HandleResult(options =>
        {
            if (resultCode == Result.Canceled) return OperationResult<PickedFile>.Cancelled();
            if (resultCode != Result.Ok) throw new IOException($"Unexpected Android picker result: {resultCode}.");
            if (data?.Data is not { } uri || uri.Scheme != "content")
                throw new IOException("The Android picker did not return one content URI.");
            if (data.ClipData is { ItemCount: > 1 }) throw new IOException("The picker returned multiple files.");
            if (!activity.TryGetTarget(out var owner) || owner.IsFinishing || owner.IsDestroyed)
                throw new ObjectDisposedException(nameof(Activity));
            var resolver = owner.ContentResolver ?? throw new InvalidOperationException("No content resolver is available.");
            Stream? stream = null;
            try
            {
                string name;
                long? length = null;
                using (var cursor = resolver.Query(uri, [OpenableColumns.DisplayName, OpenableColumns.Size], null, null, null))
                {
                    if (cursor is null || !cursor.MoveToFirst()) throw new IOException("The selected provider returned no file metadata.");
                    int nameColumn = cursor.GetColumnIndex(OpenableColumns.DisplayName);
                    int sizeColumn = cursor.GetColumnIndex(OpenableColumns.Size);
                    if (nameColumn < 0 || cursor.IsNull(nameColumn)) throw new IOException("The selected file has no display name.");
                    name = cursor.GetString(nameColumn) ?? throw new IOException("The selected file has no display name.");
                    if (sizeColumn >= 0 && !cursor.IsNull(sizeColumn))
                    {
                        long size = cursor.GetLong(sizeColumn);
                        if (size >= 0) length = size;
                    }
                }
                if (length > options.MaximumBytes) throw new FileSelectionTooLargeException(options.MaximumBytes);
                stream = resolver.OpenInputStream(uri) ?? throw new IOException("The selected provider returned no read stream.");
                var file = new PickedFile(name, stream, options, length);
                stream = null;
                return OperationResult<PickedFile>.Completed(file);
            }
            catch (Java.Lang.SecurityException error)
            {
                if (stream is not null) DisposeAfterFailure(stream, error);
                return OperationResult<PickedFile>.Denied();
            }
            catch (Exception error)
            {
                if (stream is not null) DisposeAfterFailure(stream, error);
                throw;
            }
        });
    }

    private static void DisposeAfterFailure(Stream stream, Exception error)
    {
        try { stream.Dispose(); }
        catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
    }

    public void Dispose() => protocol.Dispose();
}

internal sealed class AndroidFilePickerBridge(WeakReference<Activity> activity, IUiDispatcher dispatcher, int requestCode)
    : IAndroidFilePickerBridge
{
    public bool CheckAccess() => dispatcher.CheckAccess();
    public bool IsAvailable => activity.TryGetTarget(out var owner) &&
        !owner.IsFinishing && !owner.IsDestroyed && owner.HasWindowFocus;

    public void Post(Action action, Action<Exception> canceled)
    {
        if (dispatcher is ICancellableUiDispatcher cancellable) cancellable.Post(action, canceled);
        else dispatcher.Post(action);
    }

    public void Launch()
    {
        if (!activity.TryGetTarget(out var owner)) throw new ObjectDisposedException(nameof(Activity));
        using var intent = new Intent(Intent.ActionOpenDocument);
        intent.AddCategory(Intent.CategoryOpenable);
        intent.SetType("*/*");
        intent.PutExtra(Intent.ExtraAllowMultiple, false);
        intent.AddFlags(ActivityFlags.GrantReadUriPermission);
        try { owner.StartActivityForResult(intent, requestCode); }
        catch (Java.Lang.SecurityException error) { throw new UnauthorizedAccessException("Android denied file selection.", error); }
        catch (ActivityNotFoundException error) { throw new NotSupportedException("No Android document picker is available.", error); }
    }
}

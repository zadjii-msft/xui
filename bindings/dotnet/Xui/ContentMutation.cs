using System.Runtime.InteropServices;

namespace Xui;

public sealed partial class ContentUpdate
{
    private ContentAppend? append;

    /// <summary>Whether the loaded native runtime exports the complete scoped mutation API.</summary>
    public static bool SupportsMutation => Native.ContentMutationAvailable.Value;

    /// <summary>Checks that structural mutation is safe before changing application state.</summary>
    /// <remarks>Active native text composition rejects the operation, including inserts and removals.</remarks>
    public void ValidateMutation()
    {
        Host.Window.Guard();
        ObjectDisposedException.ThrowIf(Retired, this);
        RequireMutation();
        Host.Window.Check(Native.ContentValidateMutation(Handle));
    }

    /// <summary>Begins construction of fresh elements inside this committed attachment arena.</summary>
    /// <remarks>Complete the append before inserting its root. Disposal rolls back new handles and callbacks.
    /// Existing native elements cannot change topology while an append is active.</remarks>
    public ContentAppend BeginAppend()
    {
        Host.Window.Guard();
        ObjectDisposedException.ThrowIf(Retired, this);
        RequireMutation();
        if (!Committed || append is not null)
            throw new InvalidOperationException("Append requires a committed scope without active construction.");
        var next = new ContentAppend(this);
        Host.Window.Check(Native.ContentBeginAppend(Handle));
        append = next;
        Host.Window.BeginContentBuild(this);
        return next;
    }

    /// <summary>Releases one detached element handle and its managed callback registrations.</summary>
    /// <remarks>Release descendants before their detached root. Native validation rejects live or foreign elements.</remarks>
    public void ReleaseElement(Element element)
    {
        ArgumentNullException.ThrowIfNull(element);
        Host.Window.Guard();
        ObjectDisposedException.ThrowIf(Retired, this);
        RequireMutation();
        element.BelongsTo(Host.Window);
        Host.Window.Check(Native.ContentReleaseElement(Handle, element.Handle));
        Host.Window.RetireContentCallbacks(this, handle => handle == element.Handle);
    }

    internal static void RequireMutation()
    {
        if (!SupportsMutation) throw new NotSupportedException("This native XUI runtime does not support scoped content mutation.");
    }

    public sealed class ContentAppend : IDisposable
    {
        private readonly ContentUpdate scope;
        private readonly HashSet<Window.PostedAction> posts;
        private bool finished;

        internal ContentAppend(ContentUpdate scope)
        {
            this.scope = scope;
            lock (scope.postedGate) posts = [.. scope.posts];
        }

        /// <summary>Retains freshly constructed native elements and leaves the construction context.</summary>
        public void Complete()
        {
            scope.Host.Window.Guard();
            ObjectDisposedException.ThrowIf(scope.Retired, scope);
            if (finished || scope.append != this) throw new InvalidOperationException("The content append is no longer active.");
            End(keep: true);
        }

        private void End(bool keep)
        {
            scope.Host.Window.Check(Native.ContentEndAppend(scope.Handle, keep ? 1u : 0u));
            scope.Host.Window.EndContentBuild(scope);
            scope.append = null;
            finished = true;
            if (keep)
            {
                posts.Clear();
                return;
            }
            var retired = new HashSet<ulong>();
            foreach (ulong handle in scope.Host.Window.ContentCallbackHandles(scope))
            {
                int status = Native.ContentOwner(handle, out _);
                if (status == 2) retired.Add(handle);
                else scope.Host.Window.Check(status);
            }
            scope.Host.Window.RetireContentCallbacks(scope, retired.Contains);
            lock (scope.postedGate)
            {
                foreach (var post in scope.posts.Where(post => !posts.Contains(post)).ToArray())
                {
                    post.Action = null;
                    scope.posts.Remove(post);
                }
            }
            posts.Clear();
        }

        public void Dispose()
        {
            if (finished || scope.Retired) return;
            scope.Host.Window.Guard();
            End(keep: false);
        }
    }
}

public sealed partial class Stack
{
    /// <summary>Inserts a freshly constructed same-scope child into committed content.</summary>
    public void Insert(int index, Element child, float flex = 0)
    {
        ArgumentNullException.ThrowIfNull(child);
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Window.Guard();
        ContentUpdate.RequireMutation();
        child.BelongsTo(Window);
        Window.Check(Native.ContentStackInsert(Handle, checked((uint)index), child.Handle, flex));
    }

    /// <summary>Detaches a child and its native widgets. Release detached handles separately, descendants first.</summary>
    public void Remove(Element child)
    {
        ArgumentNullException.ThrowIfNull(child);
        Window.Guard();
        ContentUpdate.RequireMutation();
        child.BelongsTo(Window);
        Window.Check(Native.ContentStackRemove(Handle, child.Handle));
    }

    /// <summary>Preflights a move without changing state. The future index may include planned insertions.</summary>
    public void ValidateMove(Element child, int futureIndex)
    {
        ArgumentNullException.ThrowIfNull(child);
        ArgumentOutOfRangeException.ThrowIfNegative(futureIndex);
        Window.Guard();
        ContentUpdate.RequireMutation();
        child.BelongsTo(Window);
        Window.Check(Native.ContentStackValidateMove(Handle, child.Handle, checked((uint)futureIndex)));
    }

    /// <summary>Reorders one child in place. The index is its final position after removal.</summary>
    public void Move(Element child, int finalIndex)
    {
        ArgumentNullException.ThrowIfNull(child);
        ArgumentOutOfRangeException.ThrowIfNegative(finalIndex);
        Window.Guard();
        ContentUpdate.RequireMutation();
        child.BelongsTo(Window);
        Window.Check(Native.ContentStackMove(Handle, child.Handle, checked((uint)finalIndex)));
    }
}

internal static partial class Native
{
    internal static readonly Lazy<bool> ContentMutationAvailable = new(() => HasExports(
        "xui_content_begin_append", "xui_content_end_append", "xui_content_validate_mutation",
        "xui_content_stack_insert", "xui_content_stack_remove", "xui_content_stack_validate_move",
        "xui_content_stack_move", "xui_content_release_element"));

    internal static bool HasExports(params string[] names)
    {
        nint library = NativeLibrary.Load("xui", typeof(Native).Assembly, null);
        try
        {
            return names.All(name => NativeLibrary.TryGetExport(library, name, out _));
        }
        finally { NativeLibrary.Free(library); }
    }

    [LibraryImport("xui", EntryPoint = "xui_content_begin_append")]
    internal static partial int ContentBeginAppend(ulong scope);
    [LibraryImport("xui", EntryPoint = "xui_content_end_append")]
    internal static partial int ContentEndAppend(ulong scope, uint keep);
    [LibraryImport("xui", EntryPoint = "xui_content_validate_mutation")]
    internal static partial int ContentValidateMutation(ulong scope);
    [LibraryImport("xui", EntryPoint = "xui_content_stack_insert")]
    internal static partial int ContentStackInsert(ulong stack, uint index, ulong child, float flex);
    [LibraryImport("xui", EntryPoint = "xui_content_stack_remove")]
    internal static partial int ContentStackRemove(ulong stack, ulong child);
    [LibraryImport("xui", EntryPoint = "xui_content_stack_validate_move")]
    internal static partial int ContentStackValidateMove(ulong stack, ulong child, uint futureIndex);
    [LibraryImport("xui", EntryPoint = "xui_content_stack_move")]
    internal static partial int ContentStackMove(ulong stack, ulong child, uint finalIndex);
    [LibraryImport("xui", EntryPoint = "xui_content_release_element")]
    internal static partial int ContentReleaseElement(ulong scope, ulong element);
}

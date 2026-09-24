namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    private long mutationRevision;
    private int viewportUpdates;

    private void NoteNativeModelMutation() => mutationRevision = checked(mutationRevision + 1);

    private void UpdateAttachment(Attachment current, Action update)
    {
        updating++;
        Exception? failure = null;
        try
        {
            NoteNativeModelMutation();
            update();
        }
        catch (Exception error) { failure = FailAttachment(current, error); }
        finally { updating--; }
        FinishInteractionDelivery(failure);
    }

    private Exception FailAttachment(Attachment current, Exception error)
    {
        var failures = new List<Exception> { error };
        if (ReferenceEquals(attachment, current))
        {
            attachment = null;
            bool previous = transitioning;
            transitioning = true;
            try { ReleaseAttachment(current, failures); }
            finally { transitioning = previous; }
        }
        return failures.Count == 1 ? error : new AggregateException(failures);
    }

    private abstract class AttachmentResource(Element owner)
    {
        internal Element Owner { get; } = owner;
        internal bool IsRetired { get; private set; }

        internal void Retire()
        {
            if (IsRetired) return;
            IsRetired = true;
            DisposeCore();
        }

        protected abstract void DisposeCore();
    }

    private static void RetireAttachmentResources(Attachment current, ISet<Element>? removed, List<Exception> failures)
    {
        foreach (var element in current.Order)
            if (removed is null || removed.Contains(element))
            {
                if (element is RangeInput range) range.ResetPreview();
                if (element is TextInput input)
                {
                    current.Interactions.Remove(input);
                    input.ResetInteraction();
                }
            }
        foreach (var resource in current.Resources.AsEnumerable().Reverse().ToArray())
        {
            if (removed is not null && !removed.Contains(resource.Owner)) continue;
            current.Resources.Remove(resource);
            try { resource.Retire(); }
            catch (Exception error) { failures.Add(error); }
        }
        ClearAttachmentPasswords(current, removed, failures);
        CancelAttachmentImages(current, removed, failures);
    }
}

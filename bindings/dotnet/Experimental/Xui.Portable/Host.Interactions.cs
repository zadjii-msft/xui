using System.Runtime.ExceptionServices;

namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    private int interactionDeferrals;
    private bool deliveringInteractions;

    private void CaptureInteraction(Attachment current, TextInput input, TextInteraction value)
    {
        if (input.CaptureInteraction(value)) current.Interactions[input] = value;
        FinishInteractionDelivery();
    }

    private void FinishInteractionDelivery(Exception? operationError = null)
    {
        var failures = new List<Exception>();
        if (operationError is not null) failures.Add(operationError);
        if (!deliveringInteractions && interactionDeferrals == 0 && updating == 0 && !transitioning && viewportUpdates == 0)
        {
            deliveringInteractions = true;
            try
            {
                while (attachment is { } current && current.Interactions.Count != 0 &&
                    interactionDeferrals == 0 && updating == 0 && !transitioning && viewportUpdates == 0)
                {
                    var pending = current.Interactions.First();
                    current.Interactions.Remove(pending.Key);
                    if (pending.Key.Disposed || !current.Peers.ContainsKey(pending.Key) || pending.Key.Interaction != pending.Value) continue;
                    try { pending.Key.NotifyInteraction(pending.Value); }
                    catch (Exception error) { failures.Add(error); }
                }
            }
            finally { deliveringInteractions = false; }
        }
        if (failures.Count > 1) throw new AggregateException(failures);
        if (failures.Count == 1) ExceptionDispatchInfo.Capture(failures[0]).Throw();
    }

    private T DeferInteractionDelivery<T>(Func<T> operation)
    {
        interactionDeferrals++;
        T result = default!;
        Exception? failure = null;
        try { result = operation(); }
        catch (Exception error) { failure = error; }
        finally { interactionDeferrals--; }
        FinishInteractionDelivery(failure);
        return result;
    }
}

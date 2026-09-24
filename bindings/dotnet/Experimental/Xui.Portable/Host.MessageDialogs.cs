namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    private bool MessageDialogInputBlocked =>
        attachment is { } current && current.Resources.Any(resource => resource is MessageDialogOperation);

    public CapabilityAvailability GetMessageDialogAvailability()
    {
        VerifyAccess();
        if (attachment?.Backend is not IMessageDialogBackend backend) return CapabilityAvailability.Unsupported;
        var availability = InputOperation(() => backend.MessageDialogAvailability);
        if (!Enum.IsDefined(availability)) throw new InvalidOperationException("The backend returned an invalid dialog availability.");
        return availability;
    }

    public Task<OperationResult<MessageDialogDecision>> ShowMessageAsync(MessageDialogRequest request,
        CancellationToken cancellationToken = default) =>
        DeferInteractionDelivery(() => ShowMessageCore(request, cancellationToken));

    private Task<OperationResult<MessageDialogDecision>> ShowMessageCore(MessageDialogRequest request, CancellationToken cancellationToken)
    {
        VerifyMutation();
        ArgumentNullException.ThrowIfNull(request);
        if (building is not null || reconciling != 0 || viewportUpdates != 0)
            throw new InvalidOperationException("Finish construction and viewport updates before requesting a modal message.");
        var current = attachment ?? throw new InvalidOperationException("Message dialogs require an attached host.");
        if (cancellationToken.IsCancellationRequested)
            return Task.FromCanceled<OperationResult<MessageDialogDecision>>(cancellationToken);
        if (current.Resources.Any(resource => resource is MessageDialogOperation))
            throw new InvalidOperationException("A message dialog is already active for this host.");
        if (current.Backend is not IMessageDialogBackend backend ||
            GetMessageDialogAvailability() == CapabilityAvailability.Unsupported)
            return Task.FromResult(OperationResult<MessageDialogDecision>.Unsupported());
        var operation = new MessageDialogOperation(this, current, root!, request, cancellationToken);
        current.Resources.Add(operation);
        operation.Start(backend);
        return operation.Task;
    }

    private sealed class MessageDialogOperation(Host host, Attachment current, Element owner,
        MessageDialogRequest request, CancellationToken token) : AttachmentResource(owner)
    {
        private readonly TaskCompletionSource<OperationResult<MessageDialogDecision>> completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private IMessageDialogRequest? native;
        private CancellationTokenRegistration cancellation;
        private OperationResult<MessageDialogDecision>? result;
        private Exception? failure;
        private bool operating;
        private bool deliveryInvalid;
        internal Task<OperationResult<MessageDialogDecision>> Task => completion.Task;

        internal void Start(IMessageDialogBackend backend)
        {
            operating = true;
            try
            {
                native = host.InputOperation(() => backend.BeginMessageDialog(request, Complete));
                if (native is null) throw new InvalidOperationException("The backend returned no owned message dialog request.");
                if (deliveryInvalid) throw new InvalidOperationException("Message dialog completion must be posted after Begin returns.");
            }
            catch (Exception error)
            {
                failure = error;
                CloseAndComplete();
                return;
            }
            finally { operating = false; }
            cancellation = token.Register(CancelRequested);
        }

        private void CancelRequested()
        {
            var pending = Volatile.Read(ref native);
            if (pending is null) return;
            try
            {
                if (host.dispatcher.CheckAccess())
                    host.DeferInteractionDelivery(() =>
                    {
                        operating = true;
                        try { host.InputOperation(() => { pending.Cancel(); return true; }); }
                        finally { operating = false; }
                        if (deliveryInvalid) throw new InvalidOperationException("Message dialog cancellation cannot complete inline.");
                        return true;
                    });
                else pending.Cancel();
            }
            catch (Exception error)
            {
                if (host.dispatcher.CheckAccess() && deliveryInvalid && !IsRetired)
                {
                    failure = error;
                    CloseAndComplete();
                }
                else if (!completion.TrySetException(error))
                    throw;
            }
        }

        private void Complete(OperationResult<MessageDialogDecision> outcome)
        {
            host.VerifyThread();
            if (IsRetired || !ReferenceEquals(host.attachment, current)) return;
            if (operating)
            {
                deliveryInvalid = true;
                return;
            }
            host.DeferInteractionDelivery(() =>
            {
                try
                {
                    if (host.transitioning || host.updating != 0 || host.building is not null || host.reconciling != 0 || host.viewportUpdates != 0)
                        throw new InvalidOperationException("Message dialog completion cannot reenter a native or structural operation.");
                    ArgumentNullException.ThrowIfNull(outcome);
                    if (outcome.Status == OperationStatus.Completed &&
                        (!Enum.IsDefined(outcome.Value) || request.Kind == MessageDialogKind.Alert && outcome.Value != MessageDialogDecision.Accepted))
                        throw new InvalidOperationException("The backend returned a decision incompatible with the message dialog.");
                    result = outcome;
                }
                catch (Exception error) { failure = error; }
                CloseAndComplete();
                return true;
            });
        }

        private void CloseAndComplete()
        {
            current.Resources.Remove(this);
            bool wasCompleted = completion.Task.IsCompleted;
            try { Retire(); }
            catch (Exception error)
            {
                if (wasCompleted) throw;
                completion.TrySetException(error);
            }
        }

        protected override void DisposeCore()
        {
            cancellation.Unregister();
            var pending = Interlocked.Exchange(ref native, null);
            Exception? cleanupError = null;
            try
            {
                if (pending is not null) host.InputOperation(() => { pending.Dispose(); return true; });
            }
            catch (Exception error) { cleanupError = error; }
            var errorToReport = failure is null ? cleanupError : cleanupError is null
                ? failure : new AggregateException(failure, cleanupError);
            if (errorToReport is not null) completion.TrySetException(errorToReport);
            else if (token.IsCancellationRequested || result is null)
                completion.TrySetCanceled(token.IsCancellationRequested ? token : new CancellationToken(canceled: true));
            else completion.TrySetResult(result);
            if (cleanupError is not null) throw errorToReport!;
        }
    }
}

using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void ImageCleanupFailureChecks()
    {
        using var input = typeof(Program).Assembly.GetManifestResourceStream("Xui.Asset.pixels.png")!;
        using var bytes = new MemoryStream();
        input.CopyTo(bytes);
        var plan = P.ImageDecodePlan.Inspect(bytes.ToArray(), new P.ImageDecodeOptions());
        var budget = new P.ImagePixelBudget();
        var resources = new WindowsImageResources();
        var cancellationFailure = new InvalidOperationException("Injected cancellation notification failure.");
        var nativeFailure = new InvalidOperationException("Injected native cancellation failure.");
        var native = new FailingImageOwner(nativeFailure, failures: 1);
        using (var lifetime = new P.ImageRequestLifetime(new ImageCleanupDispatcher(), error => throw error))
        {
            var request = lifetime.Begin();
            request.Token.Register(() => throw cancellationFailure);
            var resource = resources.Create();
            resource.Pixels = budget.Reserve(plan);
            resource.Native = native;
            Check(request.TryOwn(resource), "The cleanup fixture did not own its native request.");
            AggregateException? failure = null;
            try { lifetime.Dispose(); }
            catch (AggregateException error) { failure = error; }
            Check(failure is not null && failure.Flatten().InnerExceptions.Contains(cancellationFailure) &&
                failure.Flatten().InnerExceptions.Contains(nativeFailure),
                "Image cancellation lost the original notification or native cleanup error.");
            Check(native.Calls == 1 && !native.Retired,
                "A token cancellation failure skipped remaining native cleanup.");
            Check(resources.Count == 1 && budget.ReservedBytes == plan.OutputPixelBytes,
                "Failed native cancellation freed or orphaned a still-live pixel reservation.");

            // ImageRequestLifetime has dropped its reference; the attachment's ledger must still own it.
            resource.Dispose();
            Check(native.Calls == 2 && native.Retired && resources.Count == 0 && budget.ReservedBytes == 0,
                "Successful native retry did not release the retained reservation.");
            resource.Dispose();
            resources.CompleteNativeRetirement();
            Check(native.Calls == 2 && budget.ReservedBytes == 0,
                "Repeated image retirement repeated native release or underflowed the pixel budget.");
        }

        var first = new FailingImageOwner(nativeFailure, failures: int.MaxValue);
        var second = new FailingImageOwner(new IOException("Second injected cancellation failure."), failures: int.MaxValue);
        var firstResource = resources.Create();
        firstResource.Native = first;
        firstResource.Pixels = budget.Reserve(plan);
        var secondResource = resources.Create();
        secondResource.Native = second;
        secondResource.Pixels = budget.Reserve(plan);
        Throws<InvalidOperationException>(firstResource.Dispose);
        Throws<IOException>(secondResource.Dispose);
        Check(resources.Count == 2 && budget.ReservedBytes == plan.OutputPixelBytes * 2 &&
            !first.Retired && !second.Retired,
            "Failed per-image cleanup released pixels before native owner retirement.");
        var arena = new FailingImageArena(first, second);
        void RetireArena()
        {
            arena.Dispose();
            resources.CompleteNativeRetirement();
        }
        Throws<IOException>(RetireArena);
        Check(resources.Count == 2 && budget.ReservedBytes == plan.OutputPixelBytes * 2,
            "A failed containing-arena teardown released reservations without acknowledgement.");

        RetireArena();
        Check(resources.Count == 0 && budget.ReservedBytes == 0 && first.Calls == 1 && second.Calls == 1,
            "Acknowledged arena retirement did not release all remaining image reservations without retrying stale native handles.");
        resources.CompleteNativeRetirement();
        Check(budget.ReservedBytes == 0, "Repeated arena acknowledgement underflowed pixel reservations.");
    }

    private sealed class FailingImageOwner(Exception failure, int failures) : IDisposable
    {
        internal int Calls;
        internal bool Retired;
        public void Dispose()
        {
            if (Retired) return;
            Calls++;
            if (Calls <= failures) throw failure;
            Retired = true;
        }
        internal void RetireByContainingOwner() => Retired = true;
    }

    private sealed class ImageCleanupDispatcher : P.IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action) => throw new InvalidOperationException("Cleanup must not require queued work.");
    }

    private sealed class FailingImageArena(params FailingImageOwner[] images) : IDisposable
    {
        private bool failed;
        public void Dispose()
        {
            if (!failed)
            {
                failed = true;
                throw new IOException("Injected containing arena release failure.");
            }
            foreach (var image in images) image.RetireByContainingOwner();
        }
    }
}

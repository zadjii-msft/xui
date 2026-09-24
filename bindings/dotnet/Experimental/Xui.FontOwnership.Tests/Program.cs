using Xui.Experimental.Portable;

internal static class Program
{
    private static int assertions;
    private static readonly FontResourceStyle Regular = new(400);
    private static readonly PackagedFontSource Source = new(new PackagedAssetManifest(typeof(Program).Assembly),
        "fonts/abel.ttf", "8809dcad25318225052f88333e208c5aad4adcb7b2c934c135735ec19aa410b4",
        new FontLicenseDeclaration("licenses/abel.txt", "4f4bc3806a1e55789c6ef75ca5fc628297b05292f74966474dc0d40324abc609",
            "OFL-1.1", new("https://github.com/google/fonts/tree/9437b806936896fa1a8c812e561067a5f30f5933/ofl/abel")));

    private static void Main()
    {
        TransferAndPins();
        ScopedAndStyleGuards();
        BindingLifetime();
        FailureRetention();
        ReentrancyAndCancellation();
        LoadCancellation();
        RepeatedRetirement();
        Console.WriteLine($"Registered font ownership: {assertions} assertions passed using fake native tokens; no registration or rendering claim.");
    }

    private static void Check(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
        assertions++;
    }
    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
    private static FontResourceLease Bytes(FontResourceCache cache) => cache.AcquireAsync(Source).GetAwaiter().GetResult();
    private static FontResource Own(FontResourceCache cache, FontResourceScope scope, Native native) => scope.Adopt(Bytes(cache), native);

    private static void TransferAndPins()
    {
        var dispatcher = new Dispatcher();
        using var cache = new FontResourceCache();
        using var scope = new FontResourceScope(dispatcher);
        var bytes = Bytes(cache);
        var native = new Native();
        var resource = scope.Adopt(bytes, native);
        Throws<ArgumentException>(() => scope.Adopt(bytes, new Native()));
        using var anotherBytes = Bytes(cache);
        Throws<ArgumentException>(() => scope.Adopt(anotherBytes, native));
        var first = resource.Acquire(scope);
        var second = resource.Acquire(scope);
        resource.Dispose();
        Check(!resource.AcceptsNewUses && native.Disposals == 0, "Closing the application handle preserves existing bound pins.");
        Throws<ObjectDisposedException>(() => resource.Acquire(scope));
        Check(ReferenceEquals(first.Registration, native) && ReferenceEquals(second.Resource, resource), "Bound pins retain the ready native identity after caller closure.");
        first.Dispose();
        first.Dispose();
        Check(native.Disposals == 0, "One pin retirement cannot release another native consumer's font.");
        Throws<ObjectDisposedException>(() => _ = first.Resource);
        native.BeforeDispose = () => Check(bytes.FontBytes.Length == 35220, "Native registration retires before its borrowed encoded bytes.");
        second.Dispose();
        Check(native.Disposals == 1, "Last native-use pin triggers exactly one registration disposal.");
        Throws<ObjectDisposedException>(() => _ = bytes.FontBytes);
        var retryBytes = Bytes(cache);
        var partial = new PartiallyRetiredNative();
        var retry = scope.Adopt(retryBytes, partial);
        Throws<IOException>(() => retry.Dispose());
        Check(partial.FirstReleased == 1 && partial.SecondReleased == 0 && retryBytes.FontBytes.Length > 0,
            "Partial native retirement preserves the borrowed encoded owner.");
        retry.Dispose();
        retry.Dispose();
        Check(partial.FirstReleased == 1 && partial.SecondReleased == 1,
            "Explicit retryable native owners do not repeat already-successful handle retirement.");
        scope.Dispose();
        Check(native.Disposals == 1 && scope.Token.IsCancellationRequested, "Scope closes once without repeating successful native cleanup.");
        var lateBytes = Bytes(cache);
        var lateNative = new Native();
        Throws<ObjectDisposedException>(() => scope.Adopt(lateBytes, lateNative));
        Check(lateBytes.FontBytes.Length == 35220 && lateNative.Disposals == 0, "Rejected late adoption leaves both owners with the producer.");
        lateNative.Dispose();
        lateBytes.Dispose();
        var closedBytes = Bytes(cache);
        closedBytes.Dispose();
        using var fresh = new FontResourceScope(dispatcher);
        var notAdopted = new Native();
        Throws<ObjectDisposedException>(() => fresh.Adopt(closedBytes, notAdopted));
        Check(notAdopted.Disposals == 0, "Invalid encoded owner does not transfer its native counterpart.");
        notAdopted.Dispose();
    }

    private static void ScopedAndStyleGuards()
    {
        var dispatcher = new Dispatcher();
        using var cache = new FontResourceCache();
        using var a = new FontResourceScope(dispatcher);
        using var b = new FontResourceScope(dispatcher);
        var native = new Native();
        var font = Own(cache, a, native);
        Throws<ArgumentException>(() => font.Acquire(b));
        var target = new Target();
        using var binding = new FontResourceBinding(b, target);
        Throws<ArgumentException>(() => binding.Set(font, Regular));
        Check(target.Applications == 0 && target.Validations == 0, "Foreign attachment handles never reach native preflight.");
        using var ownBinding = new FontResourceBinding(a, target);
        Throws<NotSupportedException>(() => ownBinding.Set(font, new(700)));
        Throws<NotSupportedException>(() => ownBinding.Set(font, new(400, true)));
        Throws<ArgumentException>(() => ownBinding.Set(font, default));
        Throws<ArgumentOutOfRangeException>(() => new FontResourceStyle(0));
        Check(target.Applications == 0, "Custom weight/style and unresolved inherited weight fail before mutation.");
        target.Composing = true;
        Throws<InvalidOperationException>(() => ownBinding.Set(font, Regular));
        Check(target.Applications == 0 && ownBinding.Resource is null, "Composition preflight refuses assignment without changing the binding.");
        target.Composing = false;
        ownBinding.Set(font, Regular);
        dispatcher.Access = false;
        Throws<InvalidOperationException>(() => font.Dispose());
        Throws<InvalidOperationException>(() => ownBinding.Set(null, Regular));
        Throws<InvalidOperationException>(() => ownBinding.Dispose());
        dispatcher.Access = true;
        Check(native.Disposals == 0 && target.Current is not null, "Wrong-thread failures preserve valid native ownership.");
        ownBinding.Dispose();
        font.Dispose();
        Check(native.Disposals == 1, "Correct-thread teardown remains possible after rejected calls.");

        using var bounded = new FontResourceScope(dispatcher);
        var fonts = new List<FontResource>();
        for (int i = 0; i < 4; i++) fonts.Add(Own(cache, bounded, new Native()));
        var rejectedBytes = Bytes(cache); var rejectedNative = new Native();
        Throws<FontResourceLimitException>(() => bounded.Adopt(rejectedBytes, rejectedNative));
        Check(rejectedBytes.FontBytes.Length == 35220 && rejectedNative.Disposals == 0, "Full attachment admission does not steal producer ownership.");
        rejectedBytes.Dispose(); rejectedNative.Dispose();
        fonts[0].Dispose();
        Own(cache, bounded, new Native()).Dispose();
        Check(fonts.Skip(1).All(fontResource => fontResource.AcceptsNewUses), "Retired registration capacity is reusable.");
    }

    private static void BindingLifetime()
    {
        var dispatcher = new Dispatcher();
        using var cache = new FontResourceCache();
        using var scope = new FontResourceScope(dispatcher);
        var nativeA = new Native(); var nativeB = new Native();
        var a = Own(cache, scope, nativeA); var b = Own(cache, scope, nativeB);
        var target = new Target();
        using var binding = new FontResourceBinding(scope, target);
        binding.Set(a, Regular);
        a.Dispose();
        binding.Set(a, Regular);
        Check(target.Applications == 2 && nativeA.Disposals == 0, "Remeasure of an already-bound closed resource preserves its pin.");
        Throws<NotSupportedException>(() => binding.Set(a, new(700)));
        Check(ReferenceEquals(binding.Resource, a), "Later typography changes cannot silently synthesize bold.");
        target.Composing = true;
        Throws<InvalidOperationException>(() => binding.Set(null, Regular));
        Check(ReferenceEquals(binding.Resource, a) && nativeA.Disposals == 0, "Normal null-clear obeys composition preflight.");
        target.Composing = false;
        target.DuringApply = pin => Check(nativeA.Disposals == 0 && pin is not null &&
            ReferenceEquals(pin.Registration, nativeB), "Replacement is pinned and installed before the old native registration can retire.");
        binding.Set(b, Regular);
        target.DuringApply = null;
        Check(nativeA.Disposals == 1 && ReferenceEquals(binding.Resource, b), "Successful replacement releases the previous pin only after native apply.");
        b.Dispose();
        scope.Dispose();
        Check(scope.Token.IsCancellationRequested && nativeB.Disposals == 0, "Scope closure preserves the last live native pin.");
        binding.Set(b, Regular);
        Check(nativeB.Disposals == 0, "Existing-use validation is distinct from scope/new-use eligibility.");
        target.Composing = true;
        binding.Dispose();
        Check(target.TerminalReleases == 1 && target.Current is null && nativeB.Disposals == 1,
            "Terminal unbind bypasses normal composition veto and retires the last native/measurement uses.");
        binding.Dispose();
        Check(target.TerminalReleases == 1, "Binding disposal is idempotent.");
    }

    private static void FailureRetention()
    {
        var dispatcher = new Dispatcher();
        using var cache = new FontResourceCache();
        using var scope = new FontResourceScope(dispatcher);
        var nativeA = new Native(); var nativeB = new Native();
        var a = Own(cache, scope, nativeA); var b = Own(cache, scope, nativeB);
        var target = new Target();
        var binding = new FontResourceBinding(scope, target);
        binding.Set(a, Regular);
        a.Dispose();
        target.FailAfterApply = true;
        Throws<IOException>(() => binding.Set(b, Regular));
        b.Dispose();
        Check(nativeA.Disposals == 0 && nativeB.Disposals == 0, "Partially failed apply retains both old and possibly-installed new pins.");
        Throws<InvalidOperationException>(() => binding.Set(null, Regular));
        target.FailRelease = true;
        Throws<IOException>(() => binding.Dispose());
        Check(nativeA.Disposals == 0 && nativeB.Disposals == 0, "Failed terminal unbind cannot release a registration still used by native code.");
        target.FailRelease = false;
        binding.Dispose();
        Check(nativeA.Disposals == 1 && nativeB.Disposals == 1 && target.Current is null, "Cleanup retry retires both uncertain resources after native references are cleared.");

        var bytes = Bytes(cache);
        var fragile = new Native { Fail = true };
        var resource = scope.Adopt(bytes, fragile);
        Throws<IOException>(() => resource.Dispose());
        Check(bytes.FontBytes.Length == 35220 && !resource.AcceptsNewUses, "Failed native unregister keeps encoded storage alive and rejects new users.");
        fragile.Fail = false;
        resource.Dispose();
        Check(fragile.Disposals == 2, "Explicit cleanup retries the failed native retirement.");
        Throws<ObjectDisposedException>(() => _ = bytes.FontBytes);
    }

    private static void ReentrancyAndCancellation()
    {
        var dispatcher = new Dispatcher();
        using var cache = new FontResourceCache();
        var scope = new FontResourceScope(dispatcher);
        var native = new Native();
        var font = Own(cache, scope, native);
        var target = new Target();
        using var binding = new FontResourceBinding(scope, target);
        target.DuringValidation = () => binding.Set(font, Regular);
        Throws<InvalidOperationException>(() => binding.Set(font, Regular));
        Check(target.Applications == 0, "Preflight cannot reenter a font binding commit.");
        target.DuringValidation = null;
        binding.Set(font, Regular);
        using var cancellation = scope.Token.Register(() => throw new InvalidOperationException("fixture cancellation callback"));
        Throws<AggregateException>(() => scope.Dispose());
        Check(scope.IsClosed && scope.Token.IsCancellationRequested && native.Disposals == 0,
            "Cancellation callback failure still closes resources without releasing a live binding.");
        binding.Dispose();
        Check(native.Disposals == 1, "Bound cleanup completes even after scope cancellation callback failure.");
        scope.Dispose();

        using var secondScope = new FontResourceScope(dispatcher);
        var secondNative = new Native();
        var secondFont = Own(cache, secondScope, secondNative);
        var secondTarget = new Target();
        using var secondBinding = new FontResourceBinding(secondScope, secondTarget);
        secondTarget.DuringValidation = secondFont.Dispose;
        Throws<ObjectDisposedException>(() => secondBinding.Set(secondFont, Regular));
        Check(secondTarget.Applications == 0 && secondNative.Disposals == 1,
            "A resource closed during native preflight cannot be pinned or installed afterward.");
        var recursiveBytes = Bytes(cache);
        var recursiveNative = new Native();
        var recursive = secondScope.Adopt(recursiveBytes, recursiveNative);
        recursiveNative.BeforeDispose = recursive.Dispose;
        Throws<InvalidOperationException>(() => recursive.Dispose());
        Check(recursiveBytes.FontBytes.Length > 0, "Reentrant native retirement fails without abandoning borrowed storage.");
        recursiveNative.BeforeDispose = null;
        recursive.Dispose();
        Throws<ObjectDisposedException>(() => _ = recursiveBytes.FontBytes);
    }

    private static void RepeatedRetirement()
    {
        var dispatcher = new Dispatcher();
        using var cache = new FontResourceCache();
        for (int cycle = 0; cycle < 110; cycle++)
        {
            var native = new Native();
            using (var scope = new FontResourceScope(dispatcher))
            {
                var resource = Own(cache, scope, native);
                using var binding = new FontResourceBinding(scope, new Target());
                binding.Set(resource, Regular);
                resource.Dispose();
                binding.Set(resource, Regular);
                binding.Set(null, new(700));
                Check(native.Disposals == 1, "Clearing the custom font allows the target's baseline bold face without synthesis.");
            }
            cache.ClearUnused();
            Check(cache.Statistics == default, "Repeated attachment/resource/binding lifetime leaves no encoded pins.");
        }
    }

    private static void LoadCancellation()
    {
        var dispatcher = new Dispatcher();
        using var cache = new FontResourceCache();
        using var scope = new FontResourceScope(dispatcher);
        var errors = new List<Exception>();
        using (var cancellation = new CancellationTokenSource())
        using (var load = new FontResourceLoad(scope, cancellation.Token, errors.Add))
        {
            Task.Run(cancellation.Cancel).GetAwaiter().GetResult();
            Throws<OperationCanceledException>(() => load.Task.GetAwaiter().GetResult());
            var native = new Native();
            var late = Own(cache, scope, native);
            Check(!load.TryComplete(OperationResult<FontResource>.Completed(late)) && native.Disposals == 1,
                "Externally canceled loads dispose their late ready resource on the UI completion context.");
        }
        using (var load = new FontResourceLoad(scope, default, errors.Add))
        {
            var native = new Native();
            var ready = Own(cache, scope, native);
            Check(load.TryComplete(OperationResult<FontResource>.Completed(ready)), "A current native load transfers a ready owner once.");
            load.Dispose();
            Check(ReferenceEquals(load.Task.Result.Value, ready) && ready.AcceptsNewUses && native.Disposals == 0,
                "Load disposal cannot revoke a resource already delivered to its caller.");
            Check(!load.TryComplete(OperationResult<FontResource>.Completed(ready)) && native.Disposals == 0,
                "A repeated successful result cannot destroy the caller's live resource.");
            ready.Dispose();
        }
        using (var foreign = new FontResourceScope(dispatcher))
        using (var load = new FontResourceLoad(scope, default, errors.Add))
        {
            var native = new Native();
            var ready = Own(cache, foreign, native);
            Throws<ArgumentException>(() => load.TryComplete(OperationResult<FontResource>.Completed(ready)));
            Check(ready.AcceptsNewUses && native.Disposals == 0, "Rejected foreign completion preserves producer ownership.");
            ready.Dispose();
        }
        using (var load = new FontResourceLoad(scope, default, errors.Add))
        {
            load.Dispose();
            load.TryComplete(OperationResult<FontResource>.Failed(new IOException("late native load")));
            Check(errors.Count == 1 && errors[0].Message == "late native load", "Native errors after cancellation remain explicit.");
            var native = new Native { Fail = true };
            var late = Own(cache, scope, native);
            load.TryComplete(OperationResult<FontResource>.Completed(late));
            Check(errors.Count == 2 && errors[1] is FontResourceRetirementException retry &&
                ReferenceEquals(retry.Resource, late) && !late.AcceptsNewUses,
                "Late unregister errors report the exact still-owned resource for a cleanup retry.");
            native.Fail = false;
            ((FontResourceRetirementException)errors[1]).Resource.Dispose();
        }
        int uiThread = Environment.CurrentManagedThreadId;
        for (int attempt = 0; attempt < 64; attempt++)
        {
            using var cancellation = new CancellationTokenSource();
            using var load = new FontResourceLoad(scope, cancellation.Token, errors.Add);
            using var barrier = new Barrier(2);
            var native = new Native
            {
                BeforeDispose = () => Check(Environment.CurrentManagedThreadId == uiThread, "A losing published-resource race retires on the native owner thread.")
            };
            var ready = Own(cache, scope, native);
            var cancel = Task.Run(() => { barrier.SignalAndWait(); cancellation.Cancel(); });
            barrier.SignalAndWait();
            bool transferred = load.TryComplete(OperationResult<FontResource>.Completed(ready));
            cancel.GetAwaiter().GetResult();
            if (transferred)
            {
                Check(load.Task.IsCompletedSuccessfully && ReferenceEquals(load.Task.Result.Value, ready) && native.Disposals == 0,
                    "Successful atomic publication transfers ownership despite concurrent later cancellation.");
                ready.Dispose();
            }
            else
            {
                Check(load.Task.IsCanceled && native.Disposals == 1,
                    "Cancellation winning publication retires the losing ready resource exactly once.");
            }
            Check(native.Disposals == 1, "Publication/cancellation has exactly one final native owner.");
        }
        using var closing = new FontResourceScope(dispatcher);
        using var pending = new FontResourceLoad(closing, default, errors.Add);
        closing.Dispose();
        Throws<OperationCanceledException>(() => pending.Task.GetAwaiter().GetResult());
        Check(pending.Token.IsCancellationRequested, "Attachment disposal cancels all service load delivery.");
    }

    private sealed class Dispatcher : IUiDispatcher
    {
        public bool Access = true;
        public bool CheckAccess() => Access;
        public void Post(Action action) => throw new NotSupportedException("This synchronous fixture has no native message loop.");
    }
    private sealed class Native : IFontNativeRegistration
    {
        public int Disposals;
        public bool Fail;
        public Action? BeforeDispose;
        private bool retired;
        public void Dispose()
        {
            if (retired) return;
            Disposals++;
            BeforeDispose?.Invoke();
            if (Fail) throw new IOException("Fixture native unregister failed.");
            retired = true;
        }
    }
    private sealed class PartiallyRetiredNative : IFontNativeRegistration
    {
        public int FirstReleased, SecondReleased;
        public void Dispose()
        {
            if (FirstReleased == 0)
            {
                FirstReleased++;
                throw new IOException("Second native handle is temporarily busy.");
            }
            if (SecondReleased == 0) SecondReleased++;
        }
    }
    private sealed class Target : IFontResourceBindingTarget
    {
        public FontResourcePin? Current;
        public int Validations, Applications, TerminalReleases;
        public bool Composing, FailAfterApply, FailRelease;
        public Action? DuringValidation;
        public Action<FontResourcePin?>? DuringApply;
        public void ValidateFontResource(FontResource? resource, FontResourceStyle effectiveStyle)
        {
            Validations++;
            if (Composing) throw new InvalidOperationException("Native composition cannot be interrupted.");
            DuringValidation?.Invoke();
        }
        public void ApplyFontResource(FontResourcePin? pin, FontResourceStyle effectiveStyle)
        {
            Applications++;
            DuringApply?.Invoke(pin);
            Current = pin;
            if (FailAfterApply) throw new IOException("Native mutation failed after partially installing its new font.");
        }
        public void ReleaseFontReferences()
        {
            TerminalReleases++;
            if (FailRelease) throw new IOException("Native font references could not be cleared.");
            Current = null;
        }
    }
}

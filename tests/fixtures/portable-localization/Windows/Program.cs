using LocalizationConsumer;

if (args is ["--expect-missing-bundle", var culture])
    LocalizationProof.VerifyMissingBundle(culture);
else if (args.Length == 0)
    LocalizationProof.Verify();
else
    throw new ArgumentException("Usage: LocalizationProof.Windows [--expect-missing-bundle en|de|ar]");
Console.WriteLine("PASS: packaged localization cultures and fallback.");

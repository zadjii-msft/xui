using System.Text.Json;
using Microsoft.CodeAnalysis;
using Xui.Generator;

internal static partial class Program
{
    private static void TestControlContracts()
    {
        using var stream = typeof(Program).Assembly.GetManifestResourceStream("GeneratorTests.Features.json")!;
        using var manifest = JsonDocument.Parse(stream);
        foreach (var entry in manifest.RootElement.GetProperty("markup").EnumerateObject())
        {
            string kind = entry.Name;
            var contract = entry.Value;
            int children = contract.GetProperty("children").GetInt32();
            bool stack = contract.GetProperty("stack").GetBoolean();
            string arguments = stack ? "" : kind == "Content" ? "External" : "\"Contract\"";
            string body = children == 0 ? ";" : " { " +
                string.Concat(Enumerable.Repeat("Text(\"Child\"); ", Math.Max(0, children))) + "}";
            string Source(string args, string ending) =>
                $"component Contract {{ param global::Xui.Element External; view {{ {kind}({args}){ending} }} }}";
            string source = Source(arguments, body);
            var parsed = XuiSourceParser.Parse(source);
            Assert(parsed.Success, $"Declared markup constructor {kind} parses.");
            var (_, compilation) = Generate(new File(@"C:\fixture\Contract.xui", source));
            Assert(!compilation.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error),
                $"Declared constructor {kind} generates its typed adapter: " + string.Join("\n", compilation.GetDiagnostics()));
            foreach (var arg in contract.GetProperty("arguments").EnumerateObject())
                Assert(parsed.Root!.SupportedArguments.Contains(arg.Name), $"{kind}.{arg.Name} reaches Designer metadata.");
            if (children > 0)
            {
                var invalid = XuiSourceParser.Parse(Source(arguments, " {}"));
                Assert(!invalid.Success && invalid.Diagnostics.Any(d => d.Message.Contains($"exactly {children}")),
                    $"{kind} rejects invalid child cardinality.");
            }
            if (!contract.GetProperty("arguments").TryGetProperty("change", out _))
            {
                string invalidSource = Source(arguments + (arguments.Length == 0 ? "" : ", ") + "change: Handler", body);
                var invalid = XuiSourceParser.Parse(invalidSource);
                Assert(!invalid.Success && invalid.Diagnostics.Any(d => d.Message.Contains("Unknown argument 'change'") &&
                    d.Span.Start == invalidSource.IndexOf("change:", StringComparison.Ordinal)),
                    $"{kind} rejects unsupported events at the authored source location.");
            }
        }
        foreach (var entry in manifest.RootElement.GetProperty("coverage").EnumerateObject())
        {
            if (entry.Value.GetProperty("markup").GetBoolean()) continue;
            var invalid = XuiSourceParser.Parse($"component Unsupported {{ view {{ {entry.Name}(\"x\"); }} }}");
            Assert(!invalid.Success && invalid.Diagnostics.Any(d => d.Message.Contains("not supported in .xui")),
                $"Explicitly unsupported language surface {entry.Name} stays unsupported.");
        }
    }
}

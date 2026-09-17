using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;
using Xui.Generator;

namespace Xui.Designer;

internal sealed record PreviewCompilation(byte[]? Assembly, string Diagnostics)
{
    public bool Success => Assembly is not null;
}

internal static class PreviewCompiler
{
    private const string SourcePath = "Preview.xui";
    private const int MaximumSourceLength = 65536;
    private static readonly object ReferenceLock = new();
    private static MetadataReference[]? cachedReferences;

    public static PreviewCompilation Compile(string source, CancellationToken cancellation = default)
    {
        cancellation.ThrowIfCancellationRequested();
        if (source is null)
            return Failure("Supply a self-contained XUI component.");
        if (source.Length > MaximumSourceLength)
            return Failure($"Source must not exceed {MaximumSourceLength} UTF-16 code units.");

        source = source.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n');
        var text = SourceText.From(source, Encoding.UTF8);
        for (int i = 0; i < source.Length; i++)
        {
            cancellation.ThrowIfCancellationRequested();
            if (source[i] == '\0')
                return Failure("Remove the embedded NUL character.", text, i);
            if (char.IsHighSurrogate(source[i]))
            {
                if (i + 1 == source.Length || !char.IsLowSurrogate(source[i + 1]))
                    return Failure("Replace the unpaired Unicode surrogate with valid Unicode text.", text, i);
                i++;
            }
            else if (char.IsLowSurrogate(source[i]))
                return Failure("Replace the unpaired Unicode surrogate with valid Unicode text.", text, i);
        }

        var parseOptions = CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Latest);
        var input = CSharpCompilation.Create(
            "Xui.Designer.Preview." + Guid.NewGuid().ToString("N"),
            references: References(cancellation),
            options: new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
                optimizationLevel: OptimizationLevel.Release,
                nullableContextOptions: NullableContextOptions.Enable));
        GeneratorDriver driver = CSharpGeneratorDriver.Create(
            generators: [new XuiGenerator().AsSourceGenerator()],
            additionalTexts: [new PreviewFile(text)],
            parseOptions: parseOptions);
        driver = driver.RunGeneratorsAndUpdateCompilation(input, out var generated, out var generatorDiagnostics, cancellation);
        cancellation.ThrowIfCancellationRequested();
        var diagnostics = generatorDiagnostics.ToList();
        diagnostics.AddRange(generated.GetDiagnostics(cancellation));
        if (diagnostics.Any(d => d.Severity == DiagnosticSeverity.Error))
            return new(null, FormatDiagnostics(diagnostics, cancellation));

        var windowType = generated.GetTypeByMetadataName("Xui.Window");
        var elementType = generated.GetTypeByMetadataName("Xui.Element");
        var components = new List<(INamedTypeSymbol Type, IMethodSymbol Constructor)>();
        foreach (var tree in generated.SyntaxTrees)
        {
            var model = generated.GetSemanticModel(tree);
            foreach (var declaration in tree.GetRoot(cancellation).DescendantNodes().OfType<ClassDeclarationSyntax>())
            {
                cancellation.ThrowIfCancellationRequested();
                if (declaration.Parent is not (BaseNamespaceDeclarationSyntax or CompilationUnitSyntax) ||
                    model.GetDeclaredSymbol(declaration, cancellation) is not INamedTypeSymbol type)
                    continue;
                if (!type.GetMembers("Root").OfType<IPropertySymbol>().Any(p =>
                    p.DeclaredAccessibility == Accessibility.Public && !p.IsStatic && IsElement(p.Type, elementType)))
                    continue;
                foreach (var constructor in type.InstanceConstructors)
                {
                    var parameters = constructor.Parameters;
                    if (constructor.DeclaredAccessibility == Accessibility.Public && parameters.Length >= 2 &&
                        SymbolEqualityComparer.Default.Equals(parameters[0].Type, windowType) &&
                        parameters[0].Name == "window" && parameters[^1].Name == "attach" &&
                        parameters[^1].Type.SpecialType == SpecialType.System_Boolean && parameters[^1].IsOptional)
                        components.Add((type, constructor));
                }
            }
        }

        if (components.Count != 1)
            return new(null, Join(FormatDiagnostics(diagnostics, cancellation),
                Failure("Supply exactly one self-contained component with a generated Root and Window constructor.").Diagnostics));

        var component = components[0];
        var required = component.Constructor.Parameters.Skip(1).SkipLast(1).Where(p => !p.IsOptional).ToArray();
        if (required.Length != 0)
        {
            // Parameter properties retain the generator's authored-source mapping.
            var location = component.Type.GetMembers(required[0].Name).OfType<IPropertySymbol>()
                .FirstOrDefault()?.Locations.FirstOrDefault() ?? component.Type.Locations.FirstOrDefault();
            var descriptor = new DiagnosticDescriptor("XUIPREVIEW002", "Component needs parameters",
                "Preview cannot supply required component parameters: {0}. Remove the param declarations and use initialized state or constants.",
                "Xui.Designer", DiagnosticSeverity.Error, true);
            diagnostics.Add(Diagnostic.Create(descriptor, location, string.Join(", ", required.Select(p => p.Name))));
            return new(null, FormatDiagnostics(diagnostics, cancellation));
        }

        string componentName = component.Type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        // This is a compilation service, not a sandbox. Authored C# remains trusted code.
        var wrapper = CSharpSyntaxTree.ParseText($$"""
            #nullable enable
            namespace Xui.Designer
            {
                public static class GeneratedPreview
                {
                    public static object Build(global::Xui.Window window)
                    {
                        var component = new {{componentName}}(window, attach: false);
                        global::Xui.Element root = component.Root;
                        window.SetContent(window.Stack().Add(root, 1));
                        return component;
                    }
                }
            }
            """, parseOptions, "GeneratedPreview.g.cs", Encoding.UTF8, cancellation);
        var compilation = generated.AddSyntaxTrees(wrapper);
        using var output = new MemoryStream();
        var emitted = compilation.Emit(output, cancellationToken: cancellation);
        cancellation.ThrowIfCancellationRequested();
        diagnostics.AddRange(emitted.Diagnostics);
        return new(emitted.Success ? output.ToArray() : null, FormatDiagnostics(diagnostics, cancellation));
    }

    private static bool IsElement(ITypeSymbol type, INamedTypeSymbol? element)
    {
        for (var current = type as INamedTypeSymbol; current is not null; current = current.BaseType)
            if (SymbolEqualityComparer.Default.Equals(current, element))
                return true;
        return false;
    }

    private static MetadataReference[] References(CancellationToken cancellation)
    {
        lock (ReferenceLock)
        {
            cancellation.ThrowIfCancellationRequested();
            if (cachedReferences is not null)
                return cachedReferences;
            string platforms = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string
                ?? throw new InvalidOperationException("Runtime compilation requires trusted platform assembly paths.");
            var references = new List<MetadataReference>();
            foreach (string path in platforms.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
                .Append(typeof(global::Xui.Window).Assembly.Location).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                cancellation.ThrowIfCancellationRequested();
                references.Add(MetadataReference.CreateFromFile(path));
            }
            cancellation.ThrowIfCancellationRequested();
            return cachedReferences = references.ToArray();
        }
    }

    private static string FormatDiagnostics(IEnumerable<Diagnostic> diagnostics, CancellationToken cancellation)
    {
        var lines = new List<string>();
        foreach (var diagnostic in diagnostics)
        {
            cancellation.ThrowIfCancellationRequested();
            if (diagnostic.Severity == DiagnosticSeverity.Hidden)
                continue;
            var span = diagnostic.Location.GetMappedLineSpan();
            string position = span.IsValid
                ? $"{span.Path}({span.StartLinePosition.Line + 1},{span.StartLinePosition.Character + 1}): "
                : "";
            lines.Add($"{position}{diagnostic.Severity.ToString().ToLowerInvariant()} {diagnostic.Id}: {diagnostic.GetMessage()}");
        }
        return string.Join(Environment.NewLine, lines.Distinct(StringComparer.Ordinal));
    }

    private static PreviewCompilation Failure(string message, SourceText? source = null, int offset = 0)
    {
        var position = source?.Lines.GetLinePosition(offset) ?? new LinePosition(0, 0);
        return new(null, $"{SourcePath}({position.Line + 1},{position.Character + 1}): error XUIPREVIEW001: {message}");
    }

    private static string Join(string first, string second) =>
        first.Length == 0 ? second : first + Environment.NewLine + second;

    private sealed class PreviewFile(SourceText text) : AdditionalText
    {
        public override string Path => SourcePath;

        public override SourceText GetText(CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return text;
        }
    }
}

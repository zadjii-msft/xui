using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using Xui.Designer;

internal static class Program
{
    private static int assertions;
    private const string Counter = """
        namespace Example;
        component Counter {
            state int Count = 0;
            view {
                VStack(spacing: 8, padding: 16) {
                    Text($"Count: {Count}");
                    Button("Increment", click: Increment);
                }
            }
            code csharp {
                public void Increment() => Count++;
            }
        }
        """;

    private static int Main()
    {
        try
        {
            NativeLibrary.SetDllImportResolver(typeof(Xui.Window).Assembly, (_, _, _) =>
                throw new InvalidOperationException("Compiler tests must not load native XUI."));
            TestComponents();
            TestDiagnostics();
            TestInput();
            TestCancellation();
            TestMetadata();
            Console.WriteLine($"Designer compiler assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    private static void TestComponents()
    {
        Success(Counter);
        Success("""
            component Styled {
                resources { Accent: theme(light: 0x123456, dark: 0x654321); }
                style AccentButton for Button {
                    background: resource(Accent);
                    cornerRadius: 4;
                    when hovered { background: 0xAABBCC; }
                }
                view { VStack() { Button("Styled", style: AccentButton); } }
            }
            """);
        Success("""component Standalone { view { Text("Non-stack root"); } }""");
        Success("""component GridRoot { view { Grid("Root") { Text("Child"); } } }""");
        Success("""component @class { view { Button("Escaped name"); } }""");
        Success("""namespace @class.@namespace; component @event { view { Text("Escaped namespace"); } }""");
        Success(Counter.ReplaceLineEndings("\r"));
        Success(Counter.ReplaceLineEndings("\r\n"));
    }

    private static void TestDiagnostics()
    {
        string invalidXui = """
            component Broken {
                view {
                    Unknown("Invalid");
                }
            }
            """;
        string invalidCSharp = """
            component Broken {
                state int Count = "not an integer";
                view { Text("Error"); }
            }
            """;
        var xui = Failure(invalidXui, "XUI001");
        Mapped(xui, 3);
        var csharp = Failure(invalidCSharp, "CS0029");
        Mapped(csharp, 2);
        var handler = Failure("""
            component Broken {
                view { Button("Run", click: MissingHandler); }
            }
            """, "CS");
        Mapped(handler, 2);
        var code = Failure("""
            component Broken {
                view { Text("Error"); }
                code csharp {
                    void BrokenMethod() { Missing(); }
                }
            }
            """, "CS0103");
        Mapped(code, 4);
        var parameters = Failure("""
            component NeedsInput {
                param string Title;
                param int Count;
                view { Text(Title); }
            }
            """, "XUIPREVIEW002");
        Assert(parameters.Diagnostics.Contains("Title, Count", StringComparison.Ordinal), parameters.Diagnostics);
        Assert(parameters.Diagnostics.Contains("initialized state", StringComparison.Ordinal), parameters.Diagnostics);
        Mapped(parameters, 2);
        foreach (var newline in new[] { "\r", "\r\n" })
        {
            Assert(Failure(invalidXui.ReplaceLineEndings(newline), "XUI001").Diagnostics == xui.Diagnostics,
                "XUI mapping must be unchanged after newline normalization.");
            Assert(Failure(invalidCSharp.ReplaceLineEndings(newline), "CS0029").Diagnostics == csharp.Diagnostics,
                "C# mapping must be unchanged after newline normalization.");
        }
        Failure("""component First { view { Text("A"); } } component Second { view { Text("B"); } }""", "XUI001");
        Failure(Counter.Replace("Example", "Xui.Designer.GeneratedPreview", StringComparison.Ordinal), "CS0101");
    }

    private static void TestInput()
    {
        Failure(null!, "Supply");
        Failure("", "XUI001");
        Failure(" \n\t", "XUI001");
        Failure(Counter + "\0", "NUL");
        Failure(Counter + "\uD800", "Unicode");
        Failure(Counter + "\uDC00", "Unicode");
        Failure(Counter + "\uD800x", "Unicode");
        Failure(new string(' ', 65537), "65536");
        Success(Counter.PadRight(65536));
        Failure((Counter + new string('\r', 65536)).Replace("\r", "\r\n", StringComparison.Ordinal), "65536");
        Success("""component Emoji { view { Text("😀"); } }""");
    }

    private static void TestCancellation()
    {
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        foreach (var source in new[] { Counter, "", new string(' ', 65537) })
        {
            try
            {
                PreviewCompiler.Compile(source, cancellation.Token);
                throw new InvalidOperationException("Expected cancellation.");
            }
            catch (OperationCanceledException error)
            {
                Assert(error.CancellationToken == cancellation.Token, "Cancellation token must be preserved.");
            }
        }
        Success(Counter);
    }

    private static void TestMetadata()
    {
        var first = Success(Counter);
        var second = Success(Counter);
        using var firstReader = new PEReader(new MemoryStream(first.Assembly!));
        using var secondReader = new PEReader(new MemoryStream(second.Assembly!));
        var metadata = firstReader.GetMetadataReader();
        var otherMetadata = secondReader.GetMetadataReader();
        Assert(metadata.GetString(metadata.GetAssemblyDefinition().Name) !=
            otherMetadata.GetString(otherMetadata.GetAssemblyDefinition().Name), "Builds need distinct assembly identities.");

        // Inspect PE metadata only: do not load or execute authored assemblies.
        var wrapper = metadata.TypeDefinitions.Select(metadata.GetTypeDefinition).Single(t =>
            metadata.GetString(t.Namespace) == "Xui.Designer" && metadata.GetString(t.Name) == "GeneratedPreview");
        Assert((wrapper.Attributes & TypeAttributes.Public) != 0 &&
            (wrapper.Attributes & TypeAttributes.Abstract) != 0 &&
            (wrapper.Attributes & TypeAttributes.Sealed) != 0, "Wrapper must be a public static class.");
        var build = wrapper.GetMethods().Select(metadata.GetMethodDefinition)
            .Single(m => metadata.GetString(m.Name) == "Build");
        Assert((build.Attributes & MethodAttributes.Public) != 0 && (build.Attributes & MethodAttributes.Static) != 0,
            "Build must be public static.");
        var signature = metadata.GetBlobReader(build.Signature);
        Assert(!signature.ReadSignatureHeader().IsInstance && signature.ReadCompressedInteger() == 1,
            "Build must take one argument.");
        Assert(signature.ReadSignatureTypeCode() == SignatureTypeCode.Object, "Build must return object.");
        Assert(signature.ReadSignatureTypeCode() == SignatureTypeCode.TypeHandle, "Build argument must be a named type.");
        var windowHandle = signature.ReadTypeHandle();
        Assert(windowHandle.Kind == HandleKind.TypeReference, "Window must come from the runtime assembly.");
        var windowType = metadata.GetTypeReference((TypeReferenceHandle)windowHandle);
        Assert(metadata.GetString(windowType.Namespace) == "Xui" && metadata.GetString(windowType.Name) == "Window",
            "Build argument must be Xui.Window.");
        var getRoot = wrapper.GetMethods().Select(metadata.GetMethodDefinition)
            .Single(m => metadata.GetString(m.Name) == "Root");
        Assert((getRoot.Attributes & MethodAttributes.Public) != 0 && (getRoot.Attributes & MethodAttributes.Static) != 0,
            "Root must be public static.");
        var rootSignature = metadata.GetBlobReader(getRoot.Signature);
        Assert(!rootSignature.ReadSignatureHeader().IsInstance && rootSignature.ReadCompressedInteger() == 1,
            "Root must take one component argument.");
        Assert(rootSignature.ReadSignatureTypeCode() == SignatureTypeCode.TypeHandle, "Root must return a named type.");
        var rootType = metadata.GetTypeReference((TypeReferenceHandle)rootSignature.ReadTypeHandle());
        Assert(metadata.GetString(rootType.Namespace) == "Xui" && metadata.GetString(rootType.Name) == "Element",
            "Root must return Xui.Element.");
        Assert(rootSignature.ReadSignatureTypeCode() == SignatureTypeCode.Object, "Root must accept the built component.");
        var component = metadata.TypeDefinitions.Select(metadata.GetTypeDefinition).Single(t =>
            metadata.GetString(t.Namespace) == "Example" && metadata.GetString(t.Name) == "Counter");
        Assert(component.GetProperties().Select(metadata.GetPropertyDefinition)
            .Any(p => metadata.GetString(p.Name) == "Count"), "State must survive compilation.");
        Assert(component.GetMethods().Select(metadata.GetMethodDefinition)
            .Any(m => metadata.GetString(m.Name) == "Increment"), "Handler must survive compilation.");
        Assert(firstReader.GetMethodBody(build.RelativeVirtualAddress).GetILBytes() is { Length: > 0 },
            "Wrapper must have a compiled body.");
    }

    private static PreviewCompilation Success(string source)
    {
        var result = PreviewCompiler.Compile(source);
        Assert(result.Success && result.Assembly is { Length: > 0 }, result.Diagnostics);
        return result;
    }

    private static PreviewCompilation Failure(string source, string diagnostic)
    {
        var result = PreviewCompiler.Compile(source);
        Assert(!result.Success && result.Assembly is null, "Invalid input must not produce an assembly.");
        Assert(result.Diagnostics.Contains(diagnostic, StringComparison.Ordinal), result.Diagnostics);
        return result;
    }

    private static void Mapped(PreviewCompilation result, int line) =>
        Assert(Regex.IsMatch(result.Diagnostics, $@"Preview\.xui\({line},[1-9][0-9]*\): error "),
            "Expected authored line and column: " + result.Diagnostics);

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
        assertions++;
    }
}

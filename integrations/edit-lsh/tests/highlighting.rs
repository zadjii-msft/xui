use std::path::Path;
use std::sync::Once;

use lsh::compiler::{Compiler, Generator};
use lsh::runtime::Runtime;
use stdext::arena::scratch_arena;
use stdext::glob::glob_match;

const GRAMMAR: &str = include_str!("../xui.lsh");

fn init() {
    static INIT: Once = Once::new();
    INIT.call_once(|| stdext::arena::init(128 * 1024 * 1024).unwrap());
}

fn highlight(source: &str) -> Vec<Vec<String>> {
    init();
    let arena = scratch_arena(None);
    let mut compiler = Compiler::new(&arena);
    compiler.parse("xui.lsh", GRAMMAR).unwrap();
    let assembly = compiler.assemble().unwrap();
    let entry = assembly
        .entrypoints
        .iter()
        .find(|ep| ep.name == "xui")
        .unwrap();
    let charsets: Vec<_> = assembly.charsets.iter().map(|cs| cs.serialize()).collect();
    let mut runtime = Runtime::new(
        &assembly.instructions,
        &assembly.strings,
        &charsets,
        entry.address as u32,
    );
    source
        .lines()
        .map(|line| {
            let line_arena = scratch_arena(None);
            let spans = runtime.parse_next_line::<u32>(&line_arena, line.as_bytes());
            assert_eq!(spans.first().unwrap().start, 0);
            assert_eq!(spans.last().unwrap().start, line.len());
            let mut kinds = vec!["other".to_string(); line.len()];
            for pair in spans.windows(2) {
                assert!(pair[0].start <= pair[1].start);
                assert!(line.is_char_boundary(pair[0].start));
                assert!(line.is_char_boundary(pair[1].start));
                let kind = assembly
                    .highlight_kinds
                    .iter()
                    .find(|kind| kind.value == pair[0].kind)
                    .unwrap();
                kinds[pair[0].start..pair[1].start].fill(kind.identifier.to_string());
            }
            kinds
        })
        .collect()
}

fn expect(source: &str, checks: &[(usize, &str, &str)]) {
    let kinds = highlight(source);
    let lines: Vec<_> = source.lines().collect();
    for &(line, text, kind) in checks {
        let start = lines[line]
            .find(text)
            .unwrap_or_else(|| panic!("Missing {text:?} on line {line}"));
        assert!(
            kinds[line][start..start + text.len()]
                .iter()
                .all(|actual| actual == kind),
            "line {line}, {text:?}: expected {kind}, got {:?}",
            &kinds[line][start..start + text.len()]
        );
    }
}

#[test]
fn reveal_node_and_arguments() {
    expect(
        "view {\nReveal(\"Find\", open: FindOpen, duration: 180) {\nHStack() { TextInput(\"Find\"); }\n}\n}",
        &[
            (1, "Reveal", "storage.type"),
            (1, "open", "variable"),
            (1, "duration", "variable"),
            (2, "TextInput", "storage.type"),
        ],
    );
}

#[test]
fn standalone_registration_and_builtin_compatibility() {
    init();
    let arena = scratch_arena(None);
    let mut generator = Generator::new(&arena);
    generator
        .read_file(&Path::new(env!("CARGO_MANIFEST_DIR")).join("xui.lsh"))
        .unwrap();
    generator
        .read_directory(lsh::compiler::builtin_definitions_path())
        .unwrap();
    let assembly = generator.assemble().unwrap();
    let entry = assembly
        .entrypoints
        .iter()
        .find(|ep| ep.name == "xui")
        .unwrap();
    assert_eq!(entry.display_name, "XUI");
    for path in ["Counter.xui", "app/Counter.xui", r"app\Counter.xui"] {
        assert!(
            entry
                .paths
                .iter()
                .any(|pattern| glob_match(pattern.as_bytes(), path.as_bytes()))
        );
    }
    assert!(
        !entry
            .paths
            .iter()
            .any(|pattern| glob_match(pattern.as_bytes(), b"Counter.cs"))
    );
}

#[test]
fn bundled_grammars_compile_together() {
    init();
    let arena = scratch_arena(None);
    let mut generator = Generator::new(&arena);
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    generator.read_file(&root.join("xui.lsh")).unwrap();
    generator.read_directory(&root.join("upstream")).unwrap();
    generator
        .read_file(&lsh::compiler::builtin_definitions_path().join("utility.lsh"))
        .unwrap();
    let assembly = generator.assemble().unwrap();
    for name in ["xui", "c", "cpp", "csharp", "rust"] {
        assert!(assembly.entrypoints.iter().any(|entry| entry.name == name));
    }
}

#[test]
fn declarations_styles_and_current_controls() {
    expect(
        r#"namespace Demo;
component Counter {
    param string Caption;
    state int Count = 0;
    resources { Accent: theme(light: 0xB42318, dark: 0x8F1D16); }
    style Danger for Button basedOn Base {
        part text { foreground: resource(Accent); }
        when hovered { cornerRadius: 2; }
    }
    view { VStack(spacing: 8) {
        RangeInput("Volume", currentValue: Count, reversed: false);
        Progress("Progress", progressState: global::Xui.ProgressState.Paused);
    } }
}"#,
        &[
            (0, "namespace", "keyword.other"),
            (1, "component", "keyword.other"),
            (1, "Counter", "storage.type"),
            (2, "param", "keyword.other"),
            (2, "string", "storage.type"),
            (3, "state", "keyword.other"),
            (3, "int", "storage.type"),
            (3, "0", "constant.numeric"),
            (4, "resources", "keyword.other"),
            (4, "Accent", "variable"),
            (4, "theme", "method"),
            (4, "light", "variable"),
            (4, "0xB42318", "constant.numeric"),
            (5, "basedOn", "keyword.other"),
            (5, "Danger", "storage.type"),
            (5, "Base", "storage.type"),
            (5, "Button", "storage.type"),
            (6, "part", "keyword.other"),
            (6, "foreground", "variable"),
            (6, "resource", "method"),
            (7, "when", "keyword.other"),
            (9, "VStack", "storage.type"),
            (10, "RangeInput", "storage.type"),
            (10, "currentValue", "variable"),
            (10, "false", "constant.language"),
            (11, "Progress", "storage.type"),
            (11, "global", "other"),
        ],
    );
}

#[test]
fn winui_parity_controls() {
    for control in [
        "ToggleSwitch",
        "ToggleButton",
        "ProgressRing",
        "CheckBox",
        "HyperlinkButton",
        "SelectorBar",
        "InfoBadge",
        "MenuBar",
    ] {
        for indent in ["", "    "] {
            let source = format!(
                "component Example {{\n{indent}view {{ VStack() {{\n{indent}{control}(\"Example\");\n{indent}}} }}\n}}"
            );
            expect(
                &source,
                &[(2, control, "storage.type"), (2, "Example", "string")],
            );
        }
    }
}

#[test]
fn csharp_nesting_and_same_line_return_to_xui() {
    expect(
        r#"component Demo {
state int Value = Make(() => { var view = 1; return view; });
code /* header */ csharp {
    void Run() { var component = "view }"; if (true) { Call(() => { return new[] { 1, 2 }; }); } }
    void Other() { var state = 1; /* } */
        var view = @"text "" } "";
still string";
    }
} view { Text("after"); }
}"#,
        &[
            (1, "view", "other"),
            (1, "return", "keyword.control"),
            (2, "header", "comment"),
            (2, "csharp", "storage.type"),
            (3, "void", "storage.type"),
            (3, "component", "other"),
            (3, "view }", "string"),
            (3, "if", "keyword.control"),
            (3, "Call", "method"),
            (4, "state", "other"),
            (4, "/* } */", "comment"),
            (6, "still string", "string"),
            (8, "view", "keyword.other"),
            (8, "Text", "storage.type"),
        ],
    );
}

#[test]
fn strings_comments_interpolation_and_raw_delimiters() {
    expect(
        r#####"component Strings {
state string Text = "escaped \" // }";
state string Interpolated = $"Count: {Format("nested", new[] { 1 })} {{literal}}";
state string Verbatim = $@"first {Format("nested")}
second ""quoted""";
state string Reverse = @$"value {1}";
state string Raw = $$$""""
raw """ // not the closing delimiter
{{{opaque}}}
"""";
/* multiple

lines */ view { Text("after", id: "ok"); }
}"#####,
        &[
            (1, "escaped", "string"),
            (1, "// }", "string"),
            (2, "Count:", "string"),
            (2, "Format", "method"),
            (2, "nested", "string"),
            (2, "1", "constant.numeric"),
            (2, "{{literal}}", "string"),
            (3, "Format", "method"),
            (4, "second", "string"),
            (5, "1", "constant.numeric"),
            (7, "raw", "string"),
            (7, "//", "string"),
            (8, "opaque", "string"),
            (10, "multiple", "comment"),
            (12, "lines */", "comment"),
            (12, "view", "keyword.other"),
            (12, "Text", "storage.type"),
        ],
    );
}

#[test]
fn token_boundaries_numbers_and_unfinished_input() {
    expect(
        "componentName stateful viewer @component\nstate double Number = -1.25e+2;\nview { Text(\"x\", size: (0xFF, 0b10_01), padding: .5f); }\n// trailing comment",
        &[
            (0, "componentName", "other"),
            (0, "stateful", "other"),
            (0, "viewer", "other"),
            (0, "@component", "other"),
            (1, "1.25e+2", "constant.numeric"),
            (2, "0xFF", "constant.numeric"),
            (2, "0b10_01", "constant.numeric"),
            (2, ".5f", "constant.numeric"),
            (3, "// trailing comment", "comment"),
        ],
    );
    expect(
        "\"unfinished\nview\n/* unfinished\ncomment",
        &[
            (0, "unfinished", "string"),
            (1, "view", "keyword.other"),
            (3, "comment", "comment"),
        ],
    );
    for source in [
        "",
        "\n",
        "code",
        "code csharp {",
        "state",
        "view { Text(",
        "\"\"\"\nraw",
        "'x",
    ] {
        highlight(source);
    }
}

#[test]
fn multiline_headers_unicode_and_inline_raw_strings() {
    expect(
        concat!(
            "component /* name */\n",
            "Demo {\n",
            "style /* style */ Accent for TreeView basedOn /* base */ Base { }\n",
            "state string Raw = \"\"\"inline // string\"\"\";\n",
            "state string Wide = \"\"\"\"contains \"\"\" quotes\"\"\"\";\n",
            "code\n",
            "/* language */ csharp\n",
            "{ void Run() { var return\u{e9} = 1; } }\n",
            "view { Text(\"after\"); }\n",
            "}\n",
            "component\u{e9} state\u{e9} view\u{e9}\n"
        ),
        &[
            (0, "/* name */", "comment"),
            (1, "Demo", "storage.type"),
            (2, "Accent", "storage.type"),
            (2, "TreeView", "storage.type"),
            (2, "Base", "storage.type"),
            (3, "// string", "string"),
            (3, ";", "other"),
            (4, "quotes", "string"),
            (4, ";", "other"),
            (6, "csharp", "storage.type"),
            (7, "return\u{e9}", "other"),
            (8, "view", "keyword.other"),
            (10, "component\u{e9}", "other"),
            (10, "state\u{e9}", "other"),
            (10, "view\u{e9}", "other"),
        ],
    );
}

#[test]
fn contextual_keywords_in_resource_names_and_namespaces() {
    expect(
        "namespace Demo.state.view;\ncomponent Demo {\nresources { code: 0xFFFFFF; component: resource(code); view: 0; }\nview { Text(\"after\"); }\n}",
        &[
            (0, "namespace", "keyword.other"),
            (0, "state", "other"),
            (0, "view", "other"),
            (2, "code", "variable"),
            (2, "component", "variable"),
            (2, "view", "variable"),
            (3, "view", "keyword.other"),
            (3, "Text", "storage.type"),
        ],
    );
}

#[test]
fn designer_token_colors() {
    expect(
        concat!(
            "$\"Hello there, {Name}!\"\n",
            "$\"Result: {Format(Name, 0xFF)}!\"\n",
            "$@\"Hello {Name}, {{literal}}\"\n",
            "TextInput(\"Your name\", text: Name, change: Rename);\n",
            "Button(\"Go\"); HStack(spacing: 8) { Text(\"Hi\"); }\n",
            "theme(light: 0x005FB8, dark: 0x60CDFF);\n",
            "theme(light: 0xFFFFFF, dark: 0x001A26);\n",
            "theme(light: 0x004E99, dark: 0x98E0FF);\n",
            "0xff 0XAbCd 0xFFu 0B10_01UL 1.25e+2 .5F\n"
        ),
        &[
            (0, "Hello there, ", "string"),
            (0, "{Name}", "other"),
            (0, "!\"", "string"),
            (1, "{", "other"),
            (1, "Format", "method"),
            (1, "Name", "other"),
            (1, "0xFF", "constant.numeric"),
            (1, "}", "other"),
            (1, "!\"", "string"),
            (2, "{Name}", "other"),
            (2, "{{literal}}", "string"),
            (3, "TextInput", "storage.type"),
            (3, "text", "variable"),
            (3, "Name", "other"),
            (3, "change", "variable"),
            (3, ": Rename", "other"),
            (4, "Button", "storage.type"),
            (4, "HStack", "storage.type"),
            (4, "Text", "storage.type"),
            (5, "0x005FB8", "constant.numeric"),
            (5, "0x60CDFF", "constant.numeric"),
            (6, "0xFFFFFF", "constant.numeric"),
            (6, "0x001A26", "constant.numeric"),
            (7, "0x004E99", "constant.numeric"),
            (7, "0x98E0FF", "constant.numeric"),
            (8, "0xff", "constant.numeric"),
            (8, "0XAbCd", "constant.numeric"),
            (8, "0xFFu", "constant.numeric"),
            (8, "0B10_01UL", "constant.numeric"),
            (8, "1.25e+2", "constant.numeric"),
            (8, ".5F", "constant.numeric"),
        ],
    );
}

#[test]
fn repository_samples_have_complete_utf8_spans() {
    fn visit(path: &Path, count: &mut usize) {
        for entry in std::fs::read_dir(path).unwrap() {
            let path = entry.unwrap().path();
            if path.is_dir() {
                if !matches!(path.file_name().unwrap().to_str().unwrap(), "bin" | "obj") {
                    visit(&path, count);
                }
            } else if path.extension().is_some_and(|ext| ext == "xui") {
                highlight(&std::fs::read_to_string(&path).unwrap());
                *count += 1;
            }
        }
    }
    let mut count = 0;
    visit(
        &Path::new(env!("CARGO_MANIFEST_DIR")).join("../../bindings/dotnet"),
        &mut count,
    );
    assert!(count > 0);
    expect(
        "state string Caption = \"Hello \u{1f30d}\";",
        &[(0, "Hello \u{1f30d}", "string")],
    );
}

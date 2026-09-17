#include "xui/syntax_highlighting.hpp"
#include "../demo/gallery_catalog.hpp"
#include "../demo/gallery_reference.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void token(const xui::MultilineText& document, std::wstring_view text, xui::SyntaxKind kind) {
    const auto start = document.text().find(text);
    check(start != std::wstring::npos, "Missing test token");
    if (kind == xui::SyntaxKind::other) {
        for (const auto& span : document.syntax_spans())
            check(span.end <= start || span.start >= start + text.size() || span.kind == kind,
                "Expected uncolored code token");
        return;
    }
    for (const auto& span : document.syntax_spans())
        if (span.start <= start && span.end >= start + text.size() && span.kind == kind) return;
    std::cerr << "Missing token color at " << start << ", expected kind " << static_cast<int>(kind) << '\n';
    for (const auto& span : document.syntax_spans())
        std::cerr << span.start << "-" << span.end << " kind " << static_cast<int>(span.kind) << '\n';
    throw std::runtime_error("Missing expected syntax color");
}
}
int main() {
    try {
        using namespace xui;
        check(syntax_language_for_path(L"C:\\Demo\\CODE.XUI") == "xui", "XUI extension");
        check(syntax_language_for_path(L".gitignore") == "ignore", "Ignore filename");
        check(syntax_language_for_path(L"test.cs") == "csharp", "C# extension");
        check(syntax_language_for_path(L"notes.txt").empty(), "Unknown extension remains plain");
        MultilineText document;
        document.set_text(L"int value = 42;");
        if (!syntax_highlighting_available()) {
            bool rejected{};
            try { set_syntax_language(document, "cpp"); } catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "Disabled builds reject explicit language");
            std::cout << "Syntax-disabled build checks passed\n";
            return 0;
        }
        set_syntax_language(document, "cpp");
        token(document, L"int", SyntaxKind::type);
        token(document, L"42", SyntaxKind::number);
        for (const auto& entry : gallery::entries) {
            document.set_text(entry.code);
            check(!document.syntax_spans().empty(), "Gallery excerpt has no syntax colors");
        }
        for (const auto& reference : gallery::references) {
            for (const auto& [language, text] : {
                    std::pair{"xui", reference.xui}, std::pair{"csharp", reference.csharp}, std::pair{"rust", reference.rust}}) {
                document.set_text(text);
                set_syntax_language(document, language);
                check(document.syntax_enabled(), "Gallery language switch disabled highlighting");
            }
        }
        for (const auto language : {"c", "csharp", "rust", "json", "python", "markdown"}) {
            set_syntax_language(document, language);
            check(document.syntax_enabled(), "Supported language was not enabled");
        }
        for (const auto language : {"c", "cpp", "csharp"}) {
            document.set_text(L"/* first\rsecond */\rint number = 17;");
            set_syntax_language(document, language);
            token(document, L"second", SyntaxKind::comment);
            token(document, L"int", SyntaxKind::type);
            token(document, L"17", SyntaxKind::number);
        }
        document.set_text(L"component Demo {\rstate string Text = \"\U0001f30d\";\r"
            L"/* comment\rcontinued */\rview { Text(\"after\", id: \"ok\"); }\r}");
        set_syntax_language(document, "xui");
        token(document, L"component", SyntaxKind::keyword);
        token(document, L"string", SyntaxKind::type);
        token(document, L"\U0001f30d", SyntaxKind::string);
        token(document, L"continued", SyntaxKind::comment);
        token(document, L"view", SyntaxKind::keyword);
        token(document, L"after", SyntaxKind::string);
        document.set_text(L"$\"Hello there, {Name}!\"");
        token(document, L"Hello there, ", SyntaxKind::string);
        token(document, L"{Name}", SyntaxKind::other);
        token(document, L"!\"", SyntaxKind::string);
        document.set_text(L"$\"Result: {Format(Name, 0xFF)}!\"");
        token(document, L"{", SyntaxKind::other);
        token(document, L"Format", SyntaxKind::function);
        token(document, L"Name", SyntaxKind::other);
        token(document, L"0xFF", SyntaxKind::number);
        token(document, L"}", SyntaxKind::other);
        token(document, L"!\"", SyntaxKind::string);
        document.set_text(L"$@\"Hello {Name}, {{literal}}\"");
        token(document, L"{Name}", SyntaxKind::other);
        token(document, L"{{literal}}", SyntaxKind::string);
        document.set_text(L"TextInput(\"Your name\", text: Name, change: Rename);"
            L" Button(\"Go\"); HStack(spacing: 8) { Text(\"Hi\"); }");
        for (const auto name : {L"TextInput", L"Button", L"HStack"})
            token(document, name, SyntaxKind::type);
        token(document, L"text", SyntaxKind::variable);
        token(document, L"Name", SyntaxKind::other);
        token(document, L"change", SyntaxKind::variable);
        token(document, L": Rename", SyntaxKind::other);
        document.set_text(L"theme(light: 0x005FB8, dark: 0x60CDFF);"
            L" theme(light: 0xFFFFFF, dark: 0x001A26);"
            L" theme(light: 0x004E99, dark: 0x98E0FF);"
            L" 0xff 0XAbCd 0xFFu 0B10_01UL 1.25e+2 .5F");
        for (const auto number : {L"0x005FB8", L"0x60CDFF", L"0xFFFFFF", L"0x001A26", L"0x004E99",
                L"0x98E0FF", L"0xff", L"0XAbCd", L"0xFFu", L"0B10_01UL", L"1.25e+2", L".5F"})
            token(document, number, SyntaxKind::number);
        const auto before = document.text();
        bool rejected{};
        try { set_syntax_language(document, "not-a-language"); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && before == document.text() && document.syntax_enabled(), "Unknown language changed document");
        set_syntax_language(document, "");
        check(!document.syntax_enabled() && document.syntax_spans().empty() && document.text() == before, "Disable syntax");
        std::cout << "LSH bundled grammar and UTF-16 conversion checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

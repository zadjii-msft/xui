#include "xui/documents.hpp"
#include <iostream>
#include <stdexcept>
using namespace xui;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected syntax operation rejection");
}
void state() {
    MultilineText document;
    int tokens{}, changes{};
    document.on_change([&](auto&) { ++changes; });
    require(!document.syntax_enabled() && document.syntax_spans().empty() && !document.rich(), "Plain default state");
    document.set_text(L"if\r\n\U0001f600\nend");
    document.set_selection({3, 5});
    const auto revision = document.revision(), selected = document.selection_revision();
    const auto syntax = document.syntax_revision();
    document.set_syntax_highlighter([&](std::wstring_view text) {
        ++tokens;
        require(text == document.text(), "Highlighter installation receives current snapshot");
        require(text == L"if\r\U0001f600\rend", "Full normalized CR snapshot and UTF-16 offsets");
        return std::vector<SyntaxSpan>{{0, 2, SyntaxKind::keyword}, {3, 5, SyntaxKind::string}, {6, 9, SyntaxKind::other}};
    });
    require(tokens == 1 && changes == 0 && document.syntax_enabled(), "Installation is silent and tokenizes once");
    require(document.revision() == revision && document.selection_revision() == selected &&
        document.selection() == TextSelection{3, 5} && document.syntax_revision() > syntax, "Presentation-only revision");
    require(document.syntax_spans().size() == 3, "Validated spans retained");
    document.set_syntax_highlighter([&](std::wstring_view text) {
        ++tokens;
        return text.empty() ? std::vector<SyntaxSpan>{} : std::vector<SyntaxSpan>{{0, text.size(), SyntaxKind::comment}};
    });
    document.set_text(L"// next\nline");
    require(tokens == 3 && document.text() == L"// next\rline" && changes == 0, "Text setter tokenizes normalized snapshot silently");
    const auto next_revision = document.revision(), next_syntax = document.syntax_revision();
    document.commit_text(L"native\r\nedit", TextSelection{2, 2});
    require(tokens == 4 && changes == 1 && document.text() == L"native\redit" &&
        document.revision() == next_revision && document.syntax_revision() > next_syntax &&
        document.selection() == TextSelection{2, 2}, "Native commits use a separate syntax revision");
    document.commit_text(document.text());
    document.set_text(document.text());
    document.set_selection({0, 0});
    require(tokens == 4 && changes == 1, "No retokenization for identical text or selection");
    document.set_read_only(true);
    document.commit_text(L"ignored");
    require(tokens == 4, "Read-only native changes do not tokenize");
    document.set_read_only(false);
    document.set_text(L"");
    require(tokens == 5 && document.syntax_spans().empty() && document.syntax_enabled(), "Empty document retains enabled tokenizer");
    const auto before_disable = document.syntax_revision();
    document.set_syntax_highlighter({});
    require(!document.syntax_enabled() && document.syntax_spans().empty() && document.syntax_revision() > before_disable,
        "Disable clears presentation");
    const auto disabled_revision = document.syntax_revision();
    document.set_syntax_highlighter({});
    document.set_text(L"no tokens");
    require(tokens == 5 && changes == 1 && document.syntax_revision() == disabled_revision, "Disabled tokenizer stays inactive");
}
void validation() {
    MultilineText document;
    document.set_text(L"A\U0001f600Z");
    document.set_selection({1, 3});
    document.set_syntax_highlighter([](auto) { return std::vector<SyntaxSpan>{{1, 3, SyntaxKind::string}}; });
    const auto original = document.syntax_spans();
    const auto revision = document.syntax_revision();
    const std::vector<std::vector<SyntaxSpan>> invalid{
        {{0, 0, SyntaxKind::other}}, {{3, 1, SyntaxKind::other}}, {{0, 5, SyntaxKind::other}},
        {{2, 3, SyntaxKind::other}}, {{1, 2, SyntaxKind::other}},
        {{1, 3, SyntaxKind::string}, {0, 1, SyntaxKind::keyword}},
        {{0, 3, SyntaxKind::string}, {1, 4, SyntaxKind::keyword}},
        {{0, 1, static_cast<SyntaxKind>(-1)}}, {{0, 1, static_cast<SyntaxKind>(8)}}
    };
    for (const auto& spans : invalid) {
        rejects([&] { document.set_syntax_highlighter([&](auto) { return spans; }); });
        require(document.syntax_spans() == original && document.syntax_revision() == revision &&
            document.syntax_enabled() && document.selection() == TextSelection{1, 3}, "Rejected spans preserve installed state");
    }
    rejects([&] { document.set_syntax_highlighter([](auto) { return std::vector<SyntaxSpan>(DocumentText::document_limit + 1); }); });
    rejects([&] {
        document.set_syntax_highlighter([](auto) -> std::vector<SyntaxSpan> { throw std::runtime_error("tokenizer failed"); });
    });
    require(document.syntax_spans() == original && document.syntax_revision() == revision, "Callback failure preserves installed state");
    RichText rich;
    rejects([&] { rich.set_syntax_highlighter([](auto) { return std::vector<SyntaxSpan>{}; }); });
    rejects([&] { rich.set_syntax_highlighter({}); });
    require(!rich.syntax_enabled(), "Rich authored runs reject syntax API");
    for (int kind = 0; kind <= 7; ++kind)
        document.set_syntax_highlighter([&](auto) { return std::vector<SyntaxSpan>{{0, 1, static_cast<SyntaxKind>(kind)}}; });
}
void failures() {
    MultilineText document;
    document.set_text(L"old");
    document.set_selection({1, 2});
    int tokens{}, changes{};
    bool fail{};
    document.set_syntax_highlighter([&](std::wstring_view text) {
        ++tokens;
        if (fail) throw std::runtime_error("tokenizer failed");
        return std::vector<SyntaxSpan>{{0, text.size(), SyntaxKind::keyword}};
    });
    document.on_change([&](auto& text) {
        ++changes;
        require(text == L"native" && document.syntax_spans().empty() && document.selection() == TextSelection{6, 6},
            "Native tokenizer failure still publishes text and selection without stale spans");
    });
    fail = true;
    const auto revision = document.revision(), syntax = document.syntax_revision(), selection = document.selection_revision();
    rejects([&] { document.set_text(L"new"); });
    require(document.text() == L"old" && document.syntax_spans().size() == 1 && document.revision() == revision &&
        document.syntax_revision() == syntax && document.selection_revision() == selection && changes == 0,
        "Failed text setter is atomic");
    rejects([&] { document.commit_text(L"native", TextSelection{6, 6}); });
    require(document.text() == L"native" && document.syntax_enabled() && changes == 1 &&
        document.syntax_revision() > syntax && document.revision() == revision, "Native error leaves coherent text and reusable tokenizer");
    fail = false;
    document.on_change([&](auto&) { ++changes; });
    document.commit_text(L"recovered");
    require(changes == 2 && tokens == 4 && document.syntax_spans() == std::vector<SyntaxSpan>{{0, 9, SyntaxKind::keyword}},
        "Tokenizer recovers on next native edit");
    document.set_syntax_highlighter([&](auto) {
        rejects([&] { document.set_text(L"reentrant"); });
        rejects([&] { document.commit_text(L"reentrant"); });
        rejects([&] { document.set_syntax_highlighter({}); });
        rejects([&] { document.set_maximum_length(1); });
        require(!document.command(TextCommand::undo), "Tokenizer cannot dispatch native editing commands");
        return std::vector<SyntaxSpan>{};
    });
    require(document.text() == L"recovered" && document.syntax_enabled(), "Reentrant mutation cannot invalidate the snapshot");
}
}
int main() {
    try {
        state(); validation(); failures();
        std::cout << "Document syntax state, UTF-16 validation and error semantics passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

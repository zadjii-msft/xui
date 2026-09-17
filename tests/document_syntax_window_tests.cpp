#include "../src/native_document.hpp"
#include "../src/window_host.hpp"
#include <richedit.h>
#include <iostream>
#include <stdexcept>
using namespace xui;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    HWND parent{};
    std::shared_ptr<MultilineText> document = std::make_shared<MultilineText>();
    std::unique_ptr<NativeDocumentBridge> bridge;
    std::exception_ptr failure;
    int changes{}, tokens{}, formats{}, replacements{}, writable_transitions{};
    Palette palette = Palette::system(ThemeMode::light);
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
        auto* self = reinterpret_cast<Fixture*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && self->bridge) {
            try {
                if (message == WM_COMMAND && HIWORD(wp) == EN_CHANGE) self->bridge->changed();
                if (message == WM_NOTIFY) return self->bridge->notify(*reinterpret_cast<NMHDR*>(lp));
            } catch (...) { self->failure = std::current_exception(); }
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
    static LRESULT CALLBACK observe(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) noexcept {
        auto& self = *reinterpret_cast<Fixture*>(data);
        if (message == EM_SETCHARFORMAT) ++self.formats;
        if (message == EM_STREAMIN || message == WM_SETTEXT || message == EM_REPLACESEL) ++self.replacements;
        if (message == EM_SETREADONLY && !wp) ++self.writable_transitions;
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    explicit Fixture(std::wstring value = L"if \U0001f600\rvalue", bool read_only = false) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"XuiDocumentSyntaxTest";
        RegisterClassW(&cls);
        parent = CreateWindowW(cls.lpszClassName, L"Document syntax fixture", WS_OVERLAPPEDWINDOW,
            0, 0, 500, 250, nullptr, nullptr, cls.hInstance, nullptr);
        require(parent != nullptr, "Create parent");
        SetWindowLongPtrW(parent, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        document->set_text(std::move(value));
        document->set_read_only(read_only);
        if (read_only) enable();
        bridge = std::make_unique<NativeDocumentBridge>(document);
        bridge->attach(parent, 1);
        bridge->on_failure([&] { failure = std::current_exception(); });
        require(SetWindowSubclass(window(), observe, 2, reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Observe native presentation");
        refresh();
        MoveWindow(window(), 0, 0, 480, 150, FALSE);
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        document->on_change([&](auto&) { ++changes; });
    }
    ~Fixture() { bridge.reset(); DestroyWindow(parent); }
    HWND window() const { return bridge->window(); }
    void refresh() {
        bridge->update(96, palette);
        if (failure) std::rethrow_exception(failure);
    }
    void enable() {
        document->set_syntax_highlighter([&](std::wstring_view text) {
            ++tokens;
            std::vector<SyntaxSpan> spans;
            if (text.starts_with(L"if")) spans.push_back({0, 2, SyntaxKind::keyword});
            if (const auto at = text.find(L"\U0001f600"); at != text.npos) spans.push_back({at, at + 2, SyntaxKind::string});
            return spans;
        });
    }
    std::wstring text() {
        GETTEXTLENGTHEX length{GTL_PRECISE | GTL_NUMCHARS, 1200};
        std::wstring result(static_cast<std::size_t>(SendMessageW(window(), EM_GETTEXTLENGTHEX,
            reinterpret_cast<WPARAM>(&length), 0)) + 1, L'\0');
        GETTEXTEX request{static_cast<DWORD>(result.size() * sizeof(wchar_t)), GT_DEFAULT, 1200};
        result.resize(SendMessageW(window(), EM_GETTEXTEX, reinterpret_cast<WPARAM>(&request),
            reinterpret_cast<LPARAM>(result.data())));
        return result;
    }
    TextSelection selection() {
        CHARRANGE range{};
        SendMessageW(window(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
        return {static_cast<std::size_t>(range.cpMin), static_cast<std::size_t>(range.cpMax)};
    }
    COLORREF foreground(LONG start, LONG end) {
        const auto selected = selection();
        const auto events = SendMessageW(window(), EM_SETEVENTMASK, 0, 0);
        CHARRANGE range{start, end};
        SendMessageW(window(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        CHARFORMAT2W format{sizeof(format)};
        SendMessageW(window(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        range = {static_cast<LONG>(selected.start), static_cast<LONG>(selected.end)};
        SendMessageW(window(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        SendMessageW(window(), EM_SETEVENTMASK, 0, events);
        require((format.dwMask & CFM_COLOR) != 0, "Range has a uniform native foreground");
        return format.crTextColor;
    }
    void check(std::wstring_view expected, int count) {
        if (failure) std::rethrow_exception(failure);
        require(text() == expected && document->text() == expected, "Exact retained and native UTF-16 text");
        require(changes == count, "Only real text edits invoke change callbacks");
    }
};
void named_argument_colors() {
    for (const bool read_only : {false, true}) {
        Fixture f(L"text: Name", read_only);
        f.document->set_syntax_highlighter([](std::wstring_view) {
            return std::vector<SyntaxSpan>{{0, 4, SyntaxKind::variable}};
        });
        for (const auto theme : {ThemeMode::light, ThemeMode::dark}) {
            f.palette = Palette::system(theme);
            f.refresh();
            require(f.foreground(0, 4) == platform::native_color(f.palette.accent) &&
                f.foreground(6, 10) == platform::native_color(f.palette.text) &&
                f.foreground(0, 4) != f.foreground(6, 10), "Named argument differs from its identifier value");
        }
        f.palette.high_contrast = true;
        f.refresh();
        require(f.foreground(0, 4) == f.foreground(6, 10), "Named arguments preserve high-contrast text colors");
        f.check(L"text: Name", 0);
    }
}
void presentation_and_history() {
    Fixture f;
    const auto initial = f.document->text();
    const auto hwnd = f.window();
    require(!f.document->rich() && (SendMessageW(hwnd, EM_GETTEXTMODE, 0, 0) & TM_RICHTEXT), "Plain contract with rich native presentation");
    SendMessageW(hwnd, EM_SETSEL, initial.size(), initial.size());
    SendMessageW(hwnd, WM_CHAR, L'!', 1);
    f.check(initial + L"!", 1);
    f.document->set_selection({3, 5});
    f.refresh();
    const auto selected = f.selection();
    const auto modified = SendMessageW(hwnd, EM_GETMODIFY, 0, 0);
    const auto events = SendMessageW(hwnd, EM_GETEVENTMASK, 0, 0);
    const auto replacements = f.replacements;
    const auto clipboard = GetClipboardSequenceNumber();
    f.enable();
    f.refresh();
    require(f.selection() == selected && f.document->selection() == selected, "Syntax preserves both selections");
    require(SendMessageW(hwnd, EM_GETMODIFY, 0, 0) == modified && SendMessageW(hwnd, EM_GETEVENTMASK, 0, 0) == events,
        "Syntax restores modified state and events");
    require(f.replacements == replacements && GetClipboardSequenceNumber() == clipboard, "Syntax never replaces text or uses clipboard");
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent) &&
        f.foreground(3, 5) == platform::native_color(f.palette.folder) &&
        f.foreground(6, 7) == platform::native_color(f.palette.text), "Token and unclassified native foregrounds");
    const auto formats = f.formats, tokens = f.tokens;
    f.refresh(); f.refresh(); f.refresh();
    require(f.formats == formats && f.tokens == tokens, "Repeated updates do not restyle or retokenize");
    require(f.document->command(TextCommand::undo), "Syntax installation preserves previous typing undo");
    f.check(initial, 2);
    f.refresh();
    require(f.document->command(TextCommand::redo), "Syntax refresh preserves redo");
    f.check(initial + L"!", 3);
    f.refresh();
    const auto revision = f.document->revision(), syntax = f.document->syntax_revision();
    f.document->replace_range({0, 2}, f.document->text(), L"IF");
    f.check(L"IF \U0001f600\rvalue!", 4);
    require(f.document->revision() == revision && f.document->syntax_revision() > syntax, "Native edit advances syntax, not property revision");
    f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.text), "Removed token resets default foreground");
    require(f.document->command(TextCommand::undo), "One undo reverses semantic edit, not formatting");
    f.check(initial + L"!", 5);
    f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent), "Undo retokenizes original text");
    f.document->set_syntax_highlighter({});
    f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.text) &&
        f.foreground(3, 5) == platform::native_color(f.palette.text), "Disable resets every token");
    require(f.document->command(TextCommand::redo), "Disable preserves native redo");
    f.check(L"IF \U0001f600\rvalue!", 6);
}
void colors_and_composition() {
    Fixture f;
    f.enable(); f.refresh();
    const auto hwnd = f.window();
    const auto initial = f.document->text();
    const auto tokens = f.tokens;
    f.palette = Palette::system(ThemeMode::dark);
    f.refresh();
    require(f.tokens == tokens && f.foreground(0, 2) == platform::native_color(f.palette.accent), "Dark palette recolors without tokenization");
    auto contrast = f.palette;
    contrast.high_contrast = true;
    contrast.text = D2D1::ColorF(0xffff00);
    f.palette = contrast; f.refresh();
    require(f.foreground(0, 2) == platform::native_color(contrast.text) &&
        f.foreground(3, 5) == platform::native_color(contrast.text), "High contrast suppresses syntax colors");
    f.palette = Palette::system(ThemeMode::light);
    f.document->set_enabled(false); EnableWindow(hwnd, FALSE); f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.disabled) &&
        f.foreground(3, 5) == platform::native_color(f.palette.disabled), "Disabled ink suppresses syntax colors");
    f.document->set_enabled(true); EnableWindow(hwnd, TRUE); f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent), "Reenable restores tokens");
    SendMessageW(hwnd, EM_SETMODIFY, FALSE, 0);
    f.enable(); f.refresh();
    require(!SendMessageW(hwnd, EM_GETMODIFY, 0, 0), "Rehighlight keeps clean document clean");
    const auto formats = f.formats, before = f.tokens;
    SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
    SendMessageW(hwnd, EM_SETSEL, initial.size(), initial.size());
    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"!"));
    f.palette = Palette::system(ThemeMode::dark);
    f.refresh();
    require(f.bridge->composing() && f.tokens == before && f.formats == formats && f.changes == 0 &&
        f.document->text() == initial, "Composition does not publish or style intermediate text");
    SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
    f.refresh();
    f.check(initial + L"!", 1);
    require(f.tokens == before + 1 && f.foreground(0, 2) == platform::native_color(f.palette.accent),
        "Committed composition refreshes syntax and deferred palette");
}
void scrolling_and_errors() {
    std::wstring value = L"if \U0001f600";
    for (int i = 0; i < 100; ++i) value += L"\rline";
    Fixture f(value);
    SendMessageW(f.window(), EM_LINESCROLL, 0, 45);
    POINT before{}, after{};
    SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&before));
    require(before.y > 0, "Fixture is scrolled");
    f.enable(); f.refresh();
    SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after));
    require(before.x == after.x && before.y == after.y, "Token formatting preserves scroll position");
    bool fail{};
    f.document->set_syntax_highlighter([&](std::wstring_view text) {
        if (fail) throw std::runtime_error("failed native tokenizer");
        return std::vector<SyntaxSpan>{{0, text.size(), SyntaxKind::keyword}};
    });
    f.refresh(); fail = true;
    bool rejected{};
    try { f.document->replace_range({0, 2}, value, L"IF"); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Native tokenizer failure is explicit");
    value.replace(0, 2, L"IF");
    f.check(value, 1);
    f.refresh();
    require(f.document->syntax_enabled() && f.document->syntax_spans().empty() &&
        f.foreground(0, 2) == platform::native_color(f.palette.text), "Native error clears stale presentation without reverting text");
    fail = false;
    require(f.document->command(TextCommand::undo), "Failed tokenizer does not lose edit undo");
    value.replace(0, 2, L"if");
    f.check(value, 2); f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent), "Native tokenizer recovers after error");
}
void typing_and_properties() {
    Fixture f;
    f.enable(); f.refresh();
    const auto initial = f.document->text();
    SendMessageW(f.window(), EM_SETSEL, initial.size(), initial.size());
    for (const auto c : std::wstring_view(L"abc")) {
        SendMessageW(f.window(), WM_CHAR, c, 1);
        f.refresh();
    }
    f.check(initial + L"abc", 3);
    require(f.document->command(TextCommand::undo), "Undo consecutive typing");
    f.check(initial, 4);
    f.refresh();
    require(f.document->command(TextCommand::redo), "Redo consecutive typing");
    f.check(initial + L"abc", 5);
    f.document->set_text(L"if\n\U0001f600");
    f.refresh();
    f.check(L"if\r\U0001f600", 5);
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent) &&
        f.foreground(3, 5) == platform::native_color(f.palette.folder), "New text property uses its normalized syntax snapshot");
}
void plain_shortcuts() {
    Fixture f;
    f.enable(); f.refresh();
    f.document->set_selection({0, 2}); f.refresh();
    BYTE original[256]{}, keys[256]{};
    require(GetKeyboardState(original) != FALSE, "Read keyboard state");
    std::copy(std::begin(original), std::end(original), std::begin(keys));
    keys[VK_CONTROL] = keys[VK_LCONTROL] = 0x80;
    require(SetKeyboardState(keys) != FALSE, "Set fixture modifier state");
    SendMessageW(f.window(), WM_KEYDOWN, L'B', 1);
    CHARFORMAT2W key_format{sizeof(key_format)};
    SendMessageW(f.window(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&key_format));
    SendMessageW(f.window(), WM_CHAR, 2, 1);
    SetKeyboardState(original);
    CHARFORMAT2W format{sizeof(format)};
    SendMessageW(f.window(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    require(!(key_format.dwEffects & CFE_BOLD) && !(format.dwEffects & CFE_BOLD),
        "Plain document ignores native rich formatting shortcuts");
    f.check(L"if \U0001f600\rvalue", 0);
}
void readonly_presentation() {
    const std::wstring initial = L"if \U0001f600\rvalue";
    {
        Fixture f(initial, true);
        require(f.foreground(0, 2) == platform::native_color(f.palette.accent) &&
            f.foreground(3, 5) == platform::native_color(f.palette.folder), "Initially read-only document receives syntax colors");
        f.document->set_selection({3, 5}); f.refresh();
        const auto modified = SendMessageW(f.window(), EM_GETMODIFY, 0, 0);
        const auto events = SendMessageW(f.window(), EM_GETEVENTMASK, 0, 0);
        const auto replacements = f.replacements;
        f.palette = Palette::system(ThemeMode::dark); f.refresh();
        require(f.foreground(0, 2) == platform::native_color(f.palette.accent) &&
            f.selection() == TextSelection{3, 5} && f.document->selection() == TextSelection{3, 5},
            "Read-only palette refresh preserves native and model selection");
        require(f.document->read_only() && (GetWindowLongPtrW(f.window(), GWL_STYLE) & ES_READONLY) &&
            f.writable_transitions == 0, "Presentation never makes a read-only control writable");
        require(modified == SendMessageW(f.window(), EM_GETMODIFY, 0, 0) &&
            events == SendMessageW(f.window(), EM_GETEVENTMASK, 0, 0) && f.replacements == replacements,
            "Read-only presentation preserves modified state and events without replacing text");
        require(!f.document->command(TextCommand::undo) && !f.document->command(TextCommand::cut) &&
            !f.document->command(TextCommand::paste), "Read-only commands stay restricted");
        SendMessageW(f.window(), WM_CHAR, L'X', 1);
        bool rejected{};
        try { f.document->replace_range({0, 2}, initial, L"IF"); } catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Read-only semantic replacement stays restricted");
        f.check(initial, 0);
    }
    Fixture f(initial);
    SendMessageW(f.window(), EM_SETSEL, initial.size(), initial.size());
    SendMessageW(f.window(), WM_CHAR, L'!', 1);
    f.document->set_read_only(true); f.enable(); f.refresh();
    require(SendMessageW(f.window(), EM_CANUNDO, 0, 0) != 0 && f.writable_transitions == 0,
        "Read-only syntax formatting retains earlier undo without dropping read-only");
    f.document->set_read_only(false); f.refresh();
    require(f.document->command(TextCommand::undo), "Undo remains available after leaving read-only");
    f.check(initial, 2); f.refresh();
    f.document->set_read_only(true); f.enable(); f.refresh();
    require(SendMessageW(f.window(), EM_CANREDO, 0, 0) != 0, "Read-only syntax formatting preserves redo");
    f.document->set_read_only(false); f.refresh();
    require(f.document->command(TextCommand::redo), "Redo remains available after leaving read-only");
    f.check(initial + L"!", 3);
}
}
int main() {
    try {
        require(SUCCEEDED(OleInitialize(nullptr)), "Initialize native document COM");
        struct Com { ~Com() { OleUninitialize(); } } com;
        readonly_presentation();
        named_argument_colors();
        presentation_and_history(); colors_and_composition(); scrolling_and_errors(); typing_and_properties(); plain_shortcuts();
        std::cout << "Native syntax colors, undo/redo, selection, scroll, composition and error handling passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

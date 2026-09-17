#include "../src/native_document.hpp"
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
    int changes{};
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
    explicit Fixture(std::wstring value = L"head \U0001f600 old\rtail") {
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"XuiDocumentEditingTest";
        RegisterClassW(&cls);
        parent = CreateWindowW(cls.lpszClassName, L"Document range fixture", WS_OVERLAPPEDWINDOW,
            0, 0, 500, 250, nullptr, nullptr, cls.hInstance, nullptr);
        require(parent != nullptr, "Create parent");
        SetWindowLongPtrW(parent, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        document->set_text(std::move(value));
        bridge = std::make_unique<NativeDocumentBridge>(document);
        bridge->attach(parent, 1);
        bridge->update(96, Palette::system(ThemeMode::light));
        MoveWindow(bridge->window(), 0, 0, 480, 200, FALSE);
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        document->on_change([&](auto&) { ++changes; });
    }
    ~Fixture() { bridge.reset(); DestroyWindow(parent); }
    std::wstring text() {
        GETTEXTLENGTHEX length{GTL_PRECISE | GTL_NUMCHARS, 1200};
        std::wstring result(static_cast<std::size_t>(SendMessageW(bridge->window(), EM_GETTEXTLENGTHEX,
            reinterpret_cast<WPARAM>(&length), 0)) + 1, L'\0');
        GETTEXTEX request{static_cast<DWORD>(result.size() * sizeof(wchar_t)), GT_DEFAULT, 1200};
        result.resize(SendMessageW(bridge->window(), EM_GETTEXTEX, reinterpret_cast<WPARAM>(&request),
            reinterpret_cast<LPARAM>(result.data())));
        return result;
    }
    TextSelection selection() {
        CHARRANGE range{};
        SendMessageW(bridge->window(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
        return {static_cast<std::size_t>(range.cpMin), static_cast<std::size_t>(range.cpMax)};
    }
    void check(std::wstring_view value, int count) {
        if (failure) std::rethrow_exception(failure);
        if (text() != value || document->text() != value) {
            for (const auto& item : {std::wstring(value), text(), document->text()}) {
                std::cerr << "UTF16:";
                for (auto c : item) std::cerr << " " << std::hex << static_cast<unsigned>(c);
                std::cerr << std::dec << '\n';
            }
        }
        require(text() == value && document->text() == value, "Exact native and retained text");
        require(changes == count, "Exactly one callback per edit");
    }
    template<class F> void rejects(F action) {
        const auto before = text();
        const auto selected = selection();
        const auto count = changes;
        const auto undo = SendMessageW(bridge->window(), EM_CANUNDO, 0, 0);
        const auto redo = SendMessageW(bridge->window(), EM_CANREDO, 0, 0);
        bool rejected{};
        try { action(); } catch (const std::exception&) { rejected = true; }
        require(rejected, "Invalid native operation rejected");
        require(text() == before && selection() == selected && changes == count, "Rejection does not mutate text, selection or callbacks");
        require(undo == SendMessageW(bridge->window(), EM_CANUNDO, 0, 0) &&
            redo == SendMessageW(bridge->window(), EM_CANREDO, 0, 0), "Rejection preserves undo and redo availability");
    }
};
void edits() {
    Fixture f;
    const auto initial = f.document->text();
    auto hwnd = f.bridge->window();
    SendMessageW(hwnd, EM_SETSEL, initial.size(), initial.size());
    SendMessageW(hwnd, WM_CHAR, L'!', 1);
    const auto original = initial + L"!";
    f.check(original, 1);
    const auto clipboard = GetClipboardSequenceNumber();
    int semantic_callbacks{};
    f.document->on_change([&](auto&) {
        ++f.changes;
        if (++semantic_callbacks == 1) {
            require(f.document->selection() == TextSelection{12, 12} && f.selection() == TextSelection{12, 12},
                "Result selection available inside callback");
            f.rejects([&] { f.document->replace_range({0, 1}, f.document->text(), L"Q"); });
            require(!f.document->command(TextCommand::undo), "Reentrant undo rejected");
        }
    });
    const auto result = f.document->replace_range({8, 11}, original, L"N\n\U0001f600");
    const auto replaced = L"head \U0001f600 N\r\U0001f600\rtail!";
    require(result == TextSelection{12, 12} && f.selection() == result, "Returned native caret");
    f.check(replaced, 2);
    require(GetClipboardSequenceNumber() == clipboard, "Replacement never changes clipboard");
    f.bridge->update(96, Palette::system(ThemeMode::light));
    f.check(replaced, 2);
    require(f.document->command(TextCommand::undo), "Undo semantic edit");
    f.check(original, 3);
    f.rejects([&] { f.document->replace_range({0, 1}, L"stale", L"B"); });
    require(f.document->command(TextCommand::undo), "Prior typing history retained");
    f.check(initial, 4);
    require(f.document->command(TextCommand::redo), "Redo native typing");
    f.check(original, 5);
    require(f.document->command(TextCommand::redo), "Redo semantic edit");
    f.check(replaced, 6);
    auto second = f.document->replace_range({0, 4}, replaced, L"HEAD");
    require(second == TextSelection{4, 4}, "Second caret");
    f.check(L"HEAD \U0001f600 N\r\U0001f600\rtail!", 7);
    require(f.document->command(TextCommand::undo), "Second visual edit is separate");
    f.check(replaced, 8);
    require(f.document->command(TextCommand::undo), "First visual edit is separate");
    f.check(original, 9);
}
void rejection_and_limits() {
    Fixture f(L"A\U0001f600Z");
    auto hwnd = f.bridge->window();
    const auto value = f.document->text();
    f.document->set_selection({1, 3}); f.bridge->update(96, Palette::system(ThemeMode::light));
    f.rejects([&] { f.document->replace_range({2, 3}, value, L"B"); });
    f.rejects([&] { f.document->replace_range({1, 2}, value, L"B"); });
    f.rejects([&] { f.document->replace_range({0, 99}, value, L"B"); });
    f.rejects([&] { f.document->replace_range({1, 3}, value, std::wstring(1, 0xd800)); });
    f.rejects([&] { f.document->replace_range({1, 3}, value, std::wstring(L"a\0b", 3)); });
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"\U0001f600"); });
    f.document->set_read_only(true);
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    f.document->set_read_only(false);
    SendMessageW(hwnd, EM_SETREADONLY, TRUE, 0);
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    SendMessageW(hwnd, EM_SETREADONLY, FALSE, 0);
    f.document->set_visible(false);
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    f.document->set_visible(true);
    ShowWindow(f.parent, SW_HIDE);
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    ShowWindow(f.parent, SW_SHOWNOACTIVATE);
    EnableWindow(hwnd, FALSE);
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    EnableWindow(hwnd, TRUE);
    EnableWindow(f.parent, FALSE);
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    EnableWindow(f.parent, TRUE);
    SendMessageW(hwnd, EM_SETSEL, value.size(), value.size());
    SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
    require(f.bridge->composing(), "Composition entered");
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
    f.document->set_maximum_length(4);
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"BCD"); });
    f.check(value, 0);
    std::cout << "Normalized replacement\n" << std::flush;
    f.document->replace_range({1, 3}, value, L"\r\nB");
    f.check(L"A\rBZ", 1);
    require(f.document->command(TextCommand::undo), "Normalized edit undo");
    f.check(value, 2);
    require(f.document->command(TextCommand::redo), "Normalized edit redo");
    f.check(L"A\rBZ", 3);
    std::cout << "Deletion and insertion\n" << std::flush;
    f.document->replace_range({1, 3}, L"A\rBZ", L"");
    f.check(L"AZ", 4);
    f.document->replace_range({1, 1}, L"AZ", L"\U0001f600");
    f.check(value, 5);
    const auto mask = SendMessageW(hwnd, EM_SETEVENTMASK, 0, 0);
    SendMessageW(hwnd, EM_SETSEL, 0, 1);
    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"T"));
    SendMessageW(hwnd, EM_SETEVENTMASK, 0, mask);
    require(f.document->text() == value && f.text() != value, "Fixture contains native text ahead of model");
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    require(f.document->command(TextCommand::undo), "Rejected stale-native edit leaves previous undo intact");
    f.check(value, 5);
    SendMessageW(hwnd, EM_SETSEL, value.size(), value.size());
    SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
    SendMessageW(hwnd, EM_SETSEL, 0, 1);
    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Q"));
    SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
    f.check(L"Q\U0001f600Z", 6);
    f.rejects([&] { f.document->replace_range({1, 3}, value, L"B"); });
    f.document->set_text(L"wait");
    f.rejects([&] { f.document->replace_range({0, 1}, L"wait", L"W"); });
}
void lifetime() {
    Fixture f(L"ABC");
    auto model = f.document;
    f.document->on_change([&](auto&) {
        ++f.changes;
        f.bridge.reset();
        f.document.reset();
    });
    require(model->replace_range({1, 2}, L"ABC", L"X") == TextSelection{2, 2}, "Owner deletion callback returns safely");
    require(model->text() == L"AXC" && f.changes == 1, "Model lives through callback");
    bool rejected{};
    try { model->replace_range({1, 2}, L"AXC", L"Y"); } catch (const std::logic_error&) { rejected = true; }
    require(rejected, "Detached model rejects subsequent edit");
}
void maximum() {
    Fixture f(L"A");
    f.document->set_maximum_length(DocumentText::document_limit);
    const std::wstring replacement(DocumentText::document_limit - 1, L'B');
    const auto result = f.document->replace_range({1, 1}, L"A", replacement);
    require(result == TextSelection{DocumentText::document_limit, DocumentText::document_limit}, "Maximum native offset");
    const auto full = L"A" + replacement;
    f.check(full, 1);
    f.rejects([&] { f.document->replace_range({0, 0}, full, L"C"); });
    require(f.document->command(TextCommand::undo), "Maximum document undo");
    f.check(L"A", 2);
    require(f.document->command(TextCommand::redo), "Maximum document redo");
    f.check(full, 3);
}
}
int main() {
    try {
        const auto hr = OleInitialize(nullptr);
        require(SUCCEEDED(hr), "Initialize native document COM");
        struct Com { ~Com() { OleUninitialize(); } } com;
        std::cout << "Undo history and consecutive edits\n" << std::flush;
        edits();
        std::cout << "Rejected targets, normalization and limits\n" << std::flush;
        rejection_and_limits();
        std::cout << "Callback owner lifetime\n" << std::flush;
        lifetime();
        std::cout << "Maximum document length\n" << std::flush;
        maximum();
        std::cout << "Native document range replacement, rejection, undo/redo and lifetime passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

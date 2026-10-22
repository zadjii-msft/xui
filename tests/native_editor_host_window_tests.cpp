#include "../src/text_input_host.hpp"
#include "../src/document_host.hpp"
#include "../src/password_host.hpp"
#include "../src/date_time_host.hpp"
#include "native_editor_test_support.hpp"
#include <richedit.h>
#include <iostream>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
unsigned descendants(HWND root) {
    unsigned count{};
    EnumChildWindows(root, [](HWND, LPARAM data) -> BOOL { ++*reinterpret_cast<unsigned*>(data); return TRUE; },
        reinterpret_cast<LPARAM>(&count));
    return count;
}
std::wstring native_text(HWND window) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), static_cast<int>(text.size())));
    return text;
}
DateTimeValue date_value(const SYSTEMTIME& value) {
    return {value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute, value.wSecond};
}
bool same_date(const SYSTEMTIME& actual, DateTimeValue expected) {
    return actual.wYear == expected.year && actual.wMonth == expected.month && actual.wDay == expected.day;
}
struct Fixture final : PeerHostServices, NativeFieldServices {
    HWND window{};
    std::unique_ptr<NativeFieldHostPeer> peer;
    std::exception_ptr failure;
    unsigned retirements{}, failures{};
    bool reject_font{};
    Palette palette = Palette::system(ThemeMode::light, VisualStyle::winui);
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
        auto* self = reinterpret_cast<Fixture*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        try {
            if (self && self->peer) {
                if (message == WM_COMMAND && HIWORD(wp) == EN_CHANGE &&
                    self->peer->window == reinterpret_cast<HWND>(lp)) self->peer->native_change();
                if (message == WM_NOTIFY && self->peer->window == reinterpret_cast<const NMHDR*>(lp)->hwndFrom)
                    return self->peer->native_notify(*reinterpret_cast<const NMHDR*>(lp));
            }
        } catch (...) { self->failure = std::current_exception(); ++self->failures; }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
    Fixture() {
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"XuiNativeEditorOwnerTest";
        RegisterClassW(&cls);
        window = CreateWindowW(cls.lpszClassName, L"Native editor owner fixture", WS_OVERLAPPEDWINDOW,
            0, 0, 600, 400, nullptr, nullptr, cls.hInstance, nullptr);
        require(window != nullptr, "Create owned editor fixture");
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        ShowWindow(window, SW_SHOWNOACTIVATE);
        require(IsWindowVisible(window) != FALSE, "Native owner fixture requires supported native visibility");
    }
    ~Fixture() {
        peer.reset();
        if (window) DestroyWindow(window);
    }
    void attach() {
        peer->control->set_visual_style(VisualStyle::winui);
        peer->control->arrange({4, 4, 450, 180});
        peer->attach(window, 101);
        peer->theme_native(GetDpiForWindow(window), palette, native_caption_font());
        peer->sync(GetDpiForWindow(window), palette);
        peer->arrange_native(peer->control->bounds(), GetDpiForWindow(window), true, palette);
        if (failure) std::rethrow_exception(failure);
    }
    HWND create_input_surface(HostPeer&, HWND, int) override { std::terminate(); }
    LRESULT dispatch_input(HostPeer&, HWND, UINT, WPARAM, LPARAM) noexcept override { std::terminate(); }
    bool input_enabled(const HostPeer&) const override { return true; }
    bool input_visible(const HostPeer&) const override { return true; }
    bool owner_open() const override { return window && IsWindow(window); }
    bool focus_peer(HostPeer& value, bool) override { SetFocus(value.window); return GetFocus() == value.window; }
    void reveal_peer(HostPeer&) override {}
    void update_peer_state() override {}
    void invalidate_peer_paint() override {}
    bool wheel_animation_allowed() const override { return false; }
    void offer_row_tooltip(HostPeer&) override { std::terminate(); }
    bool file_drag_eligible(const Control&, HWND) const override { return false; }
    void deliver_file_drop(std::function<void()>) noexcept override { std::terminate(); }
    std::shared_ptr<PeerHostServices> acquire_owner_lease() override { std::terminate(); }
    void forget_peer(HostPeer&) noexcept override { ++retirements; }
    void peer_failure() noexcept override { failure = std::current_exception(); ++failures; }
    const wchar_t* native_font_family() const override {
        if (reject_font) throw std::runtime_error("Controlled native editor attachment failure after caption");
        return L"Consolas";
    }
    HFONT native_caption_font() const override { return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)); }
    std::shared_ptr<NativeFieldServices> acquire_native_lease() override { std::terminate(); }
    NativeFieldEmbedding native_embedding(const NativeFieldHostPeer& value) const override {
        NativeFieldEmbedding result;
        result.enabled = true; result.focused = GetFocus() == value.window;
        return result;
    }
    LRESULT dispatch_native_field(NativeFieldHostPeer& value, HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept override {
        try { return value.native_message(hwnd, message, wp, lp, 2, false); }
        catch (...) { peer_failure(); return 0; }
    }
    void collect_text_clear(TextInputHostPeer&) override { std::terminate(); }
    void activate_text_clear(std::uint64_t) override { std::terminate(); }
    void invalidate_native_field(Invalidation) override {}
};
void partial_attachment() {
    Fixture fixture;
    fixture.reject_font = true;
    fixture.peer = std::make_unique<TextInputHostPeer>(fixture, fixture, std::make_shared<TextInput>(L"Partial caption"));
    bool rejected{};
    try { fixture.attach(); } catch (const std::runtime_error& error) {
        rejected = std::string_view(error.what()) == "Controlled native editor attachment failure after caption";
    }
    require(rejected && descendants(fixture.window) == 1 && fixture.peer->caption_window() &&
        !fixture.peer->window, "Attachment failure is explicit after the real caption and before EDIT creation");
    fixture.peer.reset();
    require(descendants(fixture.window) == 0 && fixture.retirements == 1,
        "Partial attachment retires its caption exactly once without an orphan native child");
}
void native_ownership() {
    Fixture fixture;
    const auto cycle = [&] {
        auto input = std::make_shared<TextInput>(L"Retained caption");
        input->set_text(L"native input");
        fixture.peer = std::make_unique<TextInputHostPeer>(fixture, fixture, input);
        fixture.attach();
        const auto edit = fixture.peer->window, caption = fixture.peer->caption_window();
        SendMessageW(edit, EM_SETSEL, 1, 4);
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"EDIT"));
        DWORD start{}, end{};
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        const auto undo = SendMessageW(edit, EM_CANUNDO, 0, 0);
        for (unsigned index = 0; index != 32; ++index) fixture.peer->sync(GetDpiForWindow(edit), fixture.palette);
        DWORD after_start{}, after_end{};
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&after_start), reinterpret_cast<LPARAM>(&after_end));
        require(fixture.peer->window == edit && fixture.peer->caption_window() == caption &&
            start == after_start && end == after_end && undo && SendMessageW(edit, EM_CANUNDO, 0, 0) == undo,
            "Repeated TextInput phases preserve real HWNDs, native selection and undo");
        require(SendMessageW(edit, EM_UNDO, 0, 0) && native_text(edit) == L"native input" &&
            input->text() == L"native input", "Actual native undo after repeated sync restores exact native and model text");
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&after_start), reinterpret_cast<LPARAM>(&after_end));
        require(after_start == 1 && after_end == 4 && input->selection() == TextInput::Selection{1, 4},
            "Native undo restores the exact replaced native and model selection");
        fixture.peer.reset();
        require(!IsWindow(edit) && !IsWindow(caption), "TextInput retires both native identities");
        for (const bool rich : {false, true}) {
            std::shared_ptr<DocumentText> document = rich ? std::static_pointer_cast<DocumentText>(std::make_shared<RichText>()) :
                std::make_shared<MultilineText>();
            document->set_text(L"native document");
            fixture.peer = std::make_unique<DocumentTextHostPeer>(fixture, fixture, document);
            fixture.attach();
            const auto native = fixture.peer->window;
            if (rich) {
                SendMessageW(native, EM_SETSEL, 0, 6);
                SendMessageW(native, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"edited"));
                require(document->text() == L"edited document", "Rich document edits remain native-owned");
            } else require(document->replace_range({0, 6}, document->text(), L"edited") == TextSelection{6, 6},
                "Plain DocumentText retains native range replacement");
            const auto before = document->selection();
            for (unsigned index = 0; index != 16; ++index) fixture.peer->sync(GetDpiForWindow(native), fixture.palette);
            require(fixture.peer->window == native && document->selection() == before,
                "Document phase updates preserve native identity and selection");
            require(document->command(TextCommand::undo) && document->text() == L"native document" &&
                native_text(native) == L"native document", "Actual document undo after repeated sync restores exact native and model text");
            require(document->command(TextCommand::redo) && document->text() == L"edited document" &&
                native_text(native) == L"edited document", "Actual document redo after repeated sync restores exact native and model text");
            fixture.peer.reset();
            require(!IsWindow(native) && !document->command(TextCommand::undo), "Document commands detach on owner retirement");
        }
        auto password = std::make_shared<PasswordInput>();
        password->set_password(L"fixture-secret");
        unsigned password_changes{};
        password->on_change([&] { ++password_changes; });
        fixture.peer = std::make_unique<PasswordHostPeer>(fixture, fixture, password);
        fixture.attach();
        const auto secret = fixture.peer->window;
        const auto clipboard = GetClipboardSequenceNumber();
        for (unsigned index = 0; index != 32; ++index) fixture.peer->sync(GetDpiForWindow(secret), fixture.palette);
        require(fixture.peer->window == secret && (GetWindowLongPtrW(secret, GWL_STYLE) & ES_PASSWORD) &&
            SendMessageW(secret, EM_GETPASSWORDCHAR, 0, 0) && password_changes == 0 &&
            GetClipboardSequenceNumber() == clipboard,
            "Password phases retain masked native identity without user callbacks or clipboard mutation");
        fixture.peer.reset();
        require(!IsWindow(secret), "Password owner retires its native identity");
        for (const auto presentation : {DateTimePresentation::date, DateTimePresentation::time, DateTimePresentation::calendar}) {
            auto date = std::make_shared<DateTimePicker>(L"Retained date", presentation);
            unsigned changes{};
            date->on_change([&](DateTimeValue) { ++changes; });
            fixture.peer = std::make_unique<DateTimeHostPeer>(fixture, fixture, date);
            fixture.attach();
            const auto native = fixture.peer->window;
            wchar_t cls[128]{};
            require(GetClassNameW(native, cls, 128) &&
                std::wstring_view(cls) == (presentation == DateTimePresentation::calendar ? MONTHCAL_CLASSW : DATETIMEPICK_CLASSW),
                "Date/time presentation uses its exact real native control class");
            if (presentation != DateTimePresentation::calendar)
                require(((GetWindowLongPtrW(native, GWL_STYLE) & DTS_TIMEFORMAT) == DTS_TIMEFORMAT) ==
                    (presentation == DateTimePresentation::time), "Date and time use distinct real native formats");
            const DateTimeValue minimum{2025, 2, 3}, maximum{2030, 11, 12};
            const DateTimeValue expected{2027, 6, 7, presentation == DateTimePresentation::date ? 0 : 12};
            date->set_range(minimum, maximum); date->set_value(expected);
            for (unsigned index = 0; index != 16; ++index) fixture.peer->sync(GetDpiForWindow(native), fixture.palette);
            SYSTEMTIME range[2]{}, value{};
            const bool calendar = presentation == DateTimePresentation::calendar;
            const auto flags = SendMessageW(native, calendar ? MCM_GETRANGE : DTM_GETRANGE, 0, reinterpret_cast<LPARAM>(range));
            const auto valid = SendMessageW(native, calendar ? MCM_GETCURSEL : DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&value));
            const bool selected = calendar ? same_date(value, expected) : date_value(value) == expected;
            const bool ranges = calendar ? same_date(range[0], minimum) && same_date(range[1], maximum) :
                date_value(range[0]) == minimum && date_value(range[1]) == maximum;
            const bool exact = fixture.peer->window == native && flags == (GDTR_MIN | GDTR_MAX) &&
                ranges && date->minimum() == minimum && date->maximum() == maximum &&
                (calendar ? valid != 0 : valid == GDT_VALID) && selected &&
                date->value() == expected && changes == 0;
            if (!exact) {
                std::cerr << "Native date readback presentation=" << static_cast<int>(presentation) <<
                    " flags=" << flags << " valid=" << valid << " callbacks=" << changes;
                for (const auto& actual : {range[0], range[1], value}) std::cerr << " value=" <<
                    actual.wYear << ',' << actual.wMonth << ',' << actual.wDay << ',' <<
                    actual.wHour << ',' << actual.wMinute << ',' << actual.wSecond;
                std::cerr << '\n';
            }
            require(exact,
                "All three presentations apply exact native range/value updates silently without replacing HWND identity");
            if (calendar) {
                value.wDay = 8;
                require(SendMessageW(native, MCM_SETCURSEL, 0, reinterpret_cast<LPARAM>(&value)) &&
                    SendMessageW(native, MCM_GETCURSEL, 0, reinterpret_cast<LPARAM>(&value)),
                    "Apply and observe a real native calendar date selection");
                require(same_date(value, {2027, 6, 8}), "Real calendar selection returns the exact selected native date");
                std::cout << "Observed native calendar clock=" << value.wHour << ',' << value.wMinute << ',' << value.wSecond << '\n';
                NMSELCHANGE change{};
                change.nmhdr = {native, 101, MCN_SELCHANGE}; change.stSelStart = value;
                change.stSelStart.wHour = 13; change.stSelStart.wMinute = 14; change.stSelStart.wSecond = 15;
                change.stSelEnd = change.stSelStart;
                SendMessageW(fixture.window, WM_NOTIFY, 101, reinterpret_cast<LPARAM>(&change));
                require(date->value() == DateTimeValue{2027, 6, 8, 12, 0, 0} && changes == 1,
                    "One real calendar selection notification preserves the entire model clock rather than importing a controlled different native payload clock");
            }
            fixture.peer.reset();
            require(!IsWindow(native), "Date/time owner retires the real native window");
        }
        require(descendants(fixture.window) == 0 && !fixture.failure, "Every concrete native owner retires without failure or children");
    };
    cycle();
    const auto gdi = xui::test::gui_resources(GR_GDIOBJECTS);
    const auto user = xui::test::gui_resources(GR_USEROBJECTS);
    for (unsigned index = 0; index != 8; ++index) cycle();
    require(fixture.retirements == 63 && xui::test::gui_resources(GR_GDIOBJECTS) == gdi &&
        xui::test::gui_resources(GR_USEROBJECTS) == user,
        "Nine seven-owner cycles retire exactly 63 owners with no GDI or USER growth");
}
void callback_retirement() {
    for (const bool throws : {false, true}) {
        Fixture fixture;
        auto password = std::make_shared<PasswordInput>();
        fixture.peer = std::make_unique<PasswordHostPeer>(fixture, fixture, password);
        fixture.attach();
        unsigned changes{};
        password->on_change([&] {
            ++changes;
            fixture.peer.reset();
            if (throws) throw std::runtime_error("Controlled native password callback failure");
        });
        const auto native = fixture.peer->window;
        SendMessageW(native, WM_IME_STARTCOMPOSITION, 0, 0);
        SetWindowTextW(native, L"committed");
        require(changes == 0 && password->length() == 0, "Composition does not publish an intermediate native password");
        SendMessageW(native, WM_IME_ENDCOMPOSITION, 0, 0);
        require(changes == 1 && password->length() == 9 && !fixture.peer && !IsWindow(native) &&
            fixture.retirements == 1 && fixture.failures == (throws ? 1u : 0u) && bool(fixture.failure) == throws,
            "IME completion publishes once and supports callback destruction and explicit failure without post-destruction access");
    }
    for (const bool throws : {false, true}) {
        Fixture fixture;
        auto date = std::make_shared<DateTimePicker>();
        date->set_value({2026, 1, 1, 13, 14, 15});
        fixture.peer = std::make_unique<DateTimeHostPeer>(fixture, fixture, date);
        fixture.attach();
        unsigned changes{};
        date->on_change([&](DateTimeValue) {
            ++changes; fixture.peer.reset();
            if (throws) throw std::runtime_error("Controlled native date callback failure");
        });
        const auto native = fixture.peer->window;
        NMDATETIMECHANGE change{};
        change.nmhdr = {native, 101, DTN_DATETIMECHANGE};
        change.dwFlags = GDT_VALID; change.st.wYear = 2026; change.st.wMonth = 3; change.st.wDay = 9;
        SendMessageW(fixture.window, WM_NOTIFY, 101, reinterpret_cast<LPARAM>(&change));
        require(changes == 1 && date->value() == DateTimeValue{2026, 3, 9, 13, 14, 15} &&
            !fixture.peer && !IsWindow(native) && fixture.retirements == 1 &&
            fixture.failures == (throws ? 1u : 0u) && bool(fixture.failure) == throws,
            "Date notification preserves time and supports callback destruction and explicit failure exactly once");
    }
}
}
int main(int argc, char**) {
    if (argc != 1) { std::cerr << "This native editor fixture accepts no selectors\n"; return 2; }
    try {
        require(SUCCEEDED(OleInitialize(nullptr)), "Initialize native editor fixture COM");
        struct Com { ~Com() { OleUninitialize(); } } com;
        std::cout << "Partial native attachment\n" << std::flush; partial_attachment();
        std::cout << "Native owner identity and retirement\n" << std::flush; native_ownership();
        std::cout << "Composition and destructive native callbacks\n" << std::flush; callback_retirement();
        std::cout << "Native partial attachment, 63-owner retirement, identity/undo, composition and destructive callbacks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

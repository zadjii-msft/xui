#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/reveal.hpp"
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
constexpr UINT update = WM_APP + 12, metrics = WM_APP + 60;
constexpr UINT_PTR driver_timer = 397;
constexpr float document_height = 180;
const std::wstring original = L"Bold document\rSecond paragraph";

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void success(HRESULT result, const char* message) { require(SUCCEEDED(result), message); }
HWND document_peer(HWND owner) {
    HWND result{};
    EnumChildWindows(owner, [](HWND child, LPARAM context) -> BOOL {
        wchar_t name[64]{};
        GetClassNameW(child, name, 64);
        if (_wcsicmp(name, MSFTEDIT_CLASS) != 0) return TRUE;
        *reinterpret_cast<HWND*>(context) = child;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&result));
    require(result != nullptr, "Find the owned native RichEdit document");
    return result;
}
std::wstring native_text(HWND editor) {
    GETTEXTLENGTHEX length_request{GTL_PRECISE | GTL_NUMCHARS, 1200};
    const auto length = SendMessageW(editor, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length_request), 0);
    require(length >= 0 && length <= 1024, "Native fixture text stays bounded");
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GETTEXTEX request{static_cast<DWORD>(value.size() * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr};
    const auto copied = SendMessageW(editor, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&request), reinterpret_cast<LPARAM>(value.data()));
    require(copied >= 0 && copied <= length, "Read the actual native document text");
    value.resize(static_cast<std::size_t>(copied));
    return value;
}
TextSelection native_selection(HWND editor) {
    CHARRANGE value{};
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&value));
    require(value.cpMin >= 0 && value.cpMax >= value.cpMin, "Read a valid native selection");
    return {static_cast<std::size_t>(value.cpMin), static_cast<std::size_t>(value.cpMax)};
}

// UIA runs off the window thread. The caller only uses this at settled geometry.
std::vector<int> uia_contract(HWND owner, HWND editor, bool read_only, bool enabled) {
    std::atomic<bool> complete{};
    std::exception_ptr error;
    std::vector<int> identity;
    std::thread client([&] {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            success(initialized, "Initialize the native UIA client apartment");
            ComPtr<IUIAutomation> automation;
            success(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)),
                "Create the native UIA client");
            ComPtr<IUIAutomationElement> element;
            success(automation->ElementFromHandle(editor, &element), "Get the retained RichEdit UIA provider");
            BOOL actual_enabled{};
            success(element->get_CurrentIsEnabled(&actual_enabled), "Read native UIA interaction state");
            require((actual_enabled != FALSE) == enabled, "Native UIA follows logical input availability");
            ComPtr<IUIAutomationTextPattern> text;
            success(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text)),
                "Reveal preserves the native RichEdit Text pattern");
            ComPtr<IUIAutomationTextRange> range;
            success(text->get_DocumentRange(&range), "Get the native document range");
            BSTR contents{};
            success(range->GetText(1024, &contents), "Read bounded text through native UIA");
            const std::wstring value = contents ? contents : L"";
            SysFreeString(contents);
            require(value.find(L"Bold document") != std::wstring::npos &&
                value.find(L"Second paragraph!") != std::wstring::npos, "UIA reads the edited live document");
            if (enabled) {
                VARIANT attribute{};
                success(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &attribute), "Read native UIA read-only semantics");
                const bool matches = attribute.vt == VT_BOOL && (attribute.boolVal != VARIANT_FALSE) == read_only;
                VariantClear(&attribute);
                require(matches, "Native UIA Text reports the authored read-only state");
                RECT bounds{}, native{};
                success(element->get_CurrentBoundingRectangle(&bounds), "Read settled native UIA geometry");
                require(GetWindowRect(editor, &native) != FALSE, "Read settled RichEdit geometry");
                require(bounds.right > bounds.left && bounds.bottom > bounds.top &&
                    std::abs(bounds.left - native.left) <= 2 && std::abs(bounds.top - native.top) <= 2 &&
                    std::abs(bounds.right - native.right) <= 2 && std::abs(bounds.bottom - native.bottom) <= 2,
                    "Fully open UIA bounds match the native document, not a replacement provider");
            }
            SAFEARRAY* runtime{};
            success(element->GetRuntimeId(&runtime), "Read native UIA identity");
            require(runtime != nullptr, "Native UIA identity is present");
            struct Array { SAFEARRAY* value; ~Array() { SafeArrayDestroy(value); } } array{runtime};
            LONG first{}, last{};
            success(SafeArrayGetLBound(runtime, 1, &first), "Read UIA identity lower bound");
            success(SafeArrayGetUBound(runtime, 1, &last), "Read UIA identity upper bound");
            for (LONG index = first; index <= last; ++index) {
                int part{};
                success(SafeArrayGetElement(runtime, &index, &part), "Read UIA identity component");
                identity.push_back(part);
            }
            require(!identity.empty(), "Native UIA identity is nonempty");
        } catch (...) { error = std::current_exception(); }
        if (SUCCEEDED(initialized)) CoUninitialize();
        complete = true;
        PostMessageW(owner, WM_NULL, 0, 0);
    });
    bool quit{};
    WPARAM exit_code{};
    while (!complete) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { quit = true; exit_code = message.wParam; }
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    client.join();
    if (quit) PostQuitMessage(static_cast<int>(exit_code));
    if (error) std::rethrow_exception(error);
    return identity;
}

struct Fixture {
    Window window{{L"XUI native document Reveal acceptance", {560, 420}, ThemeMode::light}};
    std::shared_ptr<Stack> root = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<Button> outside = std::make_shared<Button>(L"Return to document list");
    std::shared_ptr<DocumentText> document;
    std::shared_ptr<Reveal> reveal;
    HWND owner{}, editor{}, clip{}, outside_peer{};
    bool rich{}, expanding{}, motion{}, done{}, stepping{}, observing{}, watching{}, tracking_layout{};
    bool entry_middle{}, exit_middle{};
    unsigned phase{}, changes{}, expected_changes{1}, frames{};
    float entry_progress{};
    LRESULT entry_layouts{}, idle_layouts{}, idle_paints{};
    Animation::Clock::time_point started{}, idle_since{};
    std::exception_ptr error;
    std::vector<int> identity;
    static Fixture* current;

    Fixture(bool styled, bool expand) : rich(styled), expanding(expand) {
        if (rich) {
            auto value = std::make_shared<RichText>(L"Animated styled document");
            value->set_runs({{L"Bold", true}, {L" document\rSecond paragraph"}});
            document = value;
        } else {
            document = std::make_shared<MultilineText>(L"Animated plain document");
            document->set_text(original);
        }
        document->set_preferred_size({480, document_height});
        document->set_selection({original.size(), original.size()});
        PartStyleValues field;
        field.padding = Insets{};
        field.border_thickness = Insets{};
        document->set_control_style(ControlStyle::create(
            rich ? StyleTarget::rich_text : StyleTarget::multiline_text, {{StylePart::root, field}}, {}));
        document->on_change([this](const auto&) { ++changes; });
        reveal = std::make_shared<Reveal>(document, L"Document reveal");
        reveal->set_layout(expanding ? RevealLayout::expand : RevealLayout::fixed);
        reveal->set_duration(2400);
        root->set_padding({12, 12, 12, 12});
        root->add(outside, 1);
        root->add(reveal);
        window.set_content(root);
    }
    ~Fixture() {
        if (IsWindow(owner)) {
            KillTimer(owner, driver_timer);
            RemoveWindowSubclass(owner, observe, 1);
        }
    }
    void flush() { SendMessageW(owner, update, 0, 0); UpdateWindow(owner); }
    void retained() {
        require(IsWindow(editor) && document_peer(owner) == editor && GetParent(editor) == clip &&
            reveal->content() == document && reveal->retained_children().size() == 1,
            "Motion preserves the native document, parent, and retained ownership");
        require(native_text(editor) == original + L"!" && document->text() == original + L"!" && changes == expected_changes,
            "Motion preserves edited text without property replacement or duplicate callbacks");
        require(native_selection(editor) == TextSelection{1, 4} && document->selection() == TextSelection{1, 4},
            "Motion preserves native and retained UTF-16 selection");
        require(SendMessageW(editor, EM_CANUNDO, 0, 0) != 0, "Motion preserves the native undo history");
    }
    void geometry() {
        const auto bounds = document->bounds();
        require(std::abs(bounds.height - document_height) < 0.02f, "The animated document retains its full layout height");
        RECT native{}, viewport{};
        require(GetWindowRect(editor, &native) != FALSE && GetClientRect(clip, &viewport) != FALSE,
            "Read the live editor and native clipping host");
        MapWindowPoints(clip, nullptr, reinterpret_cast<POINT*>(&viewport), 2);
        POINT origin{};
        require(ClientToScreen(owner, &origin) != FALSE, "Read the owned client origin");
        const float scale = GetDpiForWindow(editor) / 96.0f;
        require(std::abs(native.top - origin.y - bounds.y * scale) <= 1.1f &&
            std::abs(native.bottom - native.top - document_height * scale) <= 1.1f,
            "Actual native placement follows the full-size retained document");
        RECT visible{};
        require(IntersectRect(&visible, &native, &viewport) != FALSE &&
            visible.bottom - visible.top > 0 && visible.bottom - visible.top < native.bottom - native.top,
            "A real intermediate frame exposes only part of the full native document");
        require(std::abs((visible.bottom - visible.top) / scale - document_height * reveal->progress()) <= 2,
            "The native clipping extent follows the actual animation progress");
        POINT inside{(visible.left + visible.right) / 2, (visible.top + visible.bottom) / 2};
        POINT clipped{inside.x, viewport.bottom + 4};
        require(clipped.y < native.bottom, "The native document extends past its clipping host");
        require(ScreenToClient(editor, &inside) && ScreenToClient(editor, &clipped), "Map native clipping probes");
        const auto dc = GetDC(editor);
        require(dc != nullptr, "Read the actual RichEdit paint clip");
        const bool inside_visible = PtVisible(dc, inside.x, inside.y) != FALSE;
        const bool outside_visible = PtVisible(dc, clipped.x, clipped.y) != FALSE;
        ReleaseDC(editor, dc);
        require(inside_visible && !outside_visible, "Windows clips the real document paint DC at the Reveal edge");
        if (tracking_layout) {
            const auto layouts = SendMessageW(owner, metrics, 2, 0);
            // A queued paint update after retargeting can repeat the baseline geometry.
            const bool advanced = reveal->progress() != entry_progress;
            const bool layout_matches = expanding ? !advanced || layouts > entry_layouts : layouts == entry_layouts;
            if (!layout_matches)
                std::cerr << "Layout mismatch: rich=" << rich << " expand=" << expanding << " phase=" << phase
                    << " open=" << reveal->open() << " progress=" << reveal->progress()
                    << " baseline-progress=" << entry_progress << " layouts=" << layouts
                    << " baseline-layouts=" << entry_layouts << '\n';
            require(layout_matches, "Changed expanding document geometry requests layout; fixed intermediate frames only place");
        }
    }
    void fail() {
        if (!error) error = std::current_exception();
        done = true;
        KillTimer(owner, driver_timer);
        window.close();
    }
    static LRESULT CALLBACK observe(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR, DWORD_PTR context) noexcept {
        auto& self = *reinterpret_cast<Fixture*>(context);
        const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
        // Placement must finish before comparing HWNDs with the model sampled by the real timer.
        if (message == update && self.watching && !self.done && !self.observing &&
            self.reveal->animating() && self.reveal->progress() > 0.15f && self.reveal->progress() < 0.85f) {
            self.observing = true;
            try {
                self.geometry();
                ++self.frames;
                if (self.reveal->open()) self.entry_middle = true;
                else self.exit_middle = true;
            } catch (...) { self.fail(); }
            self.observing = false;
        }
        return result;
    }
    void begin() {
        require(window.focus(*outside), "The owned document fixture receives native focus");
        outside_peer = GetFocus();
        owner = GetAncestor(outside_peer, GA_ROOT);
        editor = document_peer(owner);
        clip = GetParent(editor);
        require(clip && clip != owner, "The native document has a retained clipping host");
        BOOL enabled{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != FALSE,
            "Read system motion without changing it");
        motion = enabled != FALSE;
        require(SetWindowSubclass(owner, observe, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
            "Observe completed placement in the owned window");
        started = Animation::Clock::now();
        watching = true;
        reveal->set_open(true);
        require(reveal->open() && reveal->progress() == 0, "Logical entry precedes the first scheduled frame");
        require(window.focus(*document) && GetFocus() == editor, "The actual native document receives immediate opening focus");
        require(!motion || (reveal->animating() && reveal->progress() < 1), "The first character reaches an opening document");
        SendMessageW(editor, WM_CHAR, L'!', 0);
        require(document->text() == original + L"!" && changes == 1,
            "The first native character survives entry before animation completes");
        document->set_selection({1, 4});
        flush();
        retained();
        entry_layouts = SendMessageW(owner, metrics, 2, 0);
        entry_progress = reveal->progress();
        tracking_layout = true;
        require((SendMessageW(owner, metrics, 33, 0) != 0) == motion && reveal->animating() == motion,
            "Document entry uses the shared timer and respects reduced motion");
        require(SetTimer(owner, driver_timer, 15, tick) != 0, "Start the bounded document acceptance driver");
    }
    void close_document() {
        watching = false;
        reveal->set_open(false);
        require(!reveal->open() && !window.focus(*document), "Logical close rejects document focus immediately");
        require(window.focus(*outside) && GetFocus() == outside_peer, "Close returns actual focus to the application target");
        flush();
        require(!IsWindowEnabled(editor) && !document->command(TextCommand::undo),
            "Outgoing native text rejects interaction and semantic editing commands");
        retained();
        entry_layouts = SendMessageW(owner, metrics, 2, 0);
        entry_progress = reveal->progress();
        watching = true;
    }
    void step() {
        const auto now = Animation::Clock::now();
        require(now - started < std::chrono::seconds(30), "Native document animation acceptance timed out");
        if (phase == 0) {
            retained();
            require(GetFocus() == editor, "Entry preserves actual native focus");
            if (reveal->animating()) return;
            require(!motion || entry_middle, "Actual timer delivery produces an intermediate native document entry");
            watching = false;
            flush();
            identity = uia_contract(owner, editor, false, true);
            document->set_read_only(true);
            flush();
            require((GetWindowLongPtrW(editor, GWL_STYLE) & ES_READONLY) != 0 &&
                !document->command(TextCommand::undo), "Native read-only mode blocks editing without replacing the document");
            SendMessageW(editor, WM_CHAR, L'?', 0);
            retained();
            require(uia_contract(owner, editor, true, true) == identity, "Read-only changes preserve native UIA identity");
            document->set_read_only(false);
            flush();
            retained();
            close_document();
            phase = motion ? 1u : 2u;
            if (!motion) {
                reveal->set_open(true);
                require(window.focus(*document) && GetFocus() == editor, "Reduced-motion reopening preserves native identity");
                flush();
            }
        } else if (phase == 1) {
            retained();
            if (!exit_middle && reveal->animating()) return;
            require(exit_middle && reveal->animating(), "Actual timer delivery exposes an outgoing document before reversal");
            const auto before = reveal->progress();
            watching = false;
            reveal->set_open(true);
            require(reveal->progress() > 0 && reveal->progress() <= before, "Reversal starts from the current outgoing presentation");
            require(window.focus(*document) && GetFocus() == editor, "Reversal restores focus to the same live RichEdit");
            flush();
            entry_layouts = SendMessageW(owner, metrics, 2, 0);
            entry_progress = reveal->progress();
            watching = true;
            phase = 2;
        } else if (phase == 2) {
            retained();
            require(GetFocus() == editor, "Reopening preserves the native focus target");
            if (reveal->animating()) return;
            watching = false;
            flush();
            require(uia_contract(owner, editor, false, true) == identity, "Reversal preserves the native Text provider and UIA identity");
            if (rich) {
                CHARRANGE range{0, 4};
                SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
                CHARFORMAT2W format{sizeof(format)};
                SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
                require((format.dwMask & CFM_BOLD) && (format.dwEffects & CFE_BOLD), "RichEdit retains authored bold runs across motion");
                document->set_selection({1, 4});
                flush();
            }
            require(document->command(TextCommand::undo) && document->text() == original && native_text(editor) == original,
                "Native undo after reversal removes only the real typed character");
            require(document->command(TextCommand::redo) && document->text() == original + L"!" &&
                native_text(editor) == original + L"!" && changes == 3, "Native redo restores the edit with one callback per command");
            expected_changes = 3;
            document->set_selection({1, 4});
            flush();
            close_document();
            phase = 3;
        } else if (phase == 3) {
            if (reveal->animating()) return;
            watching = false;
            flush();
            retained();
            require(reveal->progress() == 0 && reveal->bounds().height == 0 && !IsWindowVisible(editor),
                "Completed exit releases layout and hides, rather than destroys, the native document");
            require(uia_contract(owner, editor, false, false) == identity, "Closed native UIA is disabled without losing its Text provider identity");
            require(SendMessageW(owner, metrics, 33, 0) == 0, "Completed document motion releases the shared timer");
            require(window.focus(*outside) && GetFocus() == outside_peer,
                "The unrelated anchor owns focus before the idle paint baseline");
            flush();
            idle_layouts = SendMessageW(owner, metrics, 2, 0);
            idle_paints = SendMessageW(owner, metrics, 0, 0);
            idle_since = Animation::Clock::now();
            phase = 4;
        } else if (phase == 4) {
            if (now - idle_since < std::chrono::milliseconds(180)) return;
            require(GetFocus() == outside_peer && SendMessageW(owner, metrics, 33, 0) == 0,
                "Idle observation retains anchor focus without an animation timer");
            require(SendMessageW(owner, metrics, 2, 0) == idle_layouts &&
                SendMessageW(owner, metrics, 0, 0) == idle_paints, "Closed documents cause no periodic root layout or paint");
            reveal->set_duration(0);
            reveal->set_open(true);
            require(window.focus(*document) && GetFocus() == editor, "Zero-duration entry retains the document peer");
            flush();
            require(reveal->progress() == 1 && !reveal->animating() && SendMessageW(owner, metrics, 33, 0) == 0,
                "Zero-duration document entry requires no animation timer");
            retained();
            reveal->set_duration(2400);
            close_document();
            watching = false;
            ShowWindow(owner, SW_HIDE);
            flush();
            require(!reveal->animating() && reveal->progress() == 0 && SendMessageW(owner, metrics, 33, 0) == 0,
                "Hiding the owner settles outgoing document motion and removes the clock");
            retained();
            ShowWindow(owner, SW_SHOWNOACTIVATE);
            reveal->set_duration(0);
            reveal->set_open(true);
            require(window.focus(*document) && GetFocus() == editor, "Showing the owner restores the retained document and focus");
            flush();
            retained();
            KillTimer(owner, driver_timer);
            done = true;
            std::cout << (rich ? "RichText" : "MultilineText") << (expanding ? " expand" : " fixed")
                << ": intermediate native placements=" << frames << ", system motion=" << motion << '\n';
            window.close();
        }
    }
    static void CALLBACK tick(HWND hwnd, UINT, UINT_PTR timer, DWORD) noexcept {
        if (!current || current->owner != hwnd || timer != driver_timer || current->done || current->stepping) return;
        current->stepping = true;
        try { current->step(); }
        catch (...) { current->fail(); }
        current->stepping = false;
    }
};
Fixture* Fixture::current{};
}

int main() {
    try {
        success(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Initialize the window apartment");
        struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
        for (bool rich : {false, true}) for (bool expand : {false, true}) {
            Fixture fixture(rich, expand);
            Fixture::current = &fixture;
            fixture.window.post([&] { fixture.begin(); });
            const auto result = Application::run(fixture.window);
            Fixture::current = nullptr;
            if (fixture.error) std::rethrow_exception(fixture.error);
            if (result) std::wcerr << fixture.window.error() << L'\n';
            require(result == 0 && fixture.done && !fixture.reveal->animating(), "Native document animation acceptance completed");
        }
        std::cout << "Document Reveal: native clipping, focus, text, selection, undo, UIA, reversal, hidden owner, and idle passed\n";
        return 0;
    } catch (const std::exception& error) {
        Fixture::current = nullptr;
        std::cerr << error.what() << '\n';
        return 1;
    }
}

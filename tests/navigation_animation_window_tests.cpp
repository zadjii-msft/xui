#include "xui/application.hpp"
#include "xui/navigation.hpp"
#include "../src/collection_presentation.hpp"
#include "owned_window_capture.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <atomic>
#include <thread>
#include <iostream>
#include <cmath>

namespace {
using namespace xui;
using detail::CollectionPresentationAccess;
using Microsoft::WRL::ComPtr;
constexpr UINT update = WM_APP + 12, metrics = WM_APP + 60;
constexpr UINT_PTR driver_timer = 497;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
HWND named(HWND owner, const wchar_t* text) {
    struct Search { const wchar_t* text; HWND found{}; } search{text};
    EnumChildWindows(owner, [](HWND child, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        wchar_t value[128]{}; GetWindowTextW(child, value, 128);
        if (std::wstring_view(value) == s.text) { s.found = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.found != nullptr, "Find the native NavigationList peer");
    return search.found;
}
void uia(HWND owner, HWND list, bool expanded) {
    std::atomic<bool> complete{};
    std::exception_ptr error;
    std::thread client([&] {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            winrt::check_hresult(initialized);
            ComPtr<IUIAutomation> automation;
            winrt::check_hresult(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)));
            ComPtr<IUIAutomationElement> root;
            winrt::check_hresult(automation->ElementFromHandle(list, &root));
            const auto find = [&](const wchar_t* id) {
                VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
                ComPtr<IUIAutomationCondition> condition;
                const auto result = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
                VariantClear(&value); winrt::check_hresult(result);
                ComPtr<IUIAutomationElement> element;
                winrt::check_hresult(root->FindFirst(TreeScope_Descendants, condition.Get(), &element));
                return element;
            };
            auto group = find(L"10:3");
            require(group != nullptr, "The actual navigation group has native UIA identity");
            ComPtr<IUIAutomationExpandCollapsePattern> disclosure;
            winrt::check_hresult(group->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&disclosure)));
            ExpandCollapseState state{};
            winrt::check_hresult(disclosure->get_CurrentExpandCollapseState(&state));
            require(state == (expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed),
                "Native group UIA exposes the immediate logical disclosure state");
            require((find(L"11:3") != nullptr) == expanded, "Outgoing children leave native UIA immediately");
        } catch (...) { error = std::current_exception(); }
        if (SUCCEEDED(initialized)) CoUninitialize();
        complete = true; PostMessageW(owner, WM_NULL, 0, 0);
    });
    while (!complete) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { PostQuitMessage(static_cast<int>(message.wParam)); break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    client.join();
    if (error) std::rethrow_exception(error);
}
struct Fixture {
    Window window;
    std::shared_ptr<NavigationView> navigation = std::make_shared<NavigationView>(L"Motion navigation");
    std::shared_ptr<ContentHost> content = std::make_shared<ContentHost>(navigation);
    std::shared_ptr<Button> anchor = std::make_shared<Button>(L"Unrelated anchor");
    HWND owner{}, list{}, editor{}, expected_focus{}, idle_focus{};
    RECT editor_bounds{};
    bool motion{}, done{}, stepping{}, observing{}, watching{}, closing_middle{}, opening_middle{}, pixels_checked{}, measuring_idle{};
    unsigned phase{}, frames{}, idle_updates{}, idle_paint_messages{};
    LRESULT frame_layouts{}, idle_paints{}, idle_layouts{};
    Animation::Clock::time_point started{}, idle_since{};
    std::shared_ptr<const ItemsSource> logical;
    std::exception_ptr error;
    static Fixture* current;

    explicit Fixture(VisualStyle style) : window{{L"Native navigation disclosure", {620, 500}, ThemeMode::light, {}, false, style}} {
        std::vector<NavigationItem> rows;
        NavigationItem group; group.key = {10, 3}; group.label = L"Sample group"; group.selectable = false;
        rows.push_back(group);
        for (std::uint64_t id = 11; id <= 14; ++id) {
            NavigationItem child; child.key = {id, 3}; child.parent = group.key;
            child.label = L"Sample child"; child.icon = ButtonIcon::folder; rows.push_back(child);
        }
        NavigationItem tail; tail.key = {20, 3}; tail.label = L"Sample tail"; rows.push_back(tail);
        navigation->set_items(std::move(rows));
        navigation->select({11, 3});
        navigation->search()->set_caption_visible(false);
        navigation->set_duration(4000);
        PartStyleValues face, selected;
        face.background = ThemeColor{0x193A71}; face.corner_radius = 0.0f;
        selected.background = ThemeColor{0x12B456};
        navigation->items()->set_control_style(ControlStyle::create(StyleTarget::navigation_list,
            {{StylePart::row, face}}, {{StylePart::row, style_states::selected, selected}}));
        auto root = std::make_shared<Stack>(Axis::horizontal);
        content->set_preferred_size({280, 500});
        root->add(content); root->add(anchor, 1); window.set_content(root);
    }
    ~Fixture() {
        if (IsWindow(owner)) { KillTimer(owner, driver_timer); RemoveWindowSubclass(owner, observe, 1); }
    }
    void flush() { SendMessageW(owner, update, 0, 0); UpdateWindow(owner); }
    void begin_idle() {
        require(window.focus(*anchor) && anchor->focused() && GetFocus() != editor,
            "The unrelated anchor owns focus before idle measurement");
        idle_focus = GetFocus();
        const auto paints = SendMessageW(owner, metrics, 0, 0);
        const auto layouts = SendMessageW(owner, metrics, 2, 0);
        unsigned delivered{};
        MSG message{};
        // SendMessage(update) does not consume the update already posted by a focus change.
        // Dispatch that finite setup work and paint existing damage before fixing the baseline.
        for (;;) {
            while (PeekMessageW(&message, owner, update, update, PM_REMOVE)) {
                require(++delivered <= 32, "Navigation idle setup did not drain its posted updates");
                DispatchMessageW(&message);
            }
            require(RedrawWindow(owner, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN) != FALSE,
                "Paint existing owner and native-editor damage before idle observation");
            if (!PeekMessageW(&message, owner, update, update, PM_NOREMOVE)) break;
        }
        require(GetFocus() == idle_focus && anchor->focused() && !navigation->animating() &&
            !CollectionPresentationAccess::get(*navigation->items()) && SendMessageW(owner, metrics, 33, 0) == 0,
            "Idle setup preserves anchor focus and the completed animation state");
        idle_paints = SendMessageW(owner, metrics, 0, 0);
        idle_layouts = SendMessageW(owner, metrics, 2, 0);
        std::cout << "Navigation idle setup: style=" << static_cast<int>(window.visual_style())
            << " updates=" << delivered << " paints=" << idle_paints - paints
            << " layouts=" << idle_layouts - layouts << '\n';
        idle_updates = idle_paint_messages = 0;
        idle_since = Animation::Clock::now();
        measuring_idle = true;
    }
    void input() {
        RECT bounds{};
        require(IsWindow(editor) && GetWindowRect(editor, &bounds) && EqualRect(&bounds, &editor_bounds) &&
            navigation->search()->text() == L"s" && GetFocus() == expected_focus,
            "Navigation motion preserves real native search identity, position, text, and intended focus");
        DWORD first{}, last{};
        SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
        require(first == 1 && last == 1 && SendMessageW(editor, EM_CANUNDO, 0, 0),
            "Navigation motion preserves native search selection and undo");
    }
    double tail() const {
        const auto& items = navigation->items();
        return items->item_bounds(*items->source()->find({20, 3})).y - items->content_viewport().y;
    }
    void fail() {
        if (!error) error = std::current_exception();
        done = true; KillTimer(owner, driver_timer); window.close();
    }
    static LRESULT CALLBACK observe(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR, DWORD_PTR context) noexcept {
        auto& self = *reinterpret_cast<Fixture*>(context);
        if (self.measuring_idle) {
            if (message == update) ++self.idle_updates;
            if (message == WM_PAINT) ++self.idle_paint_messages;
        }
        const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
        if (message == update && self.watching && !self.observing && !self.done) {
            self.observing = true;
            try {
                const auto& items = self.navigation->items();
                if (self.navigation->animating() && self.tail() > 42 && self.tail() < 195) {
                    require(items->source() == self.logical, "Timer frames never replace the logical NavigationSnapshot");
                    require(SendMessageW(hwnd, metrics, 2, 0) == self.frame_layouts,
                        "Navigation row motion performs no intermediate root layout");
                    self.input();
                    if (!self.navigation->item_expanded({10, 3})) {
                        const auto viewport = items->content_viewport();
                        require(!items->hit_test({viewport.x + 20, viewport.y + 41}),
                            "Draw-only outgoing navigation rows reject native-model hits");
                        self.closing_middle = true;
                    } else self.opening_middle = true;
                    ++self.frames;
                }
            } catch (...) { self.fail(); }
            self.observing = false;
        }
        return result;
    }
    void arm() {
        logical = navigation->items()->source();
        frame_layouts = SendMessageW(owner, metrics, 2, 0);
        watching = true;
    }
    void begin() {
        require(window.focus(*navigation->search()), "Focus the real navigation search editor");
        editor = expected_focus = GetFocus(); owner = GetAncestor(editor, GA_ROOT);
        list = named(owner, L"Motion navigation items");
        SendMessageW(editor, WM_CHAR, L's', 0); flush();
        require(GetWindowRect(editor, &editor_bounds) != FALSE, "Read native search geometry");
        uia(owner, list, true);
        BOOL allowed{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &allowed, 0) != FALSE, "Read system motion without changing it");
        motion = allowed != FALSE;
        require(SetWindowSubclass(owner, observe, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Observe actual shared-clock presentation");
        started = Animation::Clock::now();
        navigation->set_item_expanded({10, 3}, false); flush(); arm();
        require(navigation->items()->selection().focused() == ItemKey{10, 3} &&
            !navigation->items()->source()->find({11, 3}), "Collapse repairs ancestor focus and retires logical children immediately");
        require(navigation->animating() == motion && (SendMessageW(owner, metrics, 33, 0) != 0) == motion,
            "The existing window scheduler supplies the system motion policy");
        uia(owner, list, false);
        require(SetTimer(owner, driver_timer, 15, tick) != 0, "Start bounded native navigation fixture");
    }
    void pixels() {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE, "Read high contrast");
        if (contrast.dwFlags & HCF_HIGHCONTRASTON) {
            std::cout << "High contrast: authored-color pixel check omitted; native geometry and UIA remain required\n";
            return;
        }
        UpdateWindow(owner);
        const auto image = owned_window_capture::capture(owner);
        POINT origin{}; MapWindowPoints(list, owner, &origin, 1);
        const auto viewport = navigation->items()->content_viewport();
        const float scale = GetDpiForWindow(list) / 96.0f;
        const int x = origin.x + static_cast<int>(std::lround((viewport.x + 180) * scale));
        const int top = origin.y + static_cast<int>(std::lround((viewport.y + 40) * scale));
        const int bottom = origin.y + static_cast<int>(std::lround((viewport.y + 80) * scale));
        require(x >= 0 && x < image.width && top >= 0 && bottom <= image.height, "Read only owned navigation capture pixels");
        unsigned green{};
        for (int y = top; y < bottom; ++y)
            if ((image.data[static_cast<std::size_t>(y) * image.width + x] & 0xffffff) == 0x12B456) ++green;
        require(green > 0 && green < static_cast<unsigned>(bottom - top),
            "Actual compositor pixels retain and clip the exiting selected child at an intermediate frame");
    }
    void step() {
        require(Animation::Clock::now() - started < std::chrono::seconds(30), "Native navigation fixture timed out");
        if (phase == 0) {
            input();
            if (motion && !pixels_checked) {
                require(navigation->animating(), "Actual timer delivery must expose an intermediate exit");
                if (tail() < 50 || tail() > 70) return;
                pixels(); pixels_checked = true;
            }
            require(!motion || closing_middle, "The actual window clock presents outgoing navigation rows");
            watching = false;
            const auto viewport = navigation->items()->content_viewport();
            const auto scale = GetDpiForWindow(list) / 96.0f;
            const auto point = MAKELPARAM(static_cast<WORD>((viewport.x + 90) * scale),
                static_cast<WORD>((viewport.y + 20) * scale));
            const auto before = tail();
            SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, point);
            SendMessageW(list, WM_LBUTTONUP, 0, point);
            expected_focus = list;
            require(navigation->item_expanded({10, 3}) && (!motion || navigation->animating()),
                "A real group-body click reverses disclosure without settling its current presentation");
            if (motion) require(std::abs(tail() - before) < 0.01, "Native disclosure reversal starts at the current presented rectangles");
            flush(); arm(); phase = 1;
        } else if (phase == 1) {
            input();
            if (navigation->animating()) return;
            require(!motion || opening_middle, "The real clock presents intermediate reopened rows");
            watching = false; flush(); uia(owner, list, true);
            require(window.focus(*navigation->search()) && GetFocus() == editor, "Return to the same native search editor");
            expected_focus = editor;
            navigation->set_item_expanded({10, 3}, false); flush(); arm(); phase = 2;
        } else if (phase == 2) {
            input();
            if (navigation->animating()) return;
            watching = false; flush();
            require(!CollectionPresentationAccess::get(*navigation->items()) && SendMessageW(owner, metrics, 33, 0) == 0,
                "Completed disclosure releases its immutable frame and shared clock");
            begin_idle(); phase = 3;
        } else if (phase == 3) {
            const auto paints = SendMessageW(owner, metrics, 0, 0);
            const auto layouts = SendMessageW(owner, metrics, 2, 0);
            const auto timer = SendMessageW(owner, metrics, 33, 0);
            const bool frame = CollectionPresentationAccess::get(*navigation->items()) != nullptr;
            const bool focused = GetFocus() == idle_focus && anchor->focused();
            const bool quiet = paints == idle_paints && layouts == idle_layouts &&
                idle_updates == 0 && idle_paint_messages == 0 && !timer && !frame && !navigation->animating() && focused;
            if (!quiet)
                std::cerr << "Navigation idle mismatch: style=" << static_cast<int>(window.visual_style())
                    << " elapsed-ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(Animation::Clock::now() - idle_since).count()
                    << " paints=" << paints << " baseline-paints=" << idle_paints
                    << " layouts=" << layouts << " baseline-layouts=" << idle_layouts
                    << " updates=" << idle_updates << " paint-messages=" << idle_paint_messages
                    << " animation-timer=" << timer << " animating=" << navigation->animating()
                    << " presentation=" << frame << " anchor-focus=" << focused << '\n';
            require(quiet, "Settled navigation has no idle updates, repaint, layout, or animation work and retains anchor focus");
            if (Animation::Clock::now() - idle_since < std::chrono::milliseconds(180)) return;
            measuring_idle = false;
            navigation->set_duration(0); navigation->set_item_expanded({10, 3}, true);
            require(window.focus(*navigation->search()) && GetFocus() == editor, "Immediate mode retains native search identity");
            expected_focus = editor; flush(); input();
            require(!navigation->animating() && tail() == 200, "Zero duration uses natural geometry immediately");
            navigation->set_duration(4000); navigation->set_item_expanded({10, 3}, false); flush();
            require(navigation->animating() == motion, "Start disclosure before hidden-owner settlement");
            ShowWindow(owner, SW_HIDE); flush();
            require(!navigation->animating() && SendMessageW(owner, metrics, 33, 0) == 0,
                "Hidden ownership settles the producer through the shared scheduler");
            ShowWindow(owner, SW_SHOWNOACTIVATE);
            navigation->set_item_expanded({10, 3}, true); flush();
            require(navigation->animating() == motion, "Visible ownership can start another disclosure");
            navigation->set_enabled(false); flush();
            require(!navigation->animating() && SendMessageW(owner, metrics, 33, 0) == 0,
                "Disabled ownership settles the producer through the shared scheduler");
            navigation->set_enabled(true);
            navigation->set_item_expanded({10, 3}, false); flush();
            require(window.focus(*anchor), "Retirement keeps focus on an unrelated control");
            require(navigation->animating() == motion, "Start disclosure before native owner retirement");
            window.replace_content(*content, {}); flush();
            require(!navigation->animating() && !IsWindow(list) && !IsWindow(editor) &&
                SendMessageW(owner, metrics, 33, 0) == 0, "Retirement removes native peers, frames, and clock work");
            done = true; KillTimer(owner, driver_timer);
            std::cout << "Navigation style=" << static_cast<int>(window.visual_style()) << " frames=" << frames
                << " system motion=" << motion << '\n';
            window.close();
        }
    }
    static void CALLBACK tick(HWND hwnd, UINT, UINT_PTR id, DWORD) noexcept {
        if (!current || current->owner != hwnd || id != driver_timer || current->done || current->stepping) return;
        current->stepping = true;
        try { current->step(); } catch (...) { current->fail(); }
        current->stepping = false;
    }
};
Fixture* Fixture::current{};
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment { ~Apartment() { winrt::clear_factory_cache(); winrt::uninit_apartment(); } } apartment;
        for (auto style : {VisualStyle::classic, VisualStyle::winui}) {
            Fixture fixture(style); Fixture::current = &fixture;
            fixture.window.post([&] { fixture.begin(); });
            const auto result = Application::run(fixture.window);
            Fixture::current = nullptr;
            if (fixture.error) std::rethrow_exception(fixture.error);
            if (result) std::wcerr << fixture.window.error() << '\n';
            require(result == 0 && fixture.done, "Native NavigationView disclosure acceptance completed");
        }
        std::cout << "Navigation actual timer, pixels, UIA, input, reversal, focus, retirement, and idle passed\n";
    } catch (const std::exception& error) {
        Fixture::current = nullptr; std::cerr << error.what() << '\n'; return 1;
    }
}

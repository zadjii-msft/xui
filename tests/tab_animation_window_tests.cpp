#include "xui/application.hpp"
#include "owned_window_capture.hpp"
#include <UIAutomation.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
constexpr UINT metrics = WM_APP + 60, update = WM_APP + 12;
constexpr UINT_PTR driver_timer = 197;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void expect_near(float actual, float expected, float tolerance = 0.02f) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, "Presented tab geometry mismatch");
}
bool system_motion() {
    BOOL enabled{};
    require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != FALSE,
        "Read system motion preference without changing it");
    return enabled != FALSE;
}
HWND named_peer(HWND host, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(host, [](HWND hwnd, LPARAM context) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(context);
        wchar_t text[128]{};
        GetWindowTextW(hwnd, text, 128);
        if (std::wstring_view(text) == state.name) { state.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find the owned native tab peer");
    return search.result;
}
unsigned peer_count(HWND host) {
    unsigned count{};
    EnumChildWindows(host, [](HWND, LPARAM context) -> BOOL {
        ++*reinterpret_cast<unsigned*>(context); return TRUE;
    }, reinterpret_cast<LPARAM>(&count));
    return count;
}
struct Fixture {
    Window window;
    std::shared_ptr<Stack> root = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<TabStrip> tabs = std::make_shared<TabStrip>(L"Animated tab strip");
    std::shared_ptr<ContentHost> content = std::make_shared<ContentHost>(tabs);
    std::shared_ptr<Button> files = std::make_shared<Button>(L"Tab content");
    HWND host{}, strip_peer{}, new_peer{};
    unsigned native_peers{}, selections{}, closes{}, creates{}, phase{};
    std::uint64_t closed_id{};
    bool motion{}, saw_middle{}, done{};
    float original_width{};
    LRESULT entry_layouts{}, idle_layouts{}, idle_paints{};
    Animation::Clock::time_point started{}, idle_since{};
    std::exception_ptr error;
    static Fixture* current;

    explicit Fixture(Size size = {480, 280})
        : window{{L"XUI animated tab acceptance", size, ThemeMode::light}} {
        root->set_padding({12, 12, 12, 12});
        tabs->set_new_tab_button_visible(true);
        tabs->set_duration(600);
        tabs->set_tabs({{11, L"First"}, {22, L"Last"}}, 11);
        tabs->on_select([this](auto) { ++selections; });
        tabs->on_close([this](auto id) { ++closes; closed_id = id; });
        tabs->on_new_tab([this] { ++creates; });
        content->set_preferred_size(tabs->measure(size));
        root->add(content);
        root->add(files, 1);
        window.set_content(root);
    }
    void flush() { SendMessageW(host, update, 0, 0); UpdateWindow(host); }
    void locate() {
        require(files->bounds().width > 0 && files->bounds().height > 0,
            "The fixture must reserve nonempty geometry for its focus target");
        require(window.focus(*files), "The owned fixture receives native focus");
        host = GetAncestor(GetFocus(), GA_ROOT);
        require(SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE, "Keep the owned point-hit fixture unobscured without activation");
        strip_peer = named_peer(host, L"Animated tab strip");
        new_peer = named_peer(host, L"New tab");
        native_peers = peer_count(host);
        motion = system_motion();
        require(!tabs->animating(), "Initial mounted tab population is immediate");
        require(SendMessageW(host, metrics, 33, 0) == 0, "Initial tab population has no animation timer");
    }
    void insert() { tabs->set_tabs({{11, L"First"}, {33, L"Incoming"}, {22, L"Last"}}, 33); }
    void reset() { tabs->set_tabs({{11, L"First"}, {22, L"Last"}}, 11); tabs->settle(); flush(); }
    void geometry(bool gaps = false, bool crossing = false) {
        require(IsWindow(strip_peer) && named_peer(host, L"Animated tab strip") == strip_peer,
            "Insertion retains its native strip peer");
        require(IsWindow(new_peer) && named_peer(host, L"New tab") == new_peer,
            "Insertion retains the native New tab button");
        require(peer_count(host) == native_peers, "Animated tabs must not create per-tab HWNDs");
        float edge = tabs->content_bounds().x;
        for (std::size_t i = 0; i < tabs->tabs().size(); ++i) {
            const auto rect = tabs->tab_bounds(i);
            if (crossing) {
                for (std::size_t j = i + 1; j < tabs->tabs().size(); ++j) {
                    const auto other = tabs->tab_bounds(j);
                    require(rect.width == 0 || other.width == 0 ||
                        rect.x + rect.width <= other.x || other.x + other.width <= rect.x,
                        "Crossing native tabs have disjoint presented rectangles");
                }
            } else if (gaps) require(rect.x >= edge - 0.02f, "Surviving native tab rectangles do not overlap");
            else expect_near(rect.x, edge);
            edge = crossing ? (std::max)(edge, rect.x + rect.width) : rect.x + rect.width;
            if (rect.width > 0 && (!crossing || rect.width > 0.01f)) {
                require(tabs->hit_test(Point{rect.x + rect.width / 2, rect.y + rect.height / 2}) == i,
                    "Hit testing follows the presented tab rectangle");
                const auto close = tabs->close_bounds(i);
                if (close.width > 0)
                    require(tabs->hit_test(close.x + close.width / 2) == i,
                        "Close target remains inside its stable tab ID");
            }
        }
        if (gaps || crossing) require(tabs->new_tab_button_bounds().x >= edge - 0.02f, "New tab stays outside surviving tabs");
        else expect_near(tabs->new_tab_button_bounds().x, edge);
        RECT native{};
        require(GetWindowRect(new_peer, &native) != FALSE, "Read retained New tab peer bounds");
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&native), 2);
        const auto model = tabs->new_tab_button()->bounds();
        const float scale = GetDpiForWindow(host) / 96.0f;
        expect_near(static_cast<float>(native.left), model.x * scale, 1.1f);
        expect_near(static_cast<float>(native.right), (model.x + model.width) * scale, 1.1f);
        expect_near(static_cast<float>(native.top), model.y * scale, 1.1f);
        expect_near(static_cast<float>(native.bottom), (model.y + model.height) * scale, 1.1f);
    }
    void begin() {
        locate();
        original_width = tabs->tab_bounds(0).width;
        insert();
        require(tabs->selected() == 33 && selections == 0, "Logical selection precedes entry without a selection callback");
        require(tabs->tab_bounds(0).width > 0, "The insertion fixture must fit all tabs without overflow scrolling");
        flush();
        require((SendMessageW(host, metrics, 33, 0) != 0) == motion,
            "The shared timer respects the read-only system motion preference");
        require(tabs->animating() == motion, "Reduced motion settles insertion immediately");
        if (motion) expect_near(tabs->tab_bounds(1).width, 0);
        entry_layouts = SendMessageW(host, metrics, 2, 0);
        started = Animation::Clock::now();
        require(SetTimer(host, driver_timer, 15, tick) != 0, "Start the bounded acceptance driver");
    }
    void step() {
        const auto now = Animation::Clock::now();
        require(now - started < std::chrono::seconds(12), "Timed tab acceptance exceeded its deadline");
        if (phase == 0) {
            geometry();
            require(tabs->selected() == 33 && selections == 0, "Entry preserves immediate logical selection");
            if (tabs->animating() && tabs->tab_bounds(1).width > 0) {
                saw_middle = true;
                require(tabs->tab_bounds(0).width < original_width, "Existing tabs reflow during insertion");
                require(SendMessageW(host, metrics, 2, 0) == entry_layouts,
                    "Intermediate timer-driven tab frames perform zero root layouts");
            }
            if (tabs->animating()) return;
            require(!motion || saw_middle, "Actual timer delivery must expose an intermediate insertion frame");
            flush();
            require(SendMessageW(host, metrics, 33, 0) == 0, "Completed tabs stop the shared timer");
            idle_layouts = SendMessageW(host, metrics, 2, 0);
            idle_paints = SendMessageW(host, metrics, 0, 0);
            idle_since = now;
            phase = 1;
        } else if (phase == 1) {
            if (now - idle_since < std::chrono::milliseconds(150)) return;
            require(SendMessageW(host, metrics, 2, 0) == idle_layouts &&
                SendMessageW(host, metrics, 0, 0) == idle_paints,
                "Settled tabs have no periodic layout or repaint");
            tabs->set_duration(2000);
            original_width = tabs->tab_bounds(0).width;
            entry_layouts = SendMessageW(host, metrics, 2, 0);
            tabs->set_tabs({{22, L"Last"}, {33, L"Incoming"}, {11, L"First"}}, 33);
            flush();
            require(tabs->animating() == motion, "Reorder shares the existing timer and reduced-motion policy");
            saw_middle = false;
            started = now;
            phase = 2;
        } else if (phase == 2) {
            geometry(false, true);
            require(tabs->tabs()[0].id == 22 && tabs->tabs()[2].id == 11 && tabs->selected() == 33,
                "Timer-driven reorder preserves immediate logical order and selection");
            if (tabs->animating() && tabs->tab_bounds(1).width > 0 && tabs->tab_bounds(1).width < original_width)
                saw_middle = true;
            require(SendMessageW(host, metrics, 2, 0) == entry_layouts,
                "Timer-driven reorder frames perform zero root layouts");
            if (tabs->animating()) return;
            require(!motion || saw_middle, "Actual timer delivery must expose an intermediate reorder frame");
            flush();
            geometry();
            require(SendMessageW(host, metrics, 33, 0) == 0, "Completed reorder stops the shared timer");
            idle_layouts = SendMessageW(host, metrics, 2, 0);
            idle_paints = SendMessageW(host, metrics, 0, 0);
            idle_since = now;
            phase = 3;
        } else {
            if (now - idle_since < std::chrono::milliseconds(150)) return;
            require(SendMessageW(host, metrics, 2, 0) == idle_layouts &&
                SendMessageW(host, metrics, 0, 0) == idle_paints,
                "Settled reorder has no periodic layout or repaint");
            lifecycle();
            done = true;
            KillTimer(host, driver_timer);
            window.close();
        }
    }
    void lifecycle() {
        reset();
        tabs->set_duration(0);
        insert(); flush();
        require(!tabs->animating() && tabs->tab_bounds(1).width > 0 &&
            SendMessageW(host, metrics, 33, 0) == 0, "Zero-duration insertion remains immediate");
        geometry();
        reset();
        tabs->set_duration(10000);
        const auto rounding_start = Animation::Clock::now();
        insert(); flush();
        if (motion) {
            const auto layouts = SendMessageW(host, metrics, 2, 0);
            tabs->advance(rounding_start + std::chrono::milliseconds(9999));
            flush();
            require(tabs->animating() && SendMessageW(host, metrics, 33, 0) != 0,
                "Rounded endpoint geometry does not finish the clock early");
            const auto rounded_width = tabs->tab_bounds(1).width;
            geometry();
            tabs->advance(rounding_start + std::chrono::milliseconds(10001));
            flush();
            require(!tabs->animating() && SendMessageW(host, metrics, 33, 0) == 0,
                "The terminal tick settles motion and stops the timer even when progress already rounded to one");
            require(tabs->tab_bounds(1).width == rounded_width,
                "The terminal tick must run even when the incoming width is already exact");
            require(SendMessageW(host, metrics, 2, 0) == layouts,
                "Rounded completion remains placement-only");
            geometry();
        }
        reset();
        insert(); flush();
        const auto prior_width = tabs->bounds().width;
        RECT outer{};
        require(GetWindowRect(host, &outer) != FALSE, "Read owned window for resize");
        require(SetWindowPos(host, nullptr, 0, 0, outer.right - outer.left + 100, outer.bottom - outer.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Resize the owned tab viewport");
        flush();
        require(tabs->bounds().width != prior_width && !tabs->animating(), "Resize settles incompatible tab geometry");
        require(SendMessageW(host, metrics, 33, 0) == 0, "Resize leaves no stale tab timer");
        geometry();
        reset(); insert(); flush();
        tabs->set_visible(false); flush();
        require(!tabs->animating() && SendMessageW(host, metrics, 33, 0) == 0,
            "Hiding the strip settles its model and stops the timer");
        tabs->set_visible(true); flush();
        geometry();
        reset(); insert(); flush();
        ShowWindow(host, SW_HIDE); flush();
        require(!tabs->animating() && SendMessageW(host, metrics, 33, 0) == 0,
            "Hiding the owner settles tab animation");
        ShowWindow(host, SW_SHOWNOACTIVATE); flush();
        reset(); insert(); flush();
        require(window.focus(*files), "Retirement preserves focus outside the strip");
        const auto previous_focus = GetFocus();
        window.replace_content(*content, {});
        flush();
        require(!tabs->animating() && SendMessageW(host, metrics, 33, 0) == 0,
            "Content retirement settles retained tabs and releases the timer");
        require(!IsWindow(strip_peer) && !IsWindow(new_peer), "Retirement destroys both native tab peers");
        require(GetFocus() == previous_focus, "Retirement retains unrelated native focus");
    }
    static void CALLBACK tick(HWND hwnd, UINT, UINT_PTR id, DWORD) noexcept {
        if (!current || current->done || current->host != hwnd || id != driver_timer) return;
        try { current->step(); }
        catch (...) {
            current->error = std::current_exception();
            current->done = true;
            KillTimer(hwnd, driver_timer);
            current->window.close();
        }
    }
};
Fixture* Fixture::current{};

void uia_geometry(Fixture& fixture, std::size_t index = 1, const wchar_t* removed = nullptr,
    std::vector<int>* runtime_id = nullptr, bool check_order = false) {
    const auto incoming = fixture.tabs->tab_bounds(index);
    const auto logical_tabs = fixture.tabs->tabs();
    RECT expected{};
    require(GetWindowRect(fixture.strip_peer, &expected) != FALSE, "Read strip screen bounds");
    const float scale = GetDpiForWindow(fixture.strip_peer) / 96.0f;
    expected.right = expected.left + static_cast<LONG>(std::lround((incoming.x + incoming.width) * scale));
    expected.left += static_cast<LONG>(std::lround(incoming.x * scale));
    const auto host = fixture.host, strip = fixture.strip_peer, native_button = fixture.new_peer;
    std::exception_ptr failure;
    std::atomic<bool> done{};
    std::thread client([&] {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            winrt::check_hresult(initialized);
            using Microsoft::WRL::ComPtr;
            ComPtr<IUIAutomation> automation;
            winrt::check_hresult(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)));
            ComPtr<IUIAutomationElement> root;
            winrt::check_hresult(automation->ElementFromHandle(strip, &root));
            const auto find = [&](const wchar_t* name, bool required = true) {
                VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(name);
                ComPtr<IUIAutomationCondition> condition;
                const auto result = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
                VariantClear(&value); winrt::check_hresult(result);
                ComPtr<IUIAutomationElement> element;
                winrt::check_hresult(root->FindFirst(TreeScope_Descendants, condition.Get(), &element));
                require(!required || element != nullptr, "UIA exposes the named tab descendant");
                return element;
            };
            if (removed) require(!find(removed, false), "Logical removal immediately removes the UIA tab item");
            auto tab = find(L"Incoming");
            ComPtr<IUIAutomationSelectionItemPattern> selection;
            winrt::check_hresult(tab->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection)));
            BOOL selected{};
            winrt::check_hresult(selection->get_CurrentIsSelected(&selected));
            require(selected != FALSE, "UIA exposes the logical selected ID during entry");
            RECT actual{};
            winrt::check_hresult(tab->get_CurrentBoundingRectangle(&actual));
            require(std::abs(actual.left - expected.left) <= 1 && std::abs(actual.right - expected.right) <= 1,
                "UIA tab edges match the presented rather than destination rectangle");
            BOOL offscreen{};
            winrt::check_hresult(tab->get_CurrentIsOffscreen(&offscreen));
            require(!offscreen && actual.bottom > actual.top, "The intermediate tab is visible to UIA");
            if (runtime_id) {
                SAFEARRAY* id{};
                winrt::check_hresult(tab->GetRuntimeId(&id));
                struct Release { SAFEARRAY* value; ~Release() { if (value) SafeArrayDestroy(value); } } release{id};
                require(id != nullptr, "The stable tab exposes a UIA runtime identity");
                LONG first{}, last{};
                winrt::check_hresult(SafeArrayGetLBound(id, 1, &first));
                winrt::check_hresult(SafeArrayGetUBound(id, 1, &last));
                runtime_id->clear();
                for (LONG i = first; i <= last; ++i) {
                    int part{};
                    winrt::check_hresult(SafeArrayGetElement(id, &i, &part));
                    runtime_id->push_back(part);
                }
            }
            if (check_order) {
                VARIANT value{}; value.vt = VT_I4; value.lVal = UIA_TabItemControlTypeId;
                ComPtr<IUIAutomationCondition> condition;
                winrt::check_hresult(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &condition));
                ComPtr<IUIAutomationElementArray> children;
                winrt::check_hresult(root->FindAll(TreeScope_Children, condition.Get(), &children));
                int count{};
                winrt::check_hresult(children->get_Length(&count));
                require(count == static_cast<int>(logical_tabs.size()), "UIA immediately exposes the logical tab count");
                for (int i = 0; i < count; ++i) {
                    ComPtr<IUIAutomationElement> child;
                    winrt::check_hresult(children->GetElement(i, &child));
                    BSTR name{};
                    winrt::check_hresult(child->get_CurrentName(&name));
                    const bool same_name = name && std::wstring_view(name) == logical_tabs[i].title;
                    SysFreeString(name);
                    const auto suffix = L"-tab-" + std::to_wstring(logical_tabs[i].id);
                    BSTR automation_id{};
                    winrt::check_hresult(child->get_CurrentAutomationId(&automation_id));
                    const bool same_id = automation_id && std::wstring_view(automation_id).ends_with(suffix);
                    SysFreeString(automation_id);
                    require(same_name && same_id, "UIA traversal follows logical ID order, not intermediate screen order");
                }
                ComPtr<IUIAutomationElement> hit;
                const POINT center{(expected.left + expected.right) / 2, (expected.top + expected.bottom) / 2};
                winrt::check_hresult(automation->ElementFromPoint(center, &hit));
                require(hit != nullptr, "UIA point lookup returns the displayed tab");
                BSTR hit_name{};
                winrt::check_hresult(hit->get_CurrentName(&hit_name));
                const bool matches = hit_name && std::wstring_view(hit_name) == L"Incoming";
                if (!matches) {
                    int process{}, type{};
                    winrt::check_hresult(hit->get_CurrentProcessId(&process));
                    winrt::check_hresult(hit->get_CurrentControlType(&type));
                    std::wcerr << L"Point hit name='" << (hit_name ? hit_name : L"") << L"' process=" << process
                        << L" fixture=" << GetCurrentProcessId() << L" type=" << type << L" point="
                        << center.x << L"," << center.y << L" expected=" << expected.left << L"," << expected.top
                        << L"," << expected.right << L"," << expected.bottom << L'\n';
                }
                SysFreeString(hit_name);
                require(matches, "UIA point lookup agrees with the visible tab instead of its logical destination");
            }
            auto button = find(L"New tab");
            ComPtr<IUIAutomationTreeWalker> walker;
            winrt::check_hresult(automation->get_RawViewWalker(&walker));
            ComPtr<IUIAutomationElement> button_host;
            winrt::check_hresult(walker->GetParentElement(button.Get(), &button_host));
            require(button_host != nullptr, "The semantic New tab button has an HWND-backed parent");
            UIA_HWND hwnd{};
            winrt::check_hresult(button_host->get_CurrentNativeWindowHandle(&hwnd));
            require(reinterpret_cast<HWND>(hwnd) == native_button, "UIA retains the original native New tab identity");
            RECT native{};
            require(GetWindowRect(native_button, &native) != FALSE, "Read New tab native bounds for UIA");
            winrt::check_hresult(button->get_CurrentBoundingRectangle(&actual));
            require(std::abs(actual.left - native.left) <= 1 && std::abs(actual.right - native.right) <= 1,
                "UIA and the moving native New tab button share one rectangle");
        } catch (...) { failure = std::current_exception(); }
        if (SUCCEEDED(initialized)) CoUninitialize();
        done = true;
        PostMessageW(host, WM_NULL, 0, 0);
    });
    while (!done) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    client.join();
    if (failure) std::rethrow_exception(failure);
}

void sampled_contract() {
    Fixture fixture{{640, 280}};
    fixture.tabs->set_duration(10000);
    TabColors colors;
    colors.row_background = 0x192A3B;
    colors.selected_background = 0x12B456;
    colors.inactive_background = 0x253E71;
    fixture.tabs->set_colors(colors);
    bool complete{};
    fixture.window.post([&] {
        fixture.locate();
        const auto original_button_x = fixture.tabs->new_tab_button_bounds().x;
        fixture.insert(); fixture.flush();
        const bool moving = fixture.tabs->animating();
        const auto sample_start = Animation::Clock::now();
        const auto layouts = SendMessageW(fixture.host, metrics, 2, 0);
        if (moving) fixture.tabs->advance(sample_start + std::chrono::milliseconds(2000));
        fixture.flush();
        fixture.geometry();
        require(fixture.tabs->new_tab_button_bounds().x > original_button_x,
            "Insertion moves the retained New tab button with the presented trailing edge");
        require(SendMessageW(fixture.host, metrics, 2, 0) == layouts,
            "A manually sampled tab frame uses placement without root layout");
        SendMessageW(fixture.strip_peer, WM_MOUSELEAVE, 0, 0);
        require(RedrawWindow(fixture.host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
            "Paint the owned intermediate tab frame");
        winrt::check_hresult(DwmFlush());
        HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE,
            "Read high contrast without changing it");
        if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            const auto pixels = owned_window_capture::capture(fixture.host);
            POINT origin{};
            MapWindowPoints(fixture.strip_peer, fixture.host, &origin, 1);
            const float scale = GetDpiForWindow(fixture.host) / 96.0f;
            const auto pixel = [&](float x, float y) {
                const auto column = origin.x + static_cast<int>(std::lround(x * scale));
                const auto row = origin.y + static_cast<int>(std::lround(y * scale));
                require(column >= 0 && column < pixels.width && row >= 0 && row < pixels.height,
                    "Tab sample remains inside the owned capture");
                return pixels.data[static_cast<std::size_t>(row) * pixels.width + column] & 0xffffff;
            };
            const auto tab = fixture.tabs->tab_bounds(1);
            require(pixel(tab.x + tab.width / 2, tab.y + tab.height - 8) == 0x12B456,
                "Compositor pixels paint the incoming tab at its presented position");
            if (moving) {
                const auto next = fixture.tabs->tab_bounds(2);
                require(pixel(next.x + 4, next.y + next.height - 8) == 0x253E71,
                    "Pixels beyond the incoming tab remain in the adjacent presented tab");
            }
        } else std::cout << "High contrast: custom-color pixel checks omitted; geometry and UIA still checked\n";
        if (moving) fixture.tabs->advance(sample_start + std::chrono::milliseconds(8000));
        fixture.flush();
        require(!moving || fixture.tabs->animating(), "UIA must inspect an intermediate insertion frame");
        uia_geometry(fixture);
        fixture.geometry();
        const auto click = [&](Rect rect) {
            require(rect.width > 0 && rect.height > 0, "Native click requires a presented target");
            const float scale = GetDpiForWindow(fixture.strip_peer) / 96.0f;
            const auto x = static_cast<short>(std::lround((rect.x + rect.width / 2) * scale));
            const auto y = static_cast<short>(std::lround((rect.y + rect.height / 2) * scale));
            SendMessageW(fixture.strip_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(fixture.strip_peer, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        };
        auto first = fixture.tabs->tab_bounds(0);
        first.width = (std::min)(first.width, 40.0f);
        click(first);
        require(fixture.tabs->selected() == 11 && fixture.selections == 1, "Native hit testing selects the presented stable ID");
        click(fixture.tabs->close_bounds(1));
        require(fixture.closes == 1 && fixture.closed_id == 33, "Native close hit testing uses the incoming tab's presented target");
        SendMessageW(fixture.new_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
        SendMessageW(fixture.new_peer, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
        require(fixture.creates == 1, "The retained native New tab action remains callable");
        fixture.tabs->settle(); fixture.flush();
        const auto survivor = fixture.tabs->tab_bounds(1);
        const auto removal_button = fixture.tabs->new_tab_button_bounds().x;
        const auto removal_layouts = SendMessageW(fixture.host, metrics, 2, 0);
        fixture.tabs->set_tabs({{33, L"Incoming"}, {22, L"Last"}}, 33);
        fixture.flush();
        if (moving) {
            expect_near(fixture.tabs->tab_bounds(0).x, survivor.x);
            expect_near(fixture.tabs->new_tab_button_bounds().x, removal_button);
            fixture.tabs->advance(Animation::Clock::now() + std::chrono::milliseconds(2000));
            fixture.flush();
            require(fixture.tabs->tab_bounds(0).x > fixture.tabs->content_bounds().x &&
                fixture.tabs->tab_bounds(0).x < survivor.x,
                "Removal exposes an intermediate surviving-tab position");
        }
        fixture.geometry(true);
        uia_geometry(fixture, 0, L"First");
        if (moving && !(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            require(RedrawWindow(fixture.host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
                "Paint the owned intermediate removal frame");
            winrt::check_hresult(DwmFlush());
            const auto pixels = owned_window_capture::capture(fixture.host);
            POINT origin{};
            MapWindowPoints(fixture.strip_peer, fixture.host, &origin, 1);
            const float scale = GetDpiForWindow(fixture.host) / 96.0f;
            const auto tab = fixture.tabs->tab_bounds(0);
            const auto sample = [&](float x) {
                const auto column = origin.x + static_cast<int>(std::lround(x * scale));
                const auto row = origin.y + static_cast<int>(std::lround((tab.y + tab.height - 8) * scale));
                require(column >= 0 && column < pixels.width && row >= 0 && row < pixels.height,
                    "Removal pixel remains inside the owned capture");
                return pixels.data[static_cast<std::size_t>(row) * pixels.width + column] & 0xffffff;
            };
            require(sample(tab.x + tab.width / 2) == 0x12B456,
                "Surviving selected-tab pixels use intermediate removal geometry");
            require(sample((fixture.tabs->content_bounds().x + tab.x) / 2) == 0x192A3B,
                "The removed tab leaves background pixels rather than a stale tab");
        }
        require(SendMessageW(fixture.host, metrics, 2, 0) == removal_layouts,
            "Removal frames and UIA inspection perform zero root layouts");
        fixture.tabs->settle(); fixture.flush();
        fixture.geometry();
        require(SendMessageW(fixture.host, metrics, 33, 0) == 0, "Completed removal stops the shared timer");
        complete = true;
        fixture.window.close();
    });
    const auto result = Application::run(fixture.window);
    if (result) std::wcerr << fixture.window.error() << L'\n';
    require(result == 0 && complete, "Sampled tab pixels, hit testing, and UIA completed");
    require(!fixture.tabs->animating(), "Window closure settles retained tab motion");
}
void reorder_contract() {
    Fixture fixture{{800, 280}};
    fixture.tabs->set_duration(10000);
    TabColors colors;
    colors.row_background = 0x192A3B;
    colors.selected_background = 0x12B456;
    colors.inactive_background = 0x253E71;
    fixture.tabs->set_colors(colors);
    bool complete{};
    fixture.window.post([&] {
        fixture.locate();
        fixture.insert(); fixture.tabs->settle(); fixture.flush();
        std::vector<int> original_identity, reordered_identity, survivor_identity;
        uia_geometry(fixture, 1, nullptr, &original_identity, true);
        require(fixture.tabs->select(11), "The fixture selects another ID before the programmatic reorder");
        fixture.flush();
        const auto first = fixture.tabs->tab_bounds(0), last = fixture.tabs->tab_bounds(2);
        const auto layouts = SendMessageW(fixture.host, metrics, 2, 0);
        const auto selections = fixture.selections;
        fixture.tabs->set_tabs({{22, L"Last"}, {33, L"Incoming"}, {11, L"First"}}, 33);
        fixture.flush();
        require(fixture.tabs->selected() == 33 && fixture.selections == selections,
            "Reorder changes logical selection without a synthetic selection callback");
        require(fixture.tabs->animating() == fixture.motion, "Reorder uses the shared system-motion policy");
        if (fixture.motion) {
            expect_near(fixture.tabs->tab_bounds(0).x, last.x);
            expect_near(fixture.tabs->tab_bounds(2).x, first.x);
            fixture.tabs->advance(Animation::Clock::now() + std::chrono::milliseconds(1000));
            fixture.flush();
            require(fixture.tabs->tab_bounds(0).x > fixture.tabs->tab_bounds(2).x &&
                fixture.tabs->tab_bounds(1).width < last.width && fixture.tabs->tab_bounds(1).width > 24,
                "The native fixture exposes a narrowed crossing before screen order reaches logical order");
        }
        fixture.geometry(false, true);
        SendMessageW(fixture.strip_peer, WM_MOUSELEAVE, 0, 0);
        require(RedrawWindow(fixture.host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
            "Paint the owned intermediate reorder frame");
        winrt::check_hresult(DwmFlush());
        HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE,
            "Read high contrast without changing it");
        if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            const auto pixels = owned_window_capture::capture(fixture.host);
            POINT origin{};
            MapWindowPoints(fixture.strip_peer, fixture.host, &origin, 1);
            const float scale = GetDpiForWindow(fixture.host) / 96.0f;
            const auto pixel = [&](float x, float y) {
                const auto column = origin.x + static_cast<int>(std::lround(x * scale));
                const auto row = origin.y + static_cast<int>(std::lround(y * scale));
                require(column >= 0 && column < pixels.width && row >= 0 && row < pixels.height,
                    "Reorder samples remain inside the owned capture");
                return pixels.data[static_cast<std::size_t>(row) * pixels.width + column] & 0xffffff;
            };
            for (std::size_t i = 0; i < fixture.tabs->tabs().size(); ++i) {
                const auto rect = fixture.tabs->tab_bounds(i);
                require(pixel(rect.x + rect.width / 2, rect.y + rect.height - 8) ==
                    (i == 1 ? 0x12B456u : 0x253E71u),
                    "Selected and inactive pixels use each stable ID's disjoint presented rectangle");
            }
            if (fixture.motion) {
                const auto content = fixture.tabs->content_bounds();
                require(pixel(content.x + 20, content.y + content.height - 8) == 0x192A3B,
                    "Vacated reorder space paints the row rather than a logical destination tab");
            }
        } else std::cout << "High contrast: reorder custom-color pixels omitted; geometry and UIA still checked\n";
        if (fixture.motion) fixture.tabs->advance(Animation::Clock::now() + std::chrono::milliseconds(8000));
        fixture.flush();
        uia_geometry(fixture, 1, nullptr, &reordered_identity, true);
        require(original_identity == reordered_identity, "A reorder retains the tab's UIA runtime identity");
        require(SendMessageW(fixture.host, metrics, 2, 0) == layouts,
            "Reorder presentation, pixels, and UIA require zero root layouts");
        fixture.geometry(false, true);
        const auto click = [&](Rect rect) {
            require(rect.width > 0 && rect.height > 0, "Native reorder actions require a displayed target");
            const float scale = GetDpiForWindow(fixture.strip_peer) / 96.0f;
            const auto x = static_cast<short>(std::lround((rect.x + rect.width / 2) * scale));
            const auto y = static_cast<short>(std::lround((rect.y + rect.height / 2) * scale));
            SendMessageW(fixture.strip_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(fixture.strip_peer, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        };
        click(fixture.tabs->close_bounds(2));
        require(fixture.closes == 1 && fixture.closed_id == 11, "Native close acts on the displayed ID after reorder");
        auto tab = fixture.tabs->tab_bounds(0);
        tab.width = (std::min)(tab.width, 40.0f);
        click(tab);
        require(fixture.tabs->selected() == 22 && fixture.selections == selections + 1,
            "Native activation selects the visible stable ID");
        fixture.flush();
        const auto retarget_layouts = SendMessageW(fixture.host, metrics, 2, 0);
        const auto snapshot = [&] {
            std::vector<std::pair<std::uint64_t, Rect>> result;
            for (std::size_t i = 0; i < fixture.tabs->tabs().size(); ++i)
                result.emplace_back(fixture.tabs->tabs()[i].id, fixture.tabs->tab_bounds(i));
            return result;
        };
        const auto preserved = [&](const auto& before) {
            if (!fixture.motion) return;
            for (std::size_t i = 0; i < fixture.tabs->tabs().size(); ++i)
                for (const auto& [id, rect] : before) if (id == fixture.tabs->tabs()[i].id) {
                    const auto current = fixture.tabs->tab_bounds(i);
                    expect_near(current.x, rect.x);
                    expect_near(current.width, rect.width);
                }
        };
        auto before = snapshot();
        fixture.tabs->set_tabs({{11, L"First"}, {33, L"Incoming"}, {22, L"Last"}}, 33);
        fixture.flush();
        preserved(before);
        before = snapshot();
        fixture.tabs->set_tabs({{11, L"First"}, {44, L"Fourth"}, {33, L"Incoming"}, {22, L"Last"}}, 33);
        fixture.flush();
        preserved(before);
        if (fixture.motion) fixture.tabs->advance(Animation::Clock::now() + std::chrono::milliseconds(1000));
        fixture.flush();
        fixture.geometry(false, true);
        before = snapshot();
        fixture.tabs->set_tabs({{44, L"Fourth"}, {33, L"Incoming"}, {22, L"Last"}}, 33);
        fixture.flush();
        preserved(before);
        if (fixture.motion) fixture.tabs->advance(Animation::Clock::now() + std::chrono::milliseconds(8000));
        fixture.flush();
        fixture.geometry(false, true);
        uia_geometry(fixture, 1, L"First", &survivor_identity, true);
        require(original_identity == survivor_identity, "Reorder, insertion, and removal preserve surviving UIA identity");
        require(SendMessageW(fixture.host, metrics, 2, 0) == retarget_layouts,
            "Reorder reversal and topology interruptions remain placement-only");
        fixture.tabs->settle(); fixture.flush();
        fixture.geometry();
        require(SendMessageW(fixture.host, metrics, 33, 0) == 0, "Settled reorder interruptions release the timer");
        complete = true;
        fixture.window.close();
    });
    const auto result = Application::run(fixture.window);
    if (result) std::wcerr << fixture.window.error() << L'\n';
    require(result == 0 && complete && !fixture.tabs->animating(), "Reorder pixels, native actions, UIA, and interruption acceptance completed");
}
void overflow_contract() {
    Fixture fixture;
    std::vector<TabItem> items;
    for (std::uint64_t id = 1; id <= 12; ++id)
        items.push_back({id, id == 12 ? L"Incoming" : L"Document " + std::to_wstring(id)});
    fixture.tabs->set_tabs(items, 1);
    fixture.tabs->set_duration(10000);
    TabColors colors;
    colors.row_background = 0x192A3B;
    colors.selected_background = 0x12B456;
    colors.inactive_background = 0x253E71;
    fixture.tabs->set_colors(colors);
    bool complete{};
    fixture.window.post([&] {
        fixture.locate();
        RECT original_button{};
        require(GetWindowRect(fixture.new_peer, &original_button) != FALSE, "Read initial overflow New button bounds");
        const auto first = fixture.tabs->tab_bounds(0);
        const auto layouts = SendMessageW(fixture.host, metrics, 2, 0);
        require(fixture.tabs->select(12), "Select a logically present offscreen tab");
        fixture.flush();
        require(fixture.tabs->animating() == fixture.motion && fixture.selections == 1 && fixture.tabs->selected() == 12,
            "Overflow selection is immediate and motion respects system policy");
        if (fixture.motion) {
            expect_near(fixture.tabs->tab_bounds(0).width, first.width);
            fixture.tabs->advance(Animation::Clock::now() + std::chrono::milliseconds(7000));
            fixture.flush();
        }
        const auto selected = fixture.tabs->tab_bounds(11);
        require(selected.width > 48, "The sampled overflow target exposes a real native close target");
        RECT current_button{};
        require(GetWindowRect(fixture.new_peer, &current_button) != FALSE &&
            EqualRect(&original_button, &current_button), "Overflow preserves the New button's native position");
        require(peer_count(fixture.host) == fixture.native_peers, "Overflow creates no native per-tab peers");
        uia_geometry(fixture, 11, nullptr, nullptr, true);
        SendMessageW(fixture.strip_peer, WM_MOUSELEAVE, 0, 0);
        require(RedrawWindow(fixture.host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
            "Paint the owned intermediate overflow frame");
        winrt::check_hresult(DwmFlush());
        HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE,
            "Read high contrast without changing it");
        if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            const auto pixels = owned_window_capture::capture(fixture.host);
            POINT origin{};
            MapWindowPoints(fixture.strip_peer, fixture.host, &origin, 1);
            const auto scale = GetDpiForWindow(fixture.host) / 96.0f;
            const auto x = origin.x + static_cast<int>(std::lround((selected.x + selected.width / 2) * scale));
            const auto y = origin.y + static_cast<int>(std::lround((selected.y + selected.height - 8) * scale));
            require(x >= 0 && x < pixels.width && y >= 0 && y < pixels.height, "Overflow sample remains in the owned window");
            require((pixels.data[static_cast<std::size_t>(y) * pixels.width + x] & 0xffffff) == 0x12B456,
                "The selected overflow pixels match the presented UIA and pointer rectangle");
        }
        fixture.tabs->select(1);
        fixture.flush();
        if (fixture.motion) {
            const auto reversed = fixture.tabs->tab_bounds(11);
            expect_near(reversed.x, selected.x);
            expect_near(reversed.width, selected.width);
            const auto close = fixture.tabs->close_bounds(11);
            const auto scale = GetDpiForWindow(fixture.strip_peer) / 96.0f;
            const auto x = static_cast<short>(std::lround((close.x + close.width / 2) * scale));
            const auto y = static_cast<short>(std::lround((close.y + close.height / 2) * scale));
            SendMessageW(fixture.strip_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(fixture.strip_peer, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            require(fixture.closes == 1 && fixture.closed_id == 12,
                "Native close during reversal targets the displayed tab, not the destination viewport");
        }
        SendMessageW(fixture.new_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
        SendMessageW(fixture.new_peer, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
        require(fixture.creates == 1, "New tab remains callable during overflow reversal");
        fixture.tabs->settle(); fixture.flush();
        expect_near(fixture.tabs->tab_bounds(0).width, first.width);
        require(!fixture.motion || SendMessageW(fixture.host, metrics, 2, 0) == layouts,
            "Opt-in overflow frames and reversal require no root layout");
        require(SendMessageW(fixture.host, metrics, 33, 0) == 0, "Settled overflow releases the shared timer");
        fixture.tabs->select(12); fixture.flush();
        complete = true;
        fixture.window.close();
    });
    const auto result = Application::run(fixture.window);
    if (result) std::wcerr << fixture.window.error() << L'\n';
    require(result == 0 && complete && !fixture.tabs->animating(), "Overflow pixels, native actions, UIA, and retirement completed");
}
}

int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment {
            ~Apartment() { winrt::clear_factory_cache(); winrt::uninit_apartment(); }
        } apartment;
        Fixture fixture;
        Fixture::current = &fixture;
        fixture.window.post([&] { fixture.begin(); });
        const auto result = Application::run(fixture.window);
        Fixture::current = nullptr;
        if (fixture.error) std::rethrow_exception(fixture.error);
        if (result) std::wcerr << fixture.window.error() << L'\n';
        require(result == 0 && fixture.done, "Timer-driven tab acceptance completed");
        if (!fixture.motion) std::cout << "Reduced motion: insertion settled immediately by system policy\n";
        sampled_contract();
        reorder_contract();
        overflow_contract();
        std::cout << "Tab window: timer, placement work, native identity, pixels, hit/UIA geometry, policy, and retirement passed\n";
        return 0;
    } catch (const std::exception& error) {
        Fixture::current = nullptr;
        std::cerr << error.what() << '\n';
        return 1;
    }
}

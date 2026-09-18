#include "collection_presentation_fixture.hpp"
#include "xui/application.hpp"
#include "owned_window_capture.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <future>
#include <thread>
#include <iostream>

namespace {
using namespace collection_fixture;
using Microsoft::WRL::ComPtr;
constexpr UINT update = WM_APP + 12, metrics = WM_APP + 60;
HWND named(HWND owner, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND found{}; } search{name};
    EnumChildWindows(owner, [](HWND child, LPARAM context) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(context);
        wchar_t text[128]{};
        GetWindowTextW(child, text, 128);
        if (std::wstring_view(text) == s.name) { s.found = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.found != nullptr, "Find the owned collection peer");
    return search.found;
}
ComPtr<IUIAutomationElement> row(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value); winrt::check_hresult(result);
    ComPtr<IUIAutomationElement> found;
    winrt::check_hresult(root->FindFirst(TreeScope_Children, condition.Get(), &found));
    require(found != nullptr, "Find a live logical collection row");
    return found;
}
std::wstring name(IUIAutomationElement* element) {
    BSTR text{};
    winrt::check_hresult(element->get_CurrentName(&text));
    std::wstring result(text ? text : L""); SysFreeString(text); return result;
}
struct Fixture {
    Window window{{L"XUI collection presentation foundation", {520, 360}, ThemeMode::light}};
    std::shared_ptr<Stack> root = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<TextInput> search = std::make_shared<TextInput>(L"Search");
    std::shared_ptr<Button> anchor = std::make_shared<Button>(L"Idle anchor");
    std::shared_ptr<Probe> list = std::make_shared<Probe>();
    std::shared_ptr<ContentHost> host = std::make_shared<ContentHost>(list);
    HWND owner{}, peer{}, editor{};
    std::thread client;
    std::exception_ptr error;
    bool complete{};
    Fixture() {
        PartStyleValues row_face, selected;
        row_face.background = ThemeColor{0x193A71};
        row_face.padding = Insets{}; row_face.border_thickness = Insets{}; row_face.corner_radius = 0.0f;
        selected.background = ThemeColor{0x12B456};
        list->set_control_style(ControlStyle::create(StyleTarget::items_view, {{StylePart::row, row_face}},
            {{StylePart::row, style_states::selected, selected}}));
        search->set_caption_visible(false);
        search->set_preferred_size({400, 40});
        anchor->set_preferred_size({400, 36});
        root->add(search); root->add(host, 1); root->add(anchor);
        window.set_content(root);
    }
    ~Fixture() { if (client.joinable()) client.join(); }
    void flush() { SendMessageW(owner, update, 0, 0); UpdateWindow(owner); }
    void ui(std::function<void()> action) {
        auto promise = std::make_shared<std::promise<void>>();
        auto result = promise->get_future();
        require(window.post([promise, action = std::move(action)] {
            try { action(); promise->set_value(); }
            catch (...) { promise->set_exception(std::current_exception()); }
        }), "Post a bounded UI-thread fixture action");
        require(result.wait_for(std::chrono::seconds(15)) == std::future_status::ready, "UI-thread fixture action timed out");
        result.get();
    }
    void editor_unchanged() {
        require(GetFocus() == editor && search->text() == L"s" && GetParent(editor) == owner,
            "Presentation updates preserve the native search editor and focus");
        DWORD first{}, last{};
        SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
        require(first == 1 && last == 1 && SendMessageW(editor, EM_CANUNDO, 0, 0),
            "Presentation updates preserve native search selection and undo");
    }
    void pixels() {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE, "Read high contrast");
        if (contrast.dwFlags & HCF_HIGHCONTRASTON) {
            std::cout << "High contrast: authored-color pixels omitted; native hit/UIA geometry remains required\n";
            return;
        }
        require(RedrawWindow(owner, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
            "Paint the actual presented collection");
        const auto image = owned_window_capture::capture(owner);
        POINT origin{}; MapWindowPoints(peer, owner, &origin, 1);
        const auto viewport = list->content_viewport();
        const float scale = GetDpiForWindow(peer) / 96.0f;
        const auto pixel = [&](float y) {
            const auto x = origin.x + static_cast<int>(std::lround((viewport.x + 200) * scale));
            const auto line = origin.y + static_cast<int>(std::lround((viewport.y + y) * scale));
            require(x >= 0 && x < image.width && line >= 0 && line < image.height, "Sample only the owned capture");
            return image.data[static_cast<std::size_t>(line) * image.width + x] & 0xffffff;
        };
        require(pixel(48) == 0x12B456, "Frozen selected outgoing content still paints in its clipped gap");
        require(pixel(68) == 0x193A71, "Outgoing pixels stop at the same edge used by the live successor");
    }
    void begin() {
        require(window.focus(*search), "Focus the actual native search editor");
        editor = GetFocus(); owner = GetAncestor(editor, GA_ROOT);
        peer = named(owner, L"Presented collection");
        SendMessageW(editor, WM_CHAR, L's', 0);
        CollectionSelection selected; selected.set({22, 7}, true); selected.set_focus(ItemKey{22, 7});
        list->set_selection(selected); flush();
        client = std::thread([this] {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            try {
                winrt::check_hresult(initialized);
                ComPtr<IUIAutomation> automation;
                winrt::check_hresult(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)));
                ComPtr<IUIAutomationElement> collection;
                winrt::check_hresult(automation->ElementFromHandle(peer, &collection));
                auto removed = row(automation.Get(), collection.Get(), L"22:7");
                auto surviving = row(automation.Get(), collection.Get(), L"33:7");
                Rect viewport{};
                LRESULT layouts{};
                ui([&] {
                    list->prepare(); list->present(20); flush();
                    viewport = list->content_viewport();
                    layouts = SendMessageW(owner, metrics, 2, 0);
                    editor_unchanged(); pixels();
                    require(!list->hit_test({viewport.x + 50, viewport.y + 50}), "Draw-only pixels never become a model hit");
                });
                BSTR unavailable{};
                const auto gone = removed->get_CurrentName(&unavailable); SysFreeString(unavailable);
                require(gone == UIA_E_ELEMENTNOTAVAILABLE, "A removed row loses UIA identity immediately while its pixels remain");
                auto current = row(automation.Get(), collection.Get(), L"33:7");
                BOOL same{};
                winrt::check_hresult(automation->CompareElements(surviving.Get(), current.Get(), &same));
                require(same, "Surviving UIA identity does not change with presentation versions");
                const auto geometry = [&](double expected_top, double expected_height = 40) {
                    RECT native{}, actual{};
                    require(GetWindowRect(peer, &native) != FALSE, "Read actual collection peer bounds");
                    winrt::check_hresult(surviving->get_CurrentBoundingRectangle(&actual));
                    const double scale = GetDpiForWindow(peer) / 96.0;
                    require(std::abs(actual.top - native.top - (viewport.y + expected_top) * scale) <= 1 &&
                        std::abs(actual.bottom - actual.top - expected_height * scale) <= 1,
                        "Native UIA uses the same explicit row geometry and clip as drawing and input");
                    ComPtr<IUIAutomationElement> hit;
                    const POINT gap{native.left + static_cast<LONG>((viewport.x + 50) * scale),
                        native.top + static_cast<LONG>((viewport.y + 50) * scale)};
                    winrt::check_hresult(automation->ElementFromPoint(gap, &hit));
                    require(name(hit.Get()) == L"Presented collection", "Native UIA point lookup excludes outgoing pixels");
                };
                geometry(60);
                ui([&] {
                    list->present(30); flush(); editor_unchanged();
                    require(SendMessageW(owner, metrics, 2, 0) == layouts &&
                        SendMessageW(owner, metrics, 33, 0) == 0, "Immutable frame updates need paint only and create no timer");
                });
                geometry(70);
                ui([&] {
                    list->present(30, 20); flush(); editor_unchanged();
                    require(list->item_bounds(1).height == 40 && !list->hit_test({viewport.x + 50, viewport.y + 100}),
                        "A clipped live row keeps full layout height but excludes clipped-away input");
                });
                geometry(70, 20);
                ui([&] {
                    const auto scale = GetDpiForWindow(peer) / 96.0f;
                    const auto point = MAKELPARAM(static_cast<WORD>((viewport.x + 50) * scale),
                        static_cast<WORD>((viewport.y + 50) * scale));
                    SendMessageW(peer, WM_LBUTTONDOWN, MK_LBUTTON, point);
                    SendMessageW(peer, WM_LBUTTONUP, 0, point);
                    require(list->selection().focused() != ItemKey{33, 7}, "Native pointer input cannot activate the live successor through an exit gap");
                    require(window.focus(*search), "Restore the unrelated native search focus");
                    list->present(20); flush();
                });
                ComPtr<IUIAutomationScrollItemPattern> scroll;
                winrt::check_hresult(surviving->GetCurrentPatternAs(UIA_ScrollItemPatternId, IID_PPV_ARGS(&scroll)));
                winrt::check_hresult(scroll->ScrollIntoView());
                ui([&] {
                    flush();
                    require(!CollectionPresentationAccess::get(*list) && list->item_bounds(1).y == viewport.y + 40,
                        "Native UIA ScrollIntoView settles presentation before using logical geometry");
                    editor_unchanged();
                    list->present(20); flush();
                    std::weak_ptr<const FrozenCollectionRow> archive = list->frozen;
                    list->frozen.reset();
                    window.replace_content(*host, {}); flush();
                    require(!IsWindow(peer) && !CollectionPresentationAccess::get(*list) && archive.expired(),
                        "Retirement destroys the peer and releases frozen rows even with retained UIA clients");
                    editor_unchanged();
                    require(window.focus(*anchor), "Keep native caret blinking out of idle measurement");
                    flush();
                });
                Sleep(100);
                const auto idle_paints = SendMessageW(owner, metrics, 0, 0);
                const auto idle_layouts = SendMessageW(owner, metrics, 2, 0);
                Sleep(180);
                require(SendMessageW(owner, metrics, 0, 0) == idle_paints &&
                    SendMessageW(owner, metrics, 2, 0) == idle_layouts &&
                    SendMessageW(owner, metrics, 33, 0) == 0, "Retired presentation has no idle paint, layout, or timer work");
                ui([&] {
                    complete = true;
                });
            } catch (...) { error = std::current_exception(); }
            if (SUCCEEDED(initialized)) CoUninitialize();
            window.post([this] { window.close(); });
        });
    }
};
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment { ~Apartment() { winrt::clear_factory_cache(); winrt::uninit_apartment(); } } apartment;
        Fixture fixture;
        fixture.window.post([&] { fixture.begin(); });
        const auto result = Application::run(fixture.window);
        if (fixture.client.joinable()) fixture.client.join();
        if (fixture.error) std::rethrow_exception(fixture.error);
        if (result) std::wcerr << fixture.window.error() << '\n';
        require(result == 0 && fixture.complete, "Native collection presentation acceptance completed");
        std::cout << "Collection presentation native pixels, logical UIA, hit gaps, search identity, scroll, retirement, and idle passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

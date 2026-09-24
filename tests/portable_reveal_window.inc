#include <UIAutomation.h>
#include <future>
#include "xui/adaptive_layout.hpp"

namespace portable_reveal_window {
using namespace xui;
using Microsoft::WRL::ComPtr;
using Clock = Reveal::Clock;
struct RevealWindow {
    Window window{{L"Portable native Reveal", {620, 460}, ThemeMode::light}};
    std::shared_ptr<Stack> root = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<Button> outside = std::make_shared<Button>(L"Outside Reveal");
    std::shared_ptr<Grid> grid = std::make_shared<Grid>();
    std::shared_ptr<Stack> content = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<TextInput> input = std::make_shared<TextInput>(L"Native animated field");
    std::shared_ptr<Button> action = std::make_shared<Button>(L"Animated action");
    std::shared_ptr<Reveal> reveal;
    std::shared_ptr<Stack> deferred_content = std::make_shared<Stack>(Axis::vertical);
    HWND hwnd{}, editor{};
    bool done{}, motion{}, exiting{}, saw_middle{}, saw_exit_pixels{};
    int phase{};
    LRESULT idle_ticks{}, idle_layouts{};
    Clock::time_point deadline{}, phase_start{};
    std::exception_ptr failure;
    std::thread automation;
    std::promise<void> inspect_exit;
    std::future<void> exit_ready = inspect_exit.get_future();
    inline static RevealWindow* current{};
    explicit RevealWindow(RevealDirection direction) {
        window.set_visual_style(VisualStyle::winui);
        content->set_fixed_size({280, 120});
        content->set_surface(true);
        PartStyleValues color; color.background = ThemeColor{0x00ff00};
        content->set_control_style_values(StylePart::root, color);
        input->set_caption_visible(false); input->set_fixed_size({200, 40}); input->set_text(L"retained reveal");
        input->set_automation_id(L"portable-reveal-editor");
        action->set_fixed_size({180, 32}); action->set_automation_id(L"portable-reveal-action");
        content->add(input); content->add(action);
        reveal = std::make_shared<Reveal>(content);
        window.apply_portable_reveal(*reveal, true, 400, direction, true);
        grid->set_fixed_size({500, 260});
        grid->set_tracks({{TrackSizing::fixed, 260}}, {{TrackSizing::fixed, 500}});
        grid->add(reveal, 0, 0);
        auto deferred_inner = std::make_shared<Reveal>(deferred_content);
        window.apply_portable_reveal(*deferred_inner, false, 0, RevealDirection::bottom, true);
        root->add(outside); root->add(grid); root->add(std::make_shared<Reveal>(deferred_inner));
        window.set_content(root);
    }
    ~RevealWindow() {
        if (automation.joinable()) automation.join();
    }
    void flush() { SendMessageW(hwnd, update, 0, 0); UpdateWindow(hwnd); }
    void fail(std::exception_ptr error) {
        failure = error;
        if (!exiting) { exiting = true; inspect_exit.set_value(); }
        KillTimer(hwnd, driver_timer); window.close();
    }
    static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
        if (!current) return;
        try { current->step(); } catch (...) { current->fail(std::current_exception()); }
    }
    void start() {
        require(window.focus(*input), "Initial settled portable Reveal editor can focus");
        editor = GetFocus(); hwnd = GetAncestor(editor, GA_ROOT);
        SendMessageW(editor, EM_SETSEL, 2, 7);
        require(reveal->progress() == 1 && !reveal->animating(), "Portable initial state has no entry animation");
        require(!window.can_set_portable_reveal_open(*reveal, false), "Focused descendants veto logical close");
        SendMessageW(editor, WM_IME_STARTCOMPOSITION, 0, 0);
        require(!window.can_set_portable_reveal_open(*reveal, false), "Composing descendants veto logical close");
        window.apply_portable_reveal(*reveal, true, 300, reveal->direction(), false);
        require(GetFocus() == editor && window.interaction(*input).is_composing,
            "Motion-only changes preserve physical focus and native composition");
        SendMessageW(editor, WM_IME_ENDCOMPOSITION, 0, 0);
        require(window.focus(*outside), "Move focus explicitly before close");
        bool rejected{};
        try { window.stack_insert(*content, 2, std::make_shared<MultilineText>()); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && content->child_count() == 2 && IsWindow(editor),
            "Direct native insertion preflights unsupported RichEdit without changing the live tree");
        rejected = false;
        try { window.stack_insert(*deferred_content, 0, std::make_shared<MultilineText>()); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && deferred_content->child_count() == 0,
            "Unmaterialized nested portable Reveals retain the same insertion preflight");
        BOOL animation{}; HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0) != FALSE &&
            SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE, "Read motion policy");
        motion = animation && !(contrast.dwFlags & HCF_HIGHCONTRASTON);
        deadline = Clock::now() + std::chrono::seconds(12);
        automation = std::thread([this] {
            try {
                require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Initialize Reveal automation worker");
                struct Com { ~Com() { CoUninitialize(); } } com;
                ComPtr<IUIAutomation> client;
                require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&client))), "Create Reveal UIA client");
                ComPtr<IUIAutomationElement> element, owner;
                require(SUCCEEDED(client->ElementFromHandle(editor, &element)) &&
                    SUCCEEDED(client->ElementFromHandle(hwnd, &owner)), "Query real native editor provider");
                ComPtr<IUIAutomationValuePattern> value;
                require(SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))),
                    "Native EDIT retains its Value pattern");
                BSTR contents{};
                require(SUCCEEDED(value->get_CurrentValue(&contents)), "Read native editor value");
                const bool matches = std::wstring_view(contents) == L"retained reveal"; SysFreeString(contents);
                require(matches, "Native provider delegates original editor text");
                require(window.post([this] {
                    phase = 1; phase_start = Clock::now();
                    window.apply_portable_reveal(*reveal, false, 400, reveal->direction(), false);
                    require(!IsWindowEnabled(editor) && !window.focus(*input),
                        "Logical close removes input immediately before exit completes");
                    exiting = true; inspect_exit.set_value();
                }), "Post real logical close");
                require(exit_ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Await bounded exit event");
                if (failure) return;
                BOOL offscreen{}, exposed{};
                require(SUCCEEDED(element->get_CurrentIsOffscreen(&offscreen)) && offscreen,
                    "Native EDIT is logically offscreen during exit even while its HWND is painted");
                require(SUCCEEDED(element->get_CurrentIsControlElement(&exposed)) && !exposed,
                    "Native EDIT leaves the control view at logical close");
                require(SUCCEEDED(element->get_CurrentIsContentElement(&exposed)) && !exposed,
                    "Native EDIT leaves the content view at logical close");
                for (const auto id : {L"portable-reveal-editor", L"portable-reveal-action"}) {
                    VARIANT automation_id{}; automation_id.vt = VT_BSTR; automation_id.bstrVal = SysAllocString(id);
                    ComPtr<IUIAutomationCondition> identity, control, condition;
                    require(SUCCEEDED(client->CreatePropertyCondition(UIA_AutomationIdPropertyId, automation_id, &identity)), "Find reveal item by identity");
                    VariantClear(&automation_id);
                    require(SUCCEEDED(client->get_ControlViewCondition(&control)) &&
                        SUCCEEDED(client->CreateAndCondition(identity.Get(), control.Get(), &condition)), "Build control-view filter");
                    ComPtr<IUIAutomationElementArray> found;
                    require(SUCCEEDED(owner->FindAll(TreeScope_Descendants, condition.Get(), &found)), "Enumerate logical exit");
                    int count{}; require(SUCCEEDED(found->get_Length(&count)) && count == 0,
                        "Both native EDIT and custom button are absent from the exit control view");
                }
                require(window.post([this] { phase = 2; }), "Post completed accessibility check");
            } catch (...) {
                const auto error = std::current_exception();
                window.post([this, error] { fail(error); });
            }
        });
        require(SetTimer(hwnd, driver_timer, 12, tick) != 0, "Start bounded Reveal observer");
    }
    void step() {
        require(Clock::now() < deadline, "Portable Reveal window fixture timed out");
        if (phase >= 1 && phase <= 2) {
            const auto progress = reveal->progress();
            const auto clip = reveal->bounds(), child = content->bounds();
            const float full = reveal->vertical() ? 120.0f : 280.0f;
            const float slot = reveal->vertical() ? 260.0f : 500.0f;
            require(std::abs((reveal->vertical() ? clip.height : clip.width) - std::min(slot, full * progress)) < 0.02f,
                "Actual forced Grid slot still clips by sampled native progress");
            require(std::abs((reveal->vertical() ? child.height : child.width) - full) < 0.02f,
                "The retained child is never scaled by clip progress");
            if (motion && progress > 0 && progress < 1 && !saw_exit_pixels) {
                saw_middle = true;
                const auto image = owned_window_capture::capture(hwnd);
                const float scale = GetDpiForWindow(hwnd) / 96.0f;
                const auto region = reveal->bounds();
                unsigned green{};
                for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
                    if ((image.data[std::size_t(y) * image.width + x] & 0xffffff) != 0x00ff00) continue;
                    ++green;
                    require(x + 1 >= region.x * scale && y + 1 >= region.y * scale &&
                        x <= (region.x + region.width) * scale + 1 && y <= (region.y + region.height) * scale + 1,
                        "Exit pixels are clipped to the actual progress bounds, not the full parent slot");
                }
                require(green > 10, "Real exit pixels remain while the accessible subtree is hidden");
                saw_exit_pixels = true;
            }
            if (phase != 2 || reveal->animating()) return;
            require(!motion || (saw_middle && saw_exit_pixels), "Native timer and owned pixels prove intermediate exit");
            require(IsWindow(editor) && !IsWindowVisible(editor) && SendMessageW(hwnd, metrics, 33, 0) == 0,
                "Completed exit retains the editor and stops the active timer");
            idle_ticks = SendMessageW(hwnd, metrics, 34, 0);
            idle_layouts = SendMessageW(hwnd, metrics, 2, 0);
            phase = 3; phase_start = Clock::now();
        } else if (phase == 3) {
            if (Clock::now() - phase_start < std::chrono::milliseconds(100)) return;
            require(SendMessageW(hwnd, metrics, 34, 0) == idle_ticks && SendMessageW(hwnd, metrics, 2, 0) == idle_layouts,
                "A settled portable Reveal has no periodic native clock or layout work");
            window.apply_portable_reveal(*reveal, true, 0, reveal->direction(), false);
            require(window.focus(*input) && GetFocus() == editor, "Reopen uses the identical EDIT HWND");
            DWORD begin{}, end{}; SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&begin), reinterpret_cast<LPARAM>(&end));
            require(begin == 2 && end == 7 && input->text() == L"retained reveal", "Exit/reopen preserves native selection and text");
            require(window.focus(*outside), "Move focus before another transition");
            window.apply_portable_reveal(*reveal, false, 400, reveal->direction(), false);
            ShowWindow(hwnd, SW_HIDE); flush();
            require(!reveal->animating() && reveal->progress() == 0 && SendMessageW(hwnd, metrics, 33, 0) == 0,
                "Hiding the owner settles motion and stops its clock");
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            window.apply_portable_reveal(*reveal, true, 0, reveal->direction(), false);
            window.apply_portable_reveal(*reveal, false, 400, reveal->direction(), false);
            window.set_theme(ThemeMode::high_contrast); flush();
            require(!reveal->animating() && reveal->progress() == 0 && SendMessageW(hwnd, metrics, 33, 0) == 0,
                "High contrast settles motion and stops its timer");
            window.set_theme(ThemeMode::light);
            window.set_presentation("Segoe UI", 14, false, false);
            window.apply_portable_reveal(*reveal, true, 400, reveal->direction(), false);
            flush();
            require(!reveal->animating() && reveal->progress() == 1 && SendMessageW(hwnd, metrics, 33, 0) == 0,
                "Application reduced motion settles without a periodic timer");
            window.set_presentation("Segoe UI", 14, false, true);
            window.apply_portable_reveal(*reveal, false, 400, reveal->direction(), false);
            window.cancel_portable_reveal(*reveal); flush();
            require(!reveal->animating() && reveal->progress() == 0 && SendMessageW(hwnd, metrics, 33, 0) == 0,
                "Terminal cancellation stops the owned motion without a synthetic action");
            KillTimer(hwnd, driver_timer); done = true; window.close();
        }
    }
};
void run() {
    for (auto direction : {RevealDirection::bottom, RevealDirection::right}) {
        RevealWindow fixture(direction); RevealWindow::current = &fixture;
        fixture.window.post([&] { fixture.start(); });
        const auto result = Application::run(fixture.window);
        RevealWindow::current = nullptr;
        if (fixture.automation.joinable()) fixture.automation.join();
        if (fixture.failure) std::rethrow_exception(fixture.failure);
        if (result) std::wcerr << fixture.window.error() << L'\n';
        require(result == 0 && fixture.done, "Portable Reveal actual window proof completed");
    }
    std::cout << "Portable Reveal windows: actual clipped pixels, native EDIT/UIA, focus/IME, identity, HC and idle clock passed\n";
}
}

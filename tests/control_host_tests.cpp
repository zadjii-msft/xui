#include "../src/basic_host.hpp"
#include "../src/scene_host.hpp"
#include "../src/navigation_host.hpp"
#include "../src/composite_host.hpp"
#include "../src/workspace_accessibility.hpp"
#include "xui/application.hpp"
#include "xui/commands.hpp"
#include "xui/titlebar.hpp"
#include <iostream>
#include <type_traits>

namespace {
using namespace xui;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct DetachedServices final : ControlHostServices {
    unsigned retired{}, updates{}, paints{};
    HWND create_input_surface(HostPeer&, HWND, int) override { std::terminate(); }
    LRESULT dispatch_input(HostPeer&, HWND, UINT, WPARAM, LPARAM) noexcept override { std::terminate(); }
    bool input_enabled(const HostPeer&) const override { return true; }
    bool input_visible(const HostPeer&) const override { return true; }
    bool owner_open() const override { return true; }
    bool focus_peer(HostPeer&, bool) override { std::terminate(); }
    void reveal_peer(HostPeer&) override { std::terminate(); }
    void update_peer_state() override { ++updates; }
    void invalidate_peer_paint() override { ++paints; }
    bool wheel_animation_allowed() const override { std::terminate(); }
    void offer_row_tooltip(HostPeer&) override { std::terminate(); }
    bool file_drag_eligible(const Control&, HWND) const override { std::terminate(); }
    void deliver_file_drop(std::function<void()>) noexcept override { std::terminate(); }
    std::shared_ptr<PeerHostServices> acquire_owner_lease() override { std::terminate(); }
    void forget_peer(HostPeer&) noexcept override { ++retired; }
    void peer_failure() noexcept override { std::terminate(); }
    Drawing& control_drawing() override { std::terminate(); }
    HWND owner_window() const override { std::terminate(); }
    ControlHostPresentation control_presentation(const HostPeer&) const override { std::terminate(); }
    bool native_activation_allowed(bool) const override { std::terminate(); }
    std::weak_ptr<platform::CompositionPresenter> composition_presenter() const override { std::terminate(); }
    HostPeer* presented_peer(const Control*) const override { std::terminate(); }
    void activate_control(HostPeer&, bool) override { std::terminate(); }
    bool caption_control(const Control&) const override { std::terminate(); }
    void set_keyboard_focus_visible(bool) override { std::terminate(); }
    bool tab_gesture_allowed(const HostPeer&) const override { std::terminate(); }
    void begin_tab_gesture(NavigationHostPeer&, POINT) override { std::terminate(); }
    HWND create_scroll_surface(HostPeer&) override { std::terminate(); }
    void collect_popup_surface(const std::shared_ptr<Popup>&) override { std::terminate(); }
    void show_host_popup(const std::shared_ptr<Popup>&, Control&, Control*) override { std::terminate(); }
    void dismiss_host_popup(Popup&, PopupDismissReason) override { std::terminate(); }
    bool expand_menu_heading(HostPeer&, bool) override { std::terminate(); }
    void focus_owned_control(Control&) override { std::terminate(); }
    bool scroll_host_key(HostPeer&, WPARAM) override { std::terminate(); }
};

void owners() {
    static_assert(sizeof(BasicHostPeer) < 488 && sizeof(SceneHostPeer) < 488 &&
        sizeof(NavigationHostPeer) < 488 && sizeof(CompositeHostPeer) < 488);
    DetachedServices services;
    {
        BasicHostPeer text(services, std::make_shared<Label>(L"Label"));
        SceneHostPeer image(services, std::make_shared<Image>(L"Image"));
        NavigationHostPeer tabs(services, std::make_shared<TabStrip>(L"Tabs"));
        CompositeHostPeer content(services, std::make_shared<ContentView>(std::make_shared<Stack>(Axis::vertical)));
        const HostPeer* peers[]{&text, &image, &tabs, &content};
        for (const auto* peer : peers)
            require(!peer->window && !peer->provider && !peer->native_field() && !peer->runtime_host() &&
                !peer->swap_chain_host() && !peer->child_content_window(),
                "Detached family ownership never realizes native children or unrelated capabilities");
        require(image.image_peer() && image.image_sync_required() &&
            !text.image_peer() && !text.image_sync_required() && tabs.image_sync_required(),
            "Only image-bearing families participate in image work");
        require(basic_peer(text) == &text && !basic_peer(image) && navigation_peer(tabs) == &tabs &&
            !navigation_peer(content) && composite_peer(content) == &content,
            "Closed family identity cannot expose another family's state");
        text.cancel_input();
        tabs.cancel_input();
        content.cancel_input();
        image.cancel_input();
        require(!text.capture_active() && !tabs.capture_active() && !content.capture_active() && !image.capture_active(),
            "Detached cancellation retires all family input state without native work");
    }
    require(services.retired == 4 && !services.updates && !services.paints,
        "Every concrete owner retires once without scheduling host work");
    for (int family = 0; family != 4; ++family) {
        bool rejected{};
        try {
            if (family == 0) BasicHostPeer invalid(services, {});
            if (family == 1) SceneHostPeer invalid(services, {});
            if (family == 2) NavigationHostPeer invalid(services, {});
            if (family == 3) CompositeHostPeer invalid(services, {});
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Missing family models fail before native attachment");
    }
    std::cout << "host-storage basic=" << sizeof(BasicHostPeer) << " scene=" << sizeof(SceneHostPeer)
              << " navigation=" << sizeof(NavigationHostPeer) << " composite=" << sizeof(CompositeHostPeer)
              << " accessibility=" << sizeof(ControlSnapshot) << '\n';
}

template<class Payload> void snapshot_for(const Control& model) {
    const auto state = std::make_shared<ControlAccessibility>();
    publish_control(state, nullptr, model, reinterpret_cast<HWND>(1));
    require(std::holds_alternative<Payload>(state->snapshot.payload),
        "Accessibility publishes only the concrete family's payload");
    state->foundation_actions.emplace(1, FoundationAction{});
    disconnect_control(state, nullptr);
    require(!state->snapshot.window && std::holds_alternative<std::monostate>(state->snapshot.payload) &&
        state->foundation_actions.empty(), "Retirement clears typed state and pending actions");
}
void typed_accessibility() {
    Label label(L"Common identity only");
    RangeInput range;
    RadioGroup choices;
    choices.set_items({{1, L"First"}, {2, L"Second"}});
    TabStrip tabs(L"Tabs");
    tabs.set_tabs({{1, L"First"}}, 1);
    snapshot_for<std::monostate>(label);
    snapshot_for<ValueAccessibilitySnapshot>(range);
    snapshot_for<SelectionAccessibilitySnapshot>(choices);
    snapshot_for<SelectionAccessibilitySnapshot>(tabs);
    ControlSnapshot value;
    require(value.range().maximum == 100 && value.selection().tabs.empty() &&
        std::holds_alternative<std::monostate>(value.payload),
        "Read-only irrelevant metadata uses defaults without allocating or changing family identity");
    value.selection_data().tabs.push_back({1, L"Selected"});
    bool rejected{};
    try { value.scroll_data(); } catch (const std::logic_error&) { rejected = true; }
    require(rejected && value.selection().tabs.size() == 1,
        "Writing a different accessibility family fails explicitly without losing retained state");
}
}

int main() {
    try {
        owners();
        typed_accessibility();
        std::cout << "Concrete family ownership, capability, storage and typed accessibility contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

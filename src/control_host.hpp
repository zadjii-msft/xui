#pragma once

#include "host_peer.hpp"
#include "platform.hpp"
#include "control_accessibility.hpp"
#include "xui/documents.hpp"
#include "xui/runtime_hosts.hpp"

namespace xui {

class TitleBar;
class NavigationHostPeer;
namespace platform { class CompositionPresenter; }

struct ControlHostPresentation {
    UINT dpi{96};
    bool composition{}, keyboard_focus_visible{}, caption_active{}, progress_animating{};
    float progress_phase{};
    const TitleBar* titlebar{};
};

class ControlHostServices : public PeerHostServices {
public:
    virtual Drawing& control_drawing() = 0;
    virtual HWND owner_window() const = 0;
    virtual ControlHostPresentation control_presentation(const HostPeer&) const = 0;
    virtual bool native_activation_allowed(bool runtime) const = 0;
    virtual std::weak_ptr<platform::CompositionPresenter> composition_presenter() const = 0;
    virtual HostPeer* presented_peer(const Control*) const = 0;
    virtual void activate_control(HostPeer&, bool) = 0;
    virtual bool caption_control(const Control&) const = 0;
    virtual void set_keyboard_focus_visible(bool) = 0;
    virtual bool tab_gesture_allowed(const HostPeer&) const = 0;
    virtual void begin_tab_gesture(NavigationHostPeer&, POINT) = 0;
    virtual HWND create_scroll_surface(HostPeer&) = 0;
    virtual void collect_popup_surface(const std::shared_ptr<Popup>&) = 0;
    virtual void show_host_popup(const std::shared_ptr<Popup>&, Control&, Control*) = 0;
    virtual void dismiss_host_popup(Popup&, PopupDismissReason) = 0;
    virtual bool expand_menu_heading(HostPeer&, bool) = 0;
    virtual void focus_owned_control(Control&) = 0;
    virtual bool scroll_host_key(HostPeer&, WPARAM) = 0;
};

class ControlHostPeer : public HostPeer {
public:
    ControlHostServices& services;
    std::shared_ptr<ControlAccessibility> accessibility = std::make_shared<ControlAccessibility>();

    ControlHostPeer(ControlHostServices&, HostFamily, std::shared_ptr<Control>);
    ~ControlHostPeer() override;
    void attach(HWND, int) override;
    void sync(UINT, const Palette&) override;
    void publish_accessibility() override;
    void disconnect_accessibility() override;
    void publish_invoked() override;
    void cancel_input() override;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override;
    virtual void reset_text_layout() {}
    virtual void reconcile_input() {}
    virtual void start_repeat(bool) {}
    virtual std::optional<LRESULT> accessibility_action(LPARAM);
    virtual HRESULT foundation_action(const FoundationAction&);
};

D2D1_COLOR_F host_input_fill(const HostPeer&, const Palette&, bool enabled);
D2D1_COLOR_F host_status_fill(StatusSeverity, const Palette&);
void paint_host_text(Drawing&, const Palette&, const Control&, StylePart,
    std::wstring_view, Rect, D2D1_COLOR_F);

}

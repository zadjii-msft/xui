#pragma once

#include "control_host.hpp"

namespace xui {

class BasicHostPeer final : public ControlHostPeer {
public:
    Microsoft::WRL::ComPtr<IDWriteTextLayout> text_layout;
    bool repeating{}, repeat_cycle{};
    std::wstring typeahead;
    ULONGLONG typeahead_time{};
    std::optional<std::size_t> hovered_choice;
    std::optional<std::uint64_t> pressed_choice;

    BasicHostPeer(ControlHostServices&, std::shared_ptr<Control>);
    ~BasicHostPeer() override;
    void attach(HWND, int) override;
    void detach_owner() override;
    void reset_text_layout() override;
    void reconcile_input() override;
    void cancel_input() override;
    bool capture_active() const override;
    void paint(const HostPaintContext&) override;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override;
    std::optional<LRESULT> accessibility_action(LPARAM) override;
    HRESULT foundation_action(const FoundationAction&) override;
    void open_combo();
    void start_repeat(bool) override;
    void stop_repeat();
};

BasicHostPeer* basic_peer(HostPeer&);
const BasicHostPeer* basic_peer(const HostPeer&);

}

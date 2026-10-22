#pragma once

#include "native_field_host.hpp"
#include "xui/native_edit.hpp"

namespace xui {

class TextInputHostPeer final : public NativeFieldHostPeer {
public:
    TextInputHostPeer(PeerHostServices&, NativeFieldServices&, std::shared_ptr<TextInput>);
    ~TextInputHostPeer() override;
    void attach(HWND parent, int id) override;
    void sync(UINT dpi, const Palette&) override;
    void paint(const HostPaintContext&) override;
    void arrange_native(Rect, UINT dpi, bool shown, const Palette&) override;
    void theme_native(UINT dpi, const Palette&, HFONT caption_font) override;
    bool composing() const override { return edit_.composing(); }
    void native_change() override;
    bool pretranslate_key(WPARAM key) override { return edit_.suggestion_key(key); }
    void cancel_input() override;
    void detach_owner() override { edit_.dismiss_suggestions(); }
    TextInputHostPeer* text_input() const override { return const_cast<TextInputHostPeer*>(this); }
    HWND caption_window() const override { return caption_; }
    NativeFieldColors native_colors(bool caption, const Palette&) const override;
    bool can_clear() const;
    void synchronize_clear();
    void collect_clear();
    void clear_text();
    const std::shared_ptr<Button>& clear_button() const { return clear_button_; }
    HWND clear_window() const { return clear_window_; }
    void set_clear_window(HWND value) { clear_window_ = value; }
    void dismiss_suggestions() { edit_.dismiss_suggestions(); }
private:
    TextInput& input() const { return static_cast<TextInput&>(*control); }
    D2D1_COLOR_F ink(const Palette&, const NativeFieldEmbedding&) const;
    D2D1_COLOR_F fill(const Palette&, const NativeFieldEmbedding&) const;
    IRawElementProviderSimple* caption_provider() const override { return caption_provider_; }
    void disconnect_caption() override;
    NativeEditBridge edit_;
    HWND caption_{};
    std::wstring caption_text_;
    std::shared_ptr<NativeFieldAccessibility> caption_accessibility_;
    IRawElementProviderSimple* caption_provider_{};
    std::shared_ptr<Button> clear_button_;
    HWND clear_window_{};
};

class TextClearHostPeer final : public HostPeer {
public:
    TextClearHostPeer(PeerHostServices&, std::shared_ptr<Button>, std::weak_ptr<TextInput>);
    ~TextClearHostPeer() override;
    void attach(HWND parent, int id) override;
    void sync(UINT, const Palette&) override { publish_accessibility(); }
    void publish_accessibility() override;
    void disconnect_accessibility() override;
    void publish_invoked() override { raise_text_clear_invoked(provider); }
    void cancel_input() override { control->cancel(); }
    bool capture_active() const override { return control->pressed(); }
    void paint(const HostPaintContext&) override;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override;
    std::shared_ptr<TextInput> owner() const { return owner_.lock(); }
private:
    void activated(bool invoked);
    std::weak_ptr<TextInput> owner_;
    std::shared_ptr<NativeFieldAccessibility> accessibility_ = std::make_shared<NativeFieldAccessibility>();
};

}

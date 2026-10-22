#pragma once

#include "host_peer.hpp"
#include "native_field_accessibility.hpp"
#include <commctrl.h>

namespace xui {

class NativeFieldHostPeer;
class TextInputHostPeer;

struct NativeFieldEmbedding {
    bool composite{}, numeric{}, combo{}, invalid{}, reserved_end{}, styled{};
    bool enabled{}, focused{}, hovered{}, surface{};
    const PartStyleValues* field_style{};
};
struct NativeFieldColors {
    D2D1_COLOR_F ink, fill;
    bool styled{}, surface{};
};

class NativeFieldServices {
public:
    virtual ~NativeFieldServices() = default;
    virtual const wchar_t* native_font_family() const = 0;
    virtual HFONT native_caption_font() const = 0;
    virtual std::shared_ptr<NativeFieldServices> acquire_native_lease() = 0;
    virtual NativeFieldEmbedding native_embedding(const NativeFieldHostPeer&) const = 0;
    virtual LRESULT dispatch_native_field(NativeFieldHostPeer&, HWND, UINT, WPARAM, LPARAM) noexcept = 0;
    virtual void collect_text_clear(TextInputHostPeer&) = 0;
    virtual void activate_text_clear(std::uint64_t owner_id) = 0;
    virtual void invalidate_native_field(Invalidation) = 0;
};

class NativeFieldHostPeer : public HostPeer {
public:
    NativeFieldHostPeer(PeerHostServices&, NativeFieldServices&, std::shared_ptr<Control>);
    void publish_accessibility() override;
    void disconnect_accessibility() override;
    void paint(const HostPaintContext&) override = 0;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override { return {}; }
    NativeFieldHostPeer* native_field() const override { return const_cast<NativeFieldHostPeer*>(this); }
    virtual TextInputHostPeer* text_input() const { return nullptr; }
    virtual bool composing() const = 0;
    virtual void arrange_native(Rect, UINT dpi, bool shown, const Palette&) = 0;
    virtual void theme_native(UINT dpi, const Palette&, HFONT caption_font);
    virtual void native_change() {}
    virtual LRESULT native_notify(const NMHDR&) { return 0; }
    virtual bool pretranslate_key(WPARAM) { return false; }
    virtual NativeFieldColors native_colors(bool caption, const Palette&) const;
    LRESULT native_message(HWND, UINT, WPARAM, LPARAM, UINT_PTR, bool composing_native);
    bool native_occluded{};
protected:
    void attach_native_override();
    void attach_caption_override(HWND);
    virtual IRawElementProviderSimple* caption_provider() const { return nullptr; }
    virtual void disconnect_caption() {}
    void arrange_document(Rect, UINT dpi, bool shown, const Palette&, bool inset);
    void paint_document_frame(const HostPaintContext&, bool inset);
    NativeFieldServices& fields;
    std::shared_ptr<NativeFieldAccessibility> accessibility = std::make_shared<NativeFieldAccessibility>();
private:
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
};

}

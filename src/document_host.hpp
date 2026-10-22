#pragma once

#include "native_field_host.hpp"
#include "native_document.hpp"

namespace xui {

class DocumentTextHostPeer final : public NativeFieldHostPeer {
public:
    DocumentTextHostPeer(PeerHostServices&, NativeFieldServices&, std::shared_ptr<DocumentText>);
    ~DocumentTextHostPeer() override;
    void attach(HWND parent, int id) override;
    void sync(UINT dpi, const Palette&) override;
    void paint(const HostPaintContext&) override;
    void arrange_native(Rect, UINT dpi, bool shown, const Palette&) override;
    bool composing() const override { return document_.composing(); }
    void native_change() override { document_.changed(); }
    LRESULT native_notify(const NMHDR& notification) override { return document_.notify(notification); }
private:
    NativeDocumentBridge document_;
};

}

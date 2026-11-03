#include "document_host.hpp"

namespace xui {

DocumentTextHostPeer::DocumentTextHostPeer(PeerHostServices& host, NativeFieldServices& fields,
    std::shared_ptr<DocumentText> document) : NativeFieldHostPeer(host, fields, document), document_(std::move(document)) {
    document_.on_failure([&host] { host.peer_failure(); });
}
DocumentTextHostPeer::~DocumentTextHostPeer() {
    retire_native();
    disconnect_accessibility();
}
void DocumentTextHostPeer::attach(HWND parent, int id) {
    document_.attach(parent, id);
    window = document_.window();
    attach_native_override();
}
void DocumentTextHostPeer::sync(UINT dpi, const Palette& palette) {
    document_.update(dpi, palette);
    publish_accessibility();
}
void DocumentTextHostPeer::paint(const HostPaintContext& context) {
    paint_document_frame(context, true);
}
void DocumentTextHostPeer::arrange_native(Rect bounds, UINT dpi, bool shown, const Palette& palette) {
    arrange_document(bounds, dpi, shown, palette, true);
}

}

#include "date_time_host.hpp"

namespace xui {

DateTimeHostPeer::DateTimeHostPeer(PeerHostServices& host, NativeFieldServices& fields,
    std::shared_ptr<DateTimePicker> date_time) : NativeFieldHostPeer(host, fields, date_time), date_time_(std::move(date_time)) {
    date_time_.on_failure([&host] { host.peer_failure(); });
}
DateTimeHostPeer::~DateTimeHostPeer() {
    retire_native();
    disconnect_accessibility();
}
void DateTimeHostPeer::attach(HWND parent, int id) {
    date_time_.attach(parent, id);
    window = date_time_.window();
    attach_native_override();
}
void DateTimeHostPeer::sync(UINT dpi, const Palette& palette) {
    date_time_.update(dpi, palette);
    publish_accessibility();
}
void DateTimeHostPeer::paint(const HostPaintContext& context) {
    paint_document_frame(context, false);
}
void DateTimeHostPeer::arrange_native(Rect bounds, UINT dpi, bool shown, const Palette& palette) {
    arrange_document(bounds, dpi, shown, palette, false);
}

}

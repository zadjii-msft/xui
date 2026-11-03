#pragma once

#include "native_field_host.hpp"
#include "native_date_time.hpp"

namespace xui {

class DateTimeHostPeer final : public NativeFieldHostPeer {
public:
    DateTimeHostPeer(PeerHostServices&, NativeFieldServices&, std::shared_ptr<DateTimePicker>);
    ~DateTimeHostPeer() override;
    void attach(HWND parent, int id) override;
    void sync(UINT dpi, const Palette&) override;
    void paint(const HostPaintContext&) override;
    void arrange_native(Rect, UINT dpi, bool shown, const Palette&) override;
    bool composing() const override { return date_time_.composing(); }
    LRESULT native_notify(const NMHDR& notification) override { return date_time_.notify(notification); }
private:
    NativeDateTimeBridge date_time_;
};

}

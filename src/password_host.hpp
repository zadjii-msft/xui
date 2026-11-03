#pragma once

#include "native_field_host.hpp"
#include "native_password.hpp"

namespace xui {

class PasswordHostPeer final : public NativeFieldHostPeer {
public:
    PasswordHostPeer(PeerHostServices&, NativeFieldServices&, std::shared_ptr<PasswordInput>);
    ~PasswordHostPeer() override;
    void attach(HWND parent, int id) override;
    void sync(UINT dpi, const Palette&) override;
    void paint(const HostPaintContext&) override;
    void arrange_native(Rect, UINT dpi, bool shown, const Palette&) override;
    bool composing() const override { return password_.composing(); }
    void native_change() override { password_.changed(); }
private:
    NativePasswordBridge password_;
};

}

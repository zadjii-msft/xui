#pragma once
#include "control_accessibility.hpp"
#include <wrl/client.h>

namespace xui {
struct VirtualRowChild {
    std::uint64_t id{};
    HWND window{};
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> provider;
    std::shared_ptr<ControlAccessibility> accessibility;
};
struct VirtualRowPresentation {
    std::uint64_t id{};
    VirtualItemInfo info;
    UiaRect bounds{};
    std::vector<VirtualRowChild> children;
};
class VirtualRowsAccessibility final : public FragmentNavigation, public std::enable_shared_from_this<VirtualRowsAccessibility> {
public:
    VirtualRowsAccessibility(HWND window, std::uint64_t identity, std::wstring name);
    bool publish(std::uint32_t count, std::vector<VirtualRowPresentation> rows);
    void close();
    std::uint64_t identity() const { return identity_; }
    IRawElementProviderSimple* provider(std::uint64_t row = 0);
    HRESULT navigate(std::uint64_t control, HWND window, NavigateDirection direction, IRawElementProviderFragment** result) override;
    HRESULT fragment_root(IRawElementProviderFragmentRoot** result) override;
private:
    friend class VirtualRowProvider;
    struct Snapshot {
        HWND window_{};
        std::uint64_t identity_{};
        std::wstring name_;
        std::uint32_t count_{};
        std::vector<VirtualRowPresentation> rows_;
        std::weak_ptr<VirtualRowsAccessibility> owner_;
        std::weak_ptr<VirtualRowsAccessibility> weak_from_this() const { return owner_; }
    };
    std::mutex mutex_;
    HWND window_{};
    std::uint64_t identity_{};
    std::wstring name_;
    std::shared_ptr<const Snapshot> snapshot_;
};
}

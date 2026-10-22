#pragma once

#include "host_accessibility.hpp"

namespace xui {

struct NativeFieldAccessibility {
    std::mutex mutex;
    HostAccessibilityIdentity snapshot;
};

IRawElementProviderSimple* create_native_field_provider(std::shared_ptr<NativeFieldAccessibility>);
IRawElementProviderSimple* create_text_clear_provider(std::shared_ptr<NativeFieldAccessibility>);
void publish_native_field(const std::shared_ptr<NativeFieldAccessibility>&, const Control&, HWND);
void publish_text_clear(const std::shared_ptr<NativeFieldAccessibility>&,
    IRawElementProviderSimple*, const Control&, HWND);
void disconnect_native_field(const std::shared_ptr<NativeFieldAccessibility>&, IRawElementProviderSimple*);
void raise_text_clear_invoked(IRawElementProviderSimple*);

}

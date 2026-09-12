#pragma once
#include "control_accessibility.hpp"

namespace xui {
IRawElementProviderSimple* create_workspace_provider(std::shared_ptr<ControlAccessibility> state);
void raise_workspace_changes(IRawElementProviderSimple* provider,
    const ControlSnapshot& before, const ControlSnapshot& after);
}

#pragma once
#include "control_accessibility.hpp"
namespace xui {
IRawElementProviderSimple* create_grid_provider(std::shared_ptr<ControlAccessibility> state);
void raise_grid_changes(IRawElementProviderSimple* provider, const ControlSnapshot& before, const ControlSnapshot& after);
}

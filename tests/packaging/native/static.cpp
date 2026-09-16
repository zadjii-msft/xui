#include <xui/controls.hpp>

int main() {
    xui::Stack stack(xui::Axis::vertical);
    stack.set_spacing(7);
    stack.add(std::make_shared<xui::Label>(L"Package consumer"));
    return stack.child_count() == 1 ? 0 : 1;
}

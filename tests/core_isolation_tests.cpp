#include "xui/core.hpp"
#include "xui/collections.hpp"
#include "xui/data_grid.hpp"

// These deliberately conflict if generic headers reintroduce legacy declarations.
namespace xui {
constexpr int FileItem = 0, FileSnapshot = 0, FilteredView = 0, ViewWorker = 0, FileListModel = 0;
}

int main() {
    xui::Stack stack(xui::Axis::vertical);
    stack.add(std::make_shared<xui::Element>());
    stack.arrange({0, 0, 100, 100});
    xui::ItemsView items;
    xui::DataGrid grid;
    return stack.child_count() == 1 && items.source() == nullptr && grid.source() == nullptr ? 0 : 1;
}

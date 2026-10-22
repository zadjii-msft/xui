#include "collections_fixture.hpp"
#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "../src/collection_accessibility.hpp"
#include "../src/grid_accessibility.hpp"
#include "../src/collection_host.hpp"
#include "../src/grid_host.hpp"
#include "../src/file_list_host.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <iostream>

namespace {
using namespace xui;
using collections_test::require;
using Microsoft::WRL::ComPtr;
void success(HRESULT value, const char* message) { require(SUCCEEDED(value), message); }
struct DetachedServices final : PeerHostServices {
    HWND create_input_surface(HostPeer&, HWND, int) override { std::terminate(); }
    LRESULT dispatch_input(HostPeer&, HWND, UINT, WPARAM, LPARAM) noexcept override { std::terminate(); }
    bool input_enabled(const HostPeer&) const override { std::terminate(); }
    bool input_visible(const HostPeer&) const override { std::terminate(); }
    bool owner_open() const override { std::terminate(); }
    bool focus_peer(HostPeer&, bool) override { std::terminate(); }
    void reveal_peer(HostPeer&) override { std::terminate(); }
    void update_peer_state() override { std::terminate(); }
    void invalidate_peer_paint() override { std::terminate(); }
    bool wheel_animation_allowed() const override { std::terminate(); }
    void offer_row_tooltip(HostPeer&) override { std::terminate(); }
    bool file_drag_eligible(const Control&, HWND) const override { std::terminate(); }
    void deliver_file_drop(std::function<void()>) noexcept override { std::terminate(); }
    std::shared_ptr<PeerHostServices> acquire_owner_lease() override { std::terminate(); }
    void forget_peer(HostPeer&) noexcept override {}
    void peer_failure() noexcept override { std::terminate(); }
};
void image_participation() {
    DetachedServices host;
    auto list = std::make_shared<FileList>(L"No image work");
    FileListHostPeer files(host, list);
    require(!files.image_sync_required(), "A plain legacy list is rejected before image visibility/geometry work");
    list->set_thumbnails(true);
    require(files.image_sync_required(), "Enabled legacy thumbnails participate in image synchronization");
    list->set_thumbnails(false);
    require(!files.image_sync_required(), "A plain list with no retained thumbnails stops participating");
    CollectionHostPeer collection(host, std::make_shared<ItemsView>(L"Collection"));
    GridHostPeer grid(host, std::make_shared<DataGrid>(L"Grid"));
    require(collection.image_sync_required() && grid.image_sync_required(),
        "Collection/grid owners participate even for empty sources, preserving cancellation");
}
class RoleControl final : public Control {
public:
    explicit RoleControl(ControlRole role) : Control(role, L"Role capability probe", {100, 24}) {}
};
void role_capabilities() {
    for (const auto role : {ControlRole::file_list, ControlRole::data_grid, ControlRole::items_view,
        ControlRole::tree_view, ControlRole::command_menu, ControlRole::text_input,
        ControlRole::document_text, ControlRole::password_input, ControlRole::date_time}) {
        Window window;
        auto host = std::make_shared<ContentHost>();
        window.set_content(host);
        bool rejected{};
        try { window.replace_content(*host, std::make_shared<RoleControl>(role)); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Family factories reject a role without its validated concrete capability");
    }
    Window window;
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(std::make_shared<RoleControl>(ControlRole::content_view));
    root->add(std::make_shared<InlineStatus>(L"Status"));
    root->add(std::make_shared<InfoBadge>(L"Badge"));
    window.set_content(root);
}
template<class State> void mailbox(const std::shared_ptr<State>& state) {
    state->snapshot.window = reinterpret_cast<HWND>(1);
    state->snapshot.id = 91;
    state->grid_actions.emplace(7, GridAction{GridAction::select, ItemKey{1, 1}});
    GridAction action;
    require(!take_row_action(state, 90, reinterpret_cast<HWND>(1), 7, action) &&
        !take_row_action(state, 91, reinterpret_cast<HWND>(2), 7, action) &&
        !take_row_action(state, 91, reinterpret_cast<HWND>(1), 8, action) &&
        state->grid_actions.size() == 1, "Mismatched control, HWND and token do not consume actions");
    require(take_row_action(state, 91, reinterpret_cast<HWND>(1), 7, action) &&
        action.key == ItemKey{1, 1} && state->grid_actions.empty(), "Exact identity consumes a value-only action once");
    require(!take_row_action(state, 91, reinterpret_cast<HWND>(1), 7, action),
        "Consumed tokens cannot be replayed");
}
void collection() {
    auto state = std::make_shared<CollectionAccessibility>();
    mailbox(state);
    state->snapshot.collection = std::make_shared<collections_test::Items>(1000000);
    ComPtr<IRawElementProviderSimple> root;
    root.Attach(create_collection_provider(state));
    ComPtr<IRawElementProviderFragment> fragment;
    success(root.As(&fragment), "Collection root fragment");
    ComPtr<IRawElementProviderFragment> child;
    success(fragment->Navigate(NavigateDirection_FirstChild, &child), "Create virtual collection child");
    require(child != nullptr, "Nonempty collection exposes a virtual child");
    ComPtr<IRawElementProviderSimple> simple;
    success(child.As(&simple), "Collection child property interface");
    VARIANT value{};
    success(simple->GetPropertyValue(UIA_NamePropertyId, &value), "Retained child queries the live source");
    require(value.vt == VT_BSTR && value.bstrVal && std::wstring_view(value.bstrVal) == L"Record 1",
        "Virtual child has its stable source identity");
    VariantClear(&value);
    state->grid_actions.emplace(8, GridAction{});
    disconnect_row_accessibility(state, root.Get());
    require(state->grid_actions.empty() && !state->snapshot.collection,
        "Collection disconnect retires its source and pending actions");
    root.Reset(); fragment.Reset(); state.reset();
    require(simple->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
        "Child retains independent disconnected state after peer/root owner release");
}
void grid() {
    auto state = std::make_shared<GridAccessibility>();
    mailbox(state);
    state->snapshot.grid = std::make_shared<collections_test::Rows>(100000);
    state->snapshot.columns = {{L"Name", 200}};
    state->snapshot.column_order = {0};
    ComPtr<IRawElementProviderSimple> root;
    root.Attach(create_grid_provider(state));
    ComPtr<IGridProvider> table;
    success(root.As(&table), "Grid provider");
    ComPtr<IRawElementProviderSimple> cell;
    success(table->GetItem(17, 0, &cell), "Create virtual grid cell");
    require(cell != nullptr, "Nonempty grid exposes a virtual cell");
    VARIANT value{};
    success(cell->GetPropertyValue(UIA_NamePropertyId, &value), "Retained grid cell queries the live source");
    require(value.vt == VT_BSTR && value.bstrVal && std::wstring_view(value.bstrVal) == L"Row 18",
        "Grid cell has its stable row/column identity");
    VariantClear(&value);
    state->grid_actions.emplace(8, GridAction{});
    disconnect_row_accessibility(state, root.Get());
    require(state->grid_actions.empty() && !state->snapshot.grid && !state->snapshot.full_source,
        "Grid disconnect retires sources and pending actions");
    root.Reset(); table.Reset(); state.reset();
    require(cell->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
        "Cell retains independent disconnected state after peer/root owner release");
}
}
int main() {
    try {
        image_participation(); role_capabilities(); collection(); grid();
        std::cout << "Typed provider identity, token replay, retained-child and disconnect contracts passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

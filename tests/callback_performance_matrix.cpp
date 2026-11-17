#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/foundation.hpp"
#include "xui/navigation.hpp"
#include "xui/commands.hpp"
#include "xui/titlebar.hpp"
#include "xui/miller_columns.hpp"
#include "xui/vector_canvas.hpp"
#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include "xui/file_list.hpp"
#include "collections_fixture.hpp"
#include <windows.h>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <fstream>
#include <map>
#include <string_view>

namespace { thread_local std::uint64_t allocations{}, allocation_bytes{}; }
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) { ++allocations; allocation_bytes += size; return value; }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
using collections_test::require;
bool measured{};
std::map<std::string,unsigned> counts;
std::uint64_t callbacks{}, checksum{};
const collections_test::Items* observed_source{};
std::uint64_t ticks(FILETIME value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
template<class F> void phase(const char* name, unsigned count, F action, std::uint64_t expected) {
    count = measured ? count : 32;
    if (measured) if (const auto found=counts.find(name); found!=counts.end()) count=found->second;
    callbacks = checksum = 0;
    const auto reads0=observed_source?observed_source->reads.load():0;
    const auto keys0=observed_source?observed_source->keys.load():0;
    const auto lookups0=observed_source?observed_source->lookups.load():0;
    FILETIME creation{}, exit{}, kernel0{}, user0{}, kernel1{}, user1{};
    require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel0, &user0) != 0, "Read owner CPU");
    const auto a = allocations, b = allocation_bytes;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned index = 0; index != count; ++index) action(index);
    const auto wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto used = allocations - a, bytes = allocation_bytes - b;
    require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel1, &user1) != 0, "Read owner CPU after work");
    const auto expected_callbacks = expected == UINT64_MAX ? 0 : std::uint64_t(count) * expected;
    require(callbacks == expected_callbacks, "Exact callback count for the fixed workload");
    std::cout << "{\"phase\":\"" << name << "\",\"iterations\":" << count
        << ",\"wall_ms\":" << (measured ? wall : 0) << ",\"measured\":" << (measured ? "true" : "false")
        << ",\"kernel_before100ns\":" << ticks(kernel0) << ",\"kernel_after100ns\":" << ticks(kernel1)
        << ",\"user_before100ns\":" << ticks(user0) << ",\"user_after100ns\":" << ticks(user1)
        << ",\"cpp_allocations\":" << used << ",\"cpp_requested_bytes\":" << bytes
        << ",\"callbacks\":" << callbacks << ",\"checksum\":" << checksum
        << ",\"source_reads\":" << (observed_source?observed_source->reads.load()-reads0:0)
        << ",\"source_keys\":" << (observed_source?observed_source->keys.load()-keys0:0)
        << ",\"source_lookups\":" << (observed_source?observed_source->lookups.load()-lookups0:0) << "}\n";
}
template<class T, class F> void setup(const char* name, F construct) {
    const auto a = allocations, b = allocation_bytes;
    { auto value = construct(); require(value != nullptr, "Construct real control"); }
    std::cout << "{\"setup\":\"" << name << "\",\"object_bytes\":" << sizeof(T)
        << ",\"cpp_allocations\":" << allocations - a << ",\"cpp_requested_bytes\":" << allocation_bytes - b << "}\n";
}
void storage() {
    setup<Label>("Label", [] { return std::make_unique<Label>(L"Label"); });
    setup<Button>("Button", [] { return std::make_unique<Button>(L"Button"); });
    setup<Toggle>("Toggle", [] { return std::make_unique<Toggle>(L"Toggle"); });
    setup<CheckBox>("CheckBox", [] { return std::make_unique<CheckBox>(L"Check"); });
    setup<TextInput>("TextInput", [] { return std::make_unique<TextInput>(L"Text"); });
    setup<ComboBox>("ComboBox", [] { return std::make_unique<ComboBox>(L"Combo", true); });
    setup<NumericInput>("NumericInput", [] { return std::make_unique<NumericInput>(); });
    setup<RangeInput>("RangeInput", [] { return std::make_unique<RangeInput>(); });
    setup<RadioGroup>("RadioGroup", [] { return std::make_unique<RadioGroup>(); });
    setup<SelectorBar>("SelectorBar", [] { return std::make_unique<SelectorBar>(); });
    setup<TabStrip>("TabStrip", [] { return std::make_unique<TabStrip>(); });
    setup<NavigationView>("NavigationView", [] { return std::make_unique<NavigationView>(); });
    setup<NavigationPane>("NavigationPane", [] { return std::make_unique<NavigationPane>(); });
    setup<MillerColumns>("MillerColumns", [] { return std::make_unique<MillerColumns>(); });
    setup<Breadcrumb>("Breadcrumb", [] { return std::make_unique<Breadcrumb>(); });
    setup<CommandBar>("CommandBar", [] { return std::make_unique<CommandBar>(); });
    setup<CommandMenu>("CommandMenu", [] { return std::make_unique<CommandMenu>(); });
    setup<TitleBar>("TitleBar", [] { return std::make_unique<TitleBar>(L"Title"); });
    setup<ColorPicker>("ColorPicker", [] { return std::make_unique<ColorPicker>(); });
    setup<InlineStatus>("InlineStatus", [] { return std::make_unique<InlineStatus>(); });
    setup<VectorCanvas>("VectorCanvas", [] { return std::make_unique<VectorCanvas>(); });
    setup<MultilineText>("MultilineText", [] { return std::make_unique<MultilineText>(); });
    setup<RichText>("RichText", [] { return std::make_unique<RichText>(); });
    setup<PasswordInput>("PasswordInput", [] { return std::make_unique<PasswordInput>(); });
    setup<DateTimePicker>("DateTimePicker", [] { return std::make_unique<DateTimePicker>(); });
    setup<ItemsView>("ItemsView", [] { return std::make_unique<ItemsView>(); });
    setup<TreeView>("TreeView", [] { return std::make_unique<TreeView>(); });
    setup<DataGrid>("DataGrid", [] { return std::make_unique<DataGrid>(); });
    setup<Progress>("Progress", [] { return std::make_unique<Progress>(); });
    setup<ContentDialog>("ContentDialog", [] { return std::make_unique<ContentDialog>(L"Dialog", std::make_shared<Label>(L"Body")); });
    setup<LocationPicker>("LocationPicker", [] { return std::make_unique<LocationPicker>(); });
    setup<ViewPicker>("ViewPicker", [] { return std::make_unique<ViewPicker>(std::make_shared<ItemsView>()); });
}
void direct() {
    Toggle toggle(L"Observed toggle");
    toggle.on_change([](bool value) { ++callbacks; checksum += value ? 1 : 2; });
    phase("toggle-invoke", 8388608, [&](unsigned) { require(toggle.invoke(), "Toggle invocation"); }, 1);
    CheckBox check(L"Observed check");
    check.on_change([](CheckState value) { ++callbacks; checksum += unsigned(value) + 1; });
    phase("checkbox-invoke", 8388608, [&](unsigned) { require(check.invoke(), "Check invocation"); }, 1);
    Button focus(L"Focus");
    focus.on_focus([] { ++callbacks; });
    phase("control-focus-cycle", 4194304, [&](unsigned) { focus.set_focused(true); focus.set_focused(false); checksum += !focus.focused(); }, 1);
    Label visual(L"Visual");
    phase("label-property", 8388608, [&](unsigned i) { visual.set_tone(i % 2 ? TextTone::normal : TextTone::accent); ++checksum; }, UINT64_MAX);
    Progress progress;
    phase("progress-value", 4194304, [&](unsigned i) { progress.set_value(i % 2 ? 1 : 2); checksum += std::uint64_t(progress.value()); }, UINT64_MAX);
    TextInput text(L"Text");
    text.on_change([](const std::wstring& value) { ++callbacks; checksum += value.size(); });
    phase("text-commit", 2097152, [&](unsigned i) { text.commit_text(i % 2 ? L"Value A" : L"Value B"); }, 1);
    text.on_submit([] { ++callbacks; checksum += 3; });
    phase("text-submit", 16777216, [&](unsigned) { text.submit(); }, 1);
    RadioGroup radio;
    radio.set_items({{1,L"First"},{2,L"Second"}}, 1);
    radio.on_change([](std::uint64_t id) { ++callbacks; checksum += id; });
    phase("radio-selection", 2097152, [&](unsigned i) { require(radio.select(i % 2 ? 1 : 2), "Select choice"); }, 1);
    radio.on_accept([](std::uint64_t id) { ++callbacks; checksum += id; });
    phase("radio-accept", 8388608, [&](unsigned) { require(radio.accept(), "Accept choice"); }, 1);
    SelectorBar selector;
    selector.set_items({{1,L"First"},{2,L"Second"}}, 1);
    selector.on_change([](std::uint64_t id) { ++callbacks; checksum += id; });
    phase("selector-selection", 2097152, [&](unsigned i) { selector.select(i % 2 ? 1 : 2); }, 1);
    RangeInput range;
    range.on_change([](double value) { ++callbacks; checksum += std::uint64_t(value); });
    phase("range-change", 4194304, [&](unsigned i) { require(range.change_value(i % 2 ? 1 : 2), "Change range"); }, 1);
    ComboBox combo(L"Editable", true);
    combo.on_edit([](const std::wstring& value) { ++callbacks; checksum += value.size(); });
    phase("combo-editor-commit", 1048576, [&](unsigned i) { combo.editor()->commit_text(i % 2 ? L"First" : L"Other"); }, 1);
    combo.set_items({{1,L"First"},{2,L"Second"}}, 1);
    combo.on_change([](std::uint64_t id) { ++callbacks; checksum += id; });
    phase("combo-selection", 1048576, [&](unsigned i) { require(combo.select(i % 2 ? 1 : 2), "Change combo identity"); }, 1);
    MultilineText plain;
    plain.on_change([](const std::wstring& value) { ++callbacks; checksum += value.size(); });
    phase("multiline-commit", 524288, [&](unsigned i) { plain.commit_text(i % 2 ? L"First line" : L"Other line"); }, 1);
    RichText rich;
    rich.on_change([](const std::wstring& value) { ++callbacks; checksum += value.size(); });
    phase("richtext-commit", 524288, [&](unsigned i) { rich.commit_text(i % 2 ? L"First line" : L"Other line"); }, 1);
    PasswordInput password;
    password.on_change([] { ++callbacks; ++checksum; });
    phase("password-commit", 1048576, [&](unsigned i) { password.commit_password(i % 2 ? L"first" : L"other"); }, 1);
    DateTimePicker date;
    date.on_change([](DateTimeValue value) { ++callbacks; checksum += value.day; });
    phase("date-change", 1048576, [&](unsigned i) { date.change_value({2026,1,i%2 ? 2 : 3}); }, 1);
}
void collections() {
    const auto source = std::make_shared<collections_test::Items>(1000000);
    observed_source=source.get();
    ItemsView items;
    items.set_items(source);
    items.on_selection([] { ++callbacks; });
    phase("items-selection", 524288, [&](unsigned i) { require(items.select({i%2+1,1}), "Select virtual item"); checksum += items.selection().focused()->id; }, 1);
    items.on_activate([](ItemKey key) { ++callbacks; checksum += key.id; });
    phase("items-activate", 1048576, [&](unsigned) { items.activate_item({1,1}); }, 1);
    TreeView tree;
    const auto roots = std::make_shared<collections_test::Tree>();
    tree.set_tree(roots);
    tree.on_selection([] { ++callbacks; });
    phase("tree-selection", 262144, [&](unsigned i) { require(tree.select({i%2+1,1}), "Select tree item"); checksum += tree.selection().focused()->id; }, 1);
    DataGrid grid;
    grid.set_columns({{L"Name",200}});
    grid.set_source(std::make_shared<collections_test::Rows>(100000));
    grid.on_select([] { ++callbacks; });
    phase("grid-selection", 524288, [&](unsigned i) { require(grid.select({i%2+1,1}), "Select grid row"); checksum += grid.selected()->id; }, 1);
    MillerColumns miller;
    miller.set_columns({{L"First",source},{L"Second",source}});
    miller.on_selection([](std::size_t column,ItemKey key) { ++callbacks; checksum += column + key.id; });
    phase("miller-selection", 131072, [&](unsigned i) { require(miller.column_list(1)->select({i%2+1,1}), "Select Miller item"); }, 1);
    std::cout << "{\"source\":\"million-items\",\"reads\":" << source->reads.load()
        << ",\"keys\":" << source->keys.load() << ",\"lookups\":" << source->lookups.load() << "}\n";
    observed_source=nullptr;
}
void composition() {
    unsigned commands{};
    CommandBar bar;
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{{1,0,L"Run",[&] { ++commands; ++callbacks; ++checksum; }}}));
    const auto action = bar.command_button(1);
    phase("commandbar-invoke", 8388608, [&](unsigned) { require(action->invoke(), "Invoke current command"); }, 1);
    Breadcrumb breadcrumb;
    breadcrumb.set_segments({{{1,1},L"First"}});
    breadcrumb.on_navigate([](ItemKey key) { ++callbacks; checksum += key.id; });
    const auto segment = breadcrumb.segment_button({1,1});
    phase("breadcrumb-invoke", 8388608, [&](unsigned) { require(segment->invoke(), "Invoke current segment"); }, 1);
    TitleBar title(L"Title");
    title.on_caption([](CaptionAction value) { ++callbacks; checksum += unsigned(value)+1; });
    phase("titlebar-caption", 8388608, [&](unsigned) { require(title.minimize()->invoke(), "Invoke caption"); }, 1);
    MenuBar menu;
    CommandRecord group{1,0,L"File"}; group.kind=CommandKind::submenu;
    menu.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{group}));
    menu.set_open_handler([](CommandId id) { ++callbacks; checksum += id; });
    phase("menubar-heading", 2097152, [&](unsigned) { require(menu.command_button(1)->invoke(), "Invoke menu heading"); }, 1);
    TabStrip tabs;
    tabs.set_new_tab_button_visible(true);
    tabs.on_new_tab([] { ++callbacks; ++checksum; });
    phase("tabs-new-button", 8388608, [&](unsigned) { require(tabs.new_tab_button()->invoke(), "Invoke new tab"); }, 1);
    InlineStatus status;
    status.set_dismissible(true);
    status.on_dismiss([] { ++callbacks; ++checksum; });
    phase("status-dismiss", 1048576, [&](unsigned) { status.show(); require(status.dismiss_button()->invoke(), "Invoke dismissal"); }, 1);
    ColorPicker color;
    color.on_change([](RgbaColor value) { ++callbacks; checksum += value.red; });
    phase("color-channel", 65536, [&](unsigned i) { color.channels()[0]->change_value(i%2 ? 1 : 2); }, 1);
    auto shape1=VectorShape::rectangle(1,{0,0,10,10}),shape2=VectorShape::rectangle(2,{20,0,10,10});
    shape1.interactive=shape2.interactive=true; shape1.name=L"First";shape2.name=L"Second";
    VectorCanvas canvas;
    canvas.set_scene(std::make_shared<VectorScene>(std::vector<VectorShape>{shape1,shape2}));
    canvas.on_select([](ShapeId id) { ++callbacks; checksum+=id; });
    phase("scene-accessible-selection", 262144, [&](unsigned i) { require(canvas.accessible_items()->select({i%2+1,1}), "Select accessible shape"); }, 1);
    NavigationView navigation;
    navigation.on_expanded([](bool value) { ++callbacks; checksum += value?1:2; });
    phase("navigation-toggle", 65536, [&](unsigned) { require(navigation.toggle_button()->invoke(), "Toggle navigation"); }, 1);
    navigation.on_filter([](const std::wstring& value) { ++callbacks; checksum += value.size(); });
    phase("navigation-search", 32768, [&](unsigned i) { navigation.search()->commit_text(i%2?L"first":L"other"); }, 1);
    CommandSurface surface;
    surface.on_query([](CommandQuery query) { ++callbacks; checksum += query.text.size(); });
    phase("command-surface-search", 131072, [&](unsigned i) { surface.editor()->commit_text(i%2?L"first":L"other"); }, 1);
    LocationPicker location;
    location.navigation()->on_query([](NavigationQuery query) { ++callbacks; checksum+=query.text.size(); });
    phase("location-search", 131072, [&](unsigned i) { location.editor()->commit_text(i%2?L"first":L"other"); }, 1);
    const auto target = std::make_shared<ItemsView>();
    ViewPicker view(target);
    phase("viewpicker-size", 262144, [&](unsigned i) {
        require(view.size()->change_value(i%2 ? 64 : 48), "Change view size");
        checksum += std::uint64_t(target->item_size().height);
    }, UINT64_MAX);
    ContentDialog dialog(L"Dialog", std::make_shared<Label>(L"Body"));
    dialog.popup()->opened();
    dialog.bind_close([](DialogResult value) { ++callbacks; checksum += unsigned(value)+1; });
    phase("dialog-primary", 1048576, [&](unsigned) { require(dialog.primary()->invoke(), "Invoke dialog primary"); }, 1);
}
}
int main(int argc,char** argv) {
    if (argc==2 && std::string_view(argv[1])=="--measure") measured=true;
    else if(argc!=1) { std::cerr<<"Usage: callback_performance_matrix [--measure]\n";return 2; }
    try {
        std::cout<<std::setprecision(17);
        if (const auto path=std::getenv("XUI_PERF_COUNTS")) {
            std::ifstream input(path);
            require(bool(input), "Read fixed iteration protocol");
            std::string name; unsigned count{};
            while(input>>name>>count) {
                require(count>=32 && count%32==0 && counts.emplace(name,count).second, "Valid unique protocol count");
            }
            require(input.eof(), "Complete fixed count protocol");
        }
        storage(); direct(); collections(); composition();
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}

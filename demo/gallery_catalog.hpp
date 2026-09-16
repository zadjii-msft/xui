#pragma once
#include "xui/data_grid.hpp"
#include "xui/miller_columns.hpp"
#include "xui/navigation.hpp"
#include <algorithm>
#include <array>
#include <cwctype>

namespace gallery {
struct Entry { const wchar_t* id; const wchar_t* group; const wchar_t* title; const wchar_t* purpose; const wchar_t* code; };
inline constexpr std::array entries{
    Entry{L"forms", L"Input", L"Forms", L"Combine native text, actions, and independent choices.",
        L"auto name = std::make_shared<TextInput>(L\"Your name\");\nname->on_change(update);\nsave->on_click(save_greeting);\npermission->on_change(update);"},
    Entry{L"buttons", L"Input", L"Buttons and icons", L"Invoke an action. Icons retain their accessible names.",
        L"auto action = std::make_shared<Button>(L\"Refresh\");\naction->set_icon(ButtonIcon::refresh);\naction->on_click(refresh);\naction->set_enabled(true);"},
    Entry{L"toggles", L"Input", L"Independent choices", L"Toggle is a checkbox, not an exclusive radio group.",
        L"auto choice = std::make_shared<Toggle>(L\"Allow updates\");\nchoice->set_checked(true);\nchoice->on_change(handle_change);"},
    Entry{L"text", L"Input", L"Text input", L"Native single-line text supports Unicode and bounded input.",
        L"auto input = std::make_shared<TextInput>(L\"Project name\");\ninput->set_maximum_length(40);\ninput->set_placeholder(L\"Enter a project\");\ninput->on_submit(submit);"},
    Entry{L"suggestions", L"Input", L"Search and suggestions", L"Suggestions use a bounded provider, not a second text engine.",
        L"input->set_search_style(true);\ninput->set_suggestions(source);\ninput->on_submit(submit);"},
    Entry{L"labels", L"Layout", L"Text and typography", L"Measured labels support headings, captions, and semantic tones.",
        L"auto label = std::make_shared<Label>(L\"Measured text\");\nlabel->set_heading(true);\nlabel->set_tone(TextTone::accent);"},
    Entry{L"layout", L"Layout", L"Stacks and surfaces", L"Compose horizontal and vertical layouts with explicit spacing.",
        L"auto row = std::make_shared<Stack>(Axis::horizontal);\nrow->set_spacing(8);\nrow->set_surface(true);\nrow->add(child, 1);"},
    Entry{L"scroll", L"Layout", L"Scrollable forms", L"ScrollView clips retained content and reveals keyboard focus.",
        L"auto scroll = std::make_shared<ScrollView>(form);\nscroll->set_automation_id(L\"workspace-scroll\");\nscroll->set_preferred_size({480, 260});"},
    Entry{L"files", L"Collections", L"File list", L"Stable selection and visible rows. These fixtures perform no file operations.",
        L"auto list = std::make_shared<FileList>(L\"Sample files\");\nlist->set_items(immutable_items);\nlist->on_activate(handle_activation);"},
    Entry{L"grid", L"Collections", L"Data grid", L"100,000 synthetic rows without retained row objects.",
        L"grid->set_columns({{L\"Item\", 240}, {L\"Size\", 110, true}});\ngrid->set_source(source);\ngrid->on_sort(replace_sorted_source);\ngrid->reorder_column(0, 1);"},
    Entry{L"tabs", L"Navigation", L"Document tabs", L"Stable tab IDs support selection, addition, and close requests.",
        L"tabs->set_tabs({{1, L\"Notes\"}, {2, L\"Preview\"}}, 1);\ntabs->on_select(select_document);\ntabs->on_close(close_document);"},
    Entry{L"split", L"Navigation", L"Split panes", L"Drag the divider. Narrow layouts collapse the secondary pane.",
        L"auto split = std::make_shared<SplitView>(first, second);\nsplit->set_ratio(0.5f);\nsplit->set_secondary_visible(true);"},
    Entry{L"pages", L"Navigation", L"Content pages", L"PageView retains pages but arranges only the active page.",
        L"auto pages = std::make_shared<PageView>();\npages->add_page(first);\npages->add_page(second);\npages->select(1);"},
    Entry{L"images", L"Media", L"Image preview", L"Bounded WIC decoding. Supply a local image path to load a preview.",
        L"auto image = std::make_shared<Image>(L\"Preview\");\nimage->set_source(path, {320, 144});\nimage->reload();\nimage->unload();"},
    Entry{L"chart", L"Media", L"History chart", L"Fixed storage for 60 samples. Updates occur only on request.",
        L"auto chart = std::make_shared<HistoryChart>(L\"History\");\nchart->set_scale(100);\nchart->append(42);\nchart->append(std::nullopt);"},
    Entry{L"menus", L"Commands", L"Menus and confirmation", L"Styled native context menus and native system confirmation.",
        L"button->on_context_menu(make_menu_items);\n// Right-click or press Shift+F10.\nif (window.confirm(L\"Confirm\", L\"Continue?\")) {\n    apply();\n}"},
    Entry{L"themes", L"Appearance", L"Themes and accessibility", L"Dark, light, and explicit high-contrast modes share the same controls.",
        L"window.set_theme(ThemeMode::dark);\nwindow.set_theme(ThemeMode::light);\nwindow.set_theme(ThemeMode::high_contrast);"},
    Entry{L"radio", L"Input", L"Exclusive choices", L"Stable radio IDs, arrow navigation, and accessible selection.",
        L"auto radio = std::make_shared<RadioGroup>(L\"View mode\");\nradio->set_items({{10, L\"List\"}, {20, L\"Details\"}}, 10);\nradio->on_change(select_view);"},
    Entry{L"combo", L"Input", L"Selection picker", L"Preview in a popup. Enter commits. Escape keeps the selected ID.",
        L"auto combo = std::make_shared<ComboBox>(L\"Format\", true);\ncombo->set_items({{1, L\"Text\"}, {2, L\"Markdown\"}}, 1);\ncombo->on_change(select_format);\ncombo->on_edit(handle_native_text);"},
    Entry{L"popup", L"Layout", L"Retained popups", L"Interactive content shares the root target. Nested popups have bounded lifetimes.",
        L"auto popup = std::make_shared<Popup>(content);\npopup->set_placement(PopupPlacement::below);\nwindow.show_popup(popup, *anchor);\nwindow.dismiss_popup(*popup);"},
    Entry{L"tooltip", L"Appearance", L"Delayed help", L"Hover or focus shows help without a new focus target.",
        L"action->set_help_text(L\"Refresh the local preview.\");\naction->set_tooltip_delay(600);"},
    Entry{L"actions", L"Commands", L"Action variants", L"Repeat, toggle, dropdown, and split actions use Button semantics.",
        L"repeat->set_behavior(ButtonBehavior::repeat);\nrepeat->set_repeat_timing(400, 80);\ntoggle->set_behavior(ButtonBehavior::toggle);\nauto split = std::make_shared<SplitButton>(L\"Run\", L\"Run options\");"},
    Entry{L"number", L"Input", L"Numeric input", L"Native locale-aware input retains invalid text and finite bounds.",
        L"auto number = std::make_shared<NumericInput>(L\"Copies\");\nnumber->set_range({1, 99, 1, 10});\nnumber->set_value(3);\nnumber->on_change(update_copies);"},
    Entry{L"range", L"Input", L"Range input", L"Horizontal and vertical values support preview, commit, and capture cancellation.",
        L"auto range = std::make_shared<RangeInput>(L\"Scale\");\nrange->set_range({0, 100, 5, 20});\nrange->set_orientation(Axis::vertical);\nrange->on_change(apply_scale);"},
    Entry{L"disclosure", L"Layout", L"Collapsible content", L"Collapse repairs focus and stops input in the hidden subtree.",
        L"auto group = std::make_shared<Expander>(L\"Details\", content);\ngroup->set_expanded(false);\ngroup->on_change(handle_disclosure);"},
    Entry{L"progress", L"Appearance", L"Progress and capacity", L"Read-only values and static indeterminate state need no idle timer.",
        L"auto progress = std::make_shared<Progress>(L\"Task\");\nprogress->set_value(40);\nprogress->set_state(ProgressState::indeterminate);\ncapacity->set_capacity(48, 128, L\"GB\");"},
    Entry{L"items", L"Collections", L"Virtual items", L"100,000 synthetic items share list, tile, and grouped presentations.",
        L"auto items = std::make_shared<ItemsView>(L\"Items\");\nitems->set_items(source, full_source);\nitems->set_presentation(ItemsPresentation::tiles);\nitems->set_select_all_scope(SelectAllScope::filtered);\nitems->on_action(handle_inline_action);"},
    Entry{L"tree", L"Collections", L"Lazy tree", L"Expanded branches use stable IDs and cancelable child requests.",
        L"auto tree = std::make_shared<TreeView>(L\"Tree\");\ntree->set_tree(source);\ntree->on_request(start_child_request);\n// Deliver on the UI thread. Canceled requests return false.\ntree->complete(request, immutable_children);"},
    Entry{L"adaptive", L"Layout", L"Adaptive panels", L"Grid tracks, wrapped items, and width breakpoints retain the same controls.",
        L"auto grid = std::make_shared<Grid>();\ngrid->set_tracks({{TrackSizing::automatic}}, {{}, {}});\ngrid->add(first, 0, 0);\ngrid->add(second, 0, 1);\nauto panes = std::make_shared<AdaptiveLayout>(nav, details);"},
    Entry{L"grid-extensions", L"Collections", L"Table selection and filters", L"Header filters and checkboxes keep logical column IDs through reordering.",
        L"grid->set_columns({{L\"Item\", 280, false, true, true}});\ngrid->on_filter(start_filter_query);\ngrid->complete_filter(request, immutable_source);\ngrid->select_all();"},
    Entry{L"commands", L"Commands", L"Command surfaces", L"Shared command IDs drive nested menus, search, independent pin actions, and toolbar overflow.",
        L"auto surface = std::make_shared<CommandSurface>();\nsurface->set_commands(commands);\nwindow.show_commands(surface, *anchor);\n// Shortcut hints do not register keyboard bindings."},
    Entry{L"breadcrumb", L"Navigation", L"Breadcrumb locations", L"Stable location segments retain the current path when a picker is canceled.",
        L"auto path = std::make_shared<Breadcrumb>();\npath->set_segments({{{1, 1}, L\"Home\"}, {{2, 1}, L\"Work\"}});\npath->on_navigate(navigate);\n// Overflow uses the same segment callbacks."},
    Entry{L"navigation", L"Navigation", L"Grouped quick access", L"Virtual rich rows and shared selection compose inline and popup navigation.",
        L"auto pane = std::make_shared<NavigationPane>();\npane->set_items(source);\npane->on_query(start_request);\npane->complete(request, source);\nauto view = std::make_shared<ViewPicker>(items);"},
    Entry{L"shell", L"Commands", L"Shell command boundary", L"Optional native fallback preserves third-party menu ownership. Discovery never runs verbs.",
        L"ShellCommandSession session(provider);\nsession.discover();\n// Only an explicit user choice can invoke a verb.\nwindow.show_shell_commands(*anchor, paths);"},
    Entry{L"titlebar", L"Navigation", L"Custom window caption", L"Real caption hit testing keeps tab actions separate from drag, resize, and system commands.",
        L"WindowOptions options;\noptions.custom_titlebar = true;\nWindow window(options);\nwindow.titlebar()->tabs()->set_tabs({{1, L\"Document\"}}, 1);"},
    Entry{L"dialog", L"Documents", L"Content dialog", L"Retained modal content traps focus and preserves invalid values.",
        L"auto dialog = std::make_shared<ContentDialog>(L\"Rename\", content);\ndialog->on_validate(validate);\ndialog->on_result(handle_result);\nwindow.show_dialog(dialog, *anchor, editor.get());"},
    Entry{L"status", L"Documents", L"Inline status", L"Severity, actions, dismissal, and accessible live announcements share one message.",
        L"auto status = std::make_shared<InlineStatus>();\nstatus->set_message(L\"Saved\", StatusSeverity::success);\nstatus->set_dismissible(true);\nstatus->set_action(L\"Undo\", undo);"},
    Entry{L"multiline", L"Documents", L"Multiline text", L"Windows RichEdit owns Unicode paragraphs, selection, undo, and composition.",
        L"auto text = std::make_shared<MultilineText>(L\"Notes\");\ntext->set_maximum_length(65536);\ntext->set_read_only(false);\ntext->on_change(save_document);"},
    Entry{L"password", L"Documents", L"Password input", L"Native password semantics exclude plaintext automation values and copy actions.",
        L"auto password = std::make_shared<PasswordInput>();\npassword->set_maximum_length(256);\npassword->on_change(update_strength);\npassword->with_password(authenticate);"},
    Entry{L"rich-text", L"Documents", L"Rich text", L"Windows RichEdit shows styled runs and explicit HTTP links without embedded objects.",
        L"auto rich = std::make_shared<RichText>();\nrich->set_runs({{L\"Heading\", true}, {L\" Details\"}});\nrich->set_read_only(true);\nrich->on_link(handle_explicit_link);"},
    Entry{L"date-time", L"Documents", L"Date and time", L"Native date, time, and calendar editors use the Windows locale.",
        L"auto date = std::make_shared<DateTimePicker>(L\"Date\");\ndate->set_value({2026, 9, 13});\ndate->set_range({2020, 1, 1}, {2030, 12, 31});\ndate->on_change(handle_date);"},
    Entry{L"color", L"Documents", L"RGBA color picker", L"Unpremultiplied sRGB channels, alpha preview, and keyboard-accessible swatches.",
        L"auto color = std::make_shared<ColorPicker>();\ncolor->set_value({45, 100, 230, 180});\ncolor->on_change(apply_color);\ncolor->set_swatches({{0, 0, 0}, {255, 255, 255}});"},
    Entry{L"vector-canvas", L"Media", L"Vector canvas", L"Retained paths, transforms, clipping, and an accessible element list share the root renderer.",
        L"auto canvas = std::make_shared<VectorCanvas>();\ncanvas->set_scene(std::make_shared<const VectorScene>(shapes));\ncanvas->on_select(select_shape);"},
    Entry{L"map", L"Media", L"Offline coordinate map", L"A Mercator graticule and authored markers support pan and zoom. No street basemap or network requests.",
        L"auto map = std::make_shared<MapView>();\nmap->set_view({0, 179}, 2);\nmap->set_overlay(authored_markers);\nmap->on_select(select_marker);"},
    Entry{L"media-playback", L"Media", L"Media playback", L"Explicit local audio and video use Windows Media Foundation. Hiding unloads the native host.",
        L"auto media = std::make_shared<MediaPlayback>();\nmedia->load_local(owned_file);\nmedia->play();\nmedia->seek(1.0);"},
    Entry{L"web-content", L"Media", L"Optional web content", L"Opt-in WebView2 displays owned HTML. The default build does not load a browser.",
        L"auto web = std::make_shared<WebContent>();\nweb->set_profile_root(owned_directory);\nweb->set_html(L\"<h1>Owned HTML</h1>\");\nweb->focus_content();"},
    Entry{L"navigation-view", L"Navigation", L"Navigation view", L"Nested navigation shares selection across searchable items and pinned shortcuts.",
        L"auto nav = std::make_shared<NavigationView>(L\"Workspace\");\nnav->set_items(records);\nnav->on_select(show_page);\nnav->set_filter(L\"reports\");\nnav->set_expanded(false);"},
    Entry{L"miller-columns", L"Collections", L"Miller columns", L"Browse a synthetic folder hierarchy with immutable sources, horizontal scrolling, and independent vertical scrolling.",
        L"auto view = std::make_shared<xui::MillerColumns>(L\"Project library\");\n"
        L"view->set_column_width(200);\n"
        L"view->set_columns({{L\"Projects\", immutable_roots, {}}});\n"
        L"view->on_selection([view = view.get(), children_for](std::size_t column, xui::ItemKey key) {\n"
        L"    auto next = view->columns();\n"
        L"    next.resize(column + 1);\n"
        L"    next[column].selected = key;\n"
        L"    if (auto children = children_for(key))\n"
        L"        next.push_back({L\"Children\", children, {}});\n"
        L"    view->set_columns(std::move(next));\n"
        L"});\n"
        L"view->on_activate(show_activation);\n"
        L"// Sources expose folder arrows through hierarchy().expandable."}
};
inline std::wstring fold(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}
inline constexpr xui::ItemKey home_key{10000, 1}, appearance_key{10001, 1};
inline std::optional<std::size_t> entry_index(xui::ItemKey key) {
    if (key == home_key) return 0;
    if (key == appearance_key) return 16;
    if (key.version != 1 || !key.id || key.id > entries.size()) return {};
    return static_cast<std::size_t>(key.id - 1);
}
inline std::vector<xui::NavigationItem> navigation_items() {
    using namespace xui;
    std::vector<NavigationItem> result{
        {home_key, {}, L"Home", ButtonIcon::home, {}, {}, true, true, true, NavigationSection::header},
        {appearance_key, {}, L"Appearance", ButtonIcon::settings, {}, {}, true, true, true, NavigationSection::footer}
    };
    std::vector<std::wstring> groups;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        auto group = std::find(groups.begin(), groups.end(), entry.group);
        const auto ordinal = static_cast<std::size_t>(group - groups.begin());
        const ItemKey parent{1000 + ordinal, 1};
        if (group == groups.end()) {
            groups.emplace_back(entry.group);
            result.push_back({parent, {}, entry.group, ButtonIcon::folder, {}, {}, true, false});
        }
        result.push_back({{i + 1, 1}, parent, entry.title, ButtonIcon::library,
            std::wstring(entry.id) + L" " + entry.purpose});
    }
    return result;
}
class Numbers final : public xui::GridSource {
public:
    explicit Numbers(bool descending = false, bool even = false) : descending_(descending), even_(even) {}
    std::size_t size() const override { return even_ ? 50000 : 100000; }
    xui::RowKey key(std::size_t row) const override { return {(descending_ ? size() - row : row + 1) * (even_ ? 2 : 1), 1}; }
    std::optional<std::size_t> find(xui::RowKey key) const override {
        if (key.version != 1 || !key.id || key.id > 100000 || (even_ && key.id % 2)) return {};
        const auto ordinal = key.id / (even_ ? 2 : 1);
        return descending_ ? size() - ordinal : ordinal - 1;
    }
    std::wstring text(std::size_t row, std::size_t column) const override {
        const auto id = key(row).id;
        return column == 0 ? L"Item " + std::to_wstring(id) : std::to_wstring(id * 16) + L" bytes";
    }
private:
    bool descending_{}, even_{};
};
class FixtureItems final : public xui::ItemsSource {
public:
    explicit FixtureItems(std::size_t count = 100000, std::uint64_t first = 1, std::uint64_t stride = 1, bool groups = true) :
        count_(count), first_(first), stride_(stride), groups_(groups) {}
    std::size_t size() const override { return count_; }
    xui::ItemKey key(std::size_t index) const override { return {first_ + index * stride_, 1}; }
    std::optional<std::size_t> find(xui::ItemKey key) const override {
        if (key.version != 1 || key.id < first_ || (key.id - first_) % stride_) return {};
        const auto index = (key.id - first_) / stride_; return index < count_ ? std::optional<std::size_t>{index} : std::nullopt;
    }
    xui::ItemContent item(std::size_t index) const override {
        const auto id = key(index).id;
        return {L"Item " + std::to_wstring(id), L"Cached synthetic content", xui::ButtonIcon::theme, double(id % 101) / 100, L"Open"};
    }
    std::vector<xui::ItemGroup> groups() const override {
        if (!groups_) return {};
        return {{{9000000001, 1}, L"First group", 0, count_ / 2}, {{9000000002, 1}, L"Second group", count_ / 2, count_ - count_ / 2}};
    }
private:
    std::size_t count_;
    std::uint64_t first_, stride_;
    bool groups_;
};
class FixtureTree final : public xui::TreeSource {
public:
    std::shared_ptr<const xui::ItemsSource> roots() const override { return roots_; }
    bool has_children(xui::ItemKey key) const override { return key.id <= 4; }
private:
    std::shared_ptr<const xui::ItemsSource> roots_{std::make_shared<FixtureItems>(4, 1, 1, false)};
};
class FixtureMillerItems final : public xui::ItemsSource {
public:
    static constexpr std::size_t levels = 8;
    explicit FixtureMillerItems(std::size_t depth = 0, xui::ItemKey parent = {1, 1}) :
        depth_(depth), parent_(parent) {}
    std::size_t size() const override { return 28; }
    xui::ItemKey key(std::size_t index) const override { return {parent_.id * 32 + index + 1, 1}; }
    std::optional<std::size_t> find(xui::ItemKey value) const override {
        const auto first = key(0).id;
        if (value.version != 1 || value.id < first || value.id - first >= size()) return {};
        return static_cast<std::size_t>(value.id - first);
    }
    xui::ItemContent item(std::size_t index) const override {
        static constexpr std::array<std::array<const wchar_t*, 3>, levels> names{{
            {L"Atlas", L"Beacon", L"Cedar"},
            {L"Design", L"Engineering", L"Research"},
            {L"Milestones", L"Prototypes", L"Reports"},
            {L"2026", L"2025", L"2024"},
            {L"Quarter 1", L"Quarter 2", L"Quarter 3"},
            {L"Planning", L"Delivery", L"Review"},
            {L"Drafts", L"Approved", L"Archive"},
            {L"Summary", L"Decisions", L"Release notes"}
        }};
        const auto name = index < 3 ? std::wstring(names[depth_][index]) :
            L"Reference note " + std::to_wstring(index - 2);
        const bool branch = hierarchy(index).expandable;
        return {name, branch ? L"Synthetic folder" : L"Synthetic document",
            branch ? xui::ButtonIcon::folder : xui::ButtonIcon::library};
    }
    xui::ItemHierarchy hierarchy(std::size_t index) const override {
        xui::ItemHierarchy result;
        result.expandable = depth_ + 1 < levels && index < 3;
        return result;
    }
    std::shared_ptr<const FixtureMillerItems> children(xui::ItemKey value) const {
        const auto row = find(value);
        if (!row || !hierarchy(*row).expandable) return {};
        return std::make_shared<const FixtureMillerItems>(depth_ + 1, value);
    }
private:
    const std::size_t depth_;
    const xui::ItemKey parent_;
};
class FixtureMillerPath {
public:
    const std::vector<xui::MillerColumn>& columns() const { return columns_; }
    bool select(std::size_t column, xui::ItemKey key) {
        if (column >= columns_.size()) return false;
        const auto source = std::static_pointer_cast<const FixtureMillerItems>(columns_[column].source);
        const auto row = source->find(key);
        if (!row) return false;
        auto next = columns_;
        next.resize(column + 1);
        next[column].selected = key;
        if (auto children = source->children(key))
            next.push_back({source->item(*row).primary, std::move(children), {}});
        columns_ = std::move(next);
        return true;
    }
private:
    std::vector<xui::MillerColumn> columns_{{L"Projects", std::make_shared<const FixtureMillerItems>(), {}}};
};
}

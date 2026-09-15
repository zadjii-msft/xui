#include "browser.hpp"
#include "directory.hpp"
#include "explorer_state.hpp"
#include "xui/suggestions.hpp"
#include "shell_dispatch.hpp"
#include "xui/application.hpp"
#include "xui/navigation.hpp"
#include <array>
#include <mutex>
#include <cwctype>

namespace xui {
namespace {
using namespace explorer;
class CachedPlaces final : public ItemsSource {
public:
    CachedPlaces(const std::map<ItemKey, std::filesystem::path>& paths, std::wstring query = {}) {
        std::transform(query.begin(), query.end(), query.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        for (const auto& [key, path] : paths) {
            auto text = path.wstring();
            std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (text.find(query) != std::wstring::npos) rows_.push_back({key, path.wstring()});
        }
    }
    std::size_t size() const override { return rows_.size(); }
    ItemKey key(std::size_t i) const override { return rows_.at(i).first; }
    std::optional<std::size_t> find(ItemKey key) const override {
        for (std::size_t i = 0; i < rows_.size(); ++i) if (rows_[i].first == key) return i;
        return {};
    }
    ItemContent item(std::size_t i) const override { return {rows_.at(i).second, L"Cached path location", ButtonIcon::up}; }
    std::vector<ItemGroup> groups() const override {
        return {{{1, 0}, L"Current path", 0, rows_.size()}};
    }
private:
    std::vector<std::pair<ItemKey, std::wstring>> rows_;
};
class BrowserRoot final : public Stack {
public:
    explicit BrowserRoot(std::function<void()> arranged) : Stack(Axis::vertical), arranged_(std::move(arranged)) {}
    void arrange(Rect bounds) override {
        Stack::arrange(bounds);
        arranged_();
    }
private:
    std::function<void()> arranged_;
};
struct SourceSlot {
    std::mutex mutex;
    std::filesystem::path path;
    void set(std::filesystem::path next) {
        std::lock_guard lock(mutex);
        path = std::move(next);
    }
    std::filesystem::path get() {
        std::lock_guard lock(mutex);
        return path;
    }
};
struct Pane {
    PaneState state;
    std::shared_ptr<Stack> root = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<TabStrip> tabs = std::make_shared<TabStrip>();
    std::shared_ptr<TextInput> address = std::make_shared<TextInput>(L"Folder address");
    std::shared_ptr<TextInput> search = std::make_shared<TextInput>(L"Search files");
    std::shared_ptr<Breadcrumb> breadcrumb = std::make_shared<Breadcrumb>();
    std::map<ItemKey, std::filesystem::path> breadcrumb_paths;
    std::filesystem::path breadcrumb_path;
    std::shared_ptr<FileList> list = std::make_shared<FileList>();
    std::shared_ptr<Label> status = std::make_shared<Label>(L"Reading folder...");
    std::shared_ptr<Button> back, forward, up, refresh, new_tab;
    std::shared_ptr<SourceSlot> source = std::make_shared<SourceSlot>();
    std::shared_ptr<ViewTask> task;
    std::wstring error, notice;
    std::size_t thumbnail_failures{};
    std::uint64_t pending_serial{};
    bool syncing{}, filtering{}, loaded{};
    bool reload_source{};
    explicit Pane(const std::filesystem::path& initial) : state(initial) {}
};
class Browser {
    Window window_;
    std::array<std::unique_ptr<Pane>, 2> panes_;
    std::shared_ptr<SplitView> split_;
    std::shared_ptr<Button> split_button_;
    std::size_t active_{};
public:
    explicit Browser(const BrowserOptions& options)
        : window_({L"XUI/Files - " + options.folder.wstring(), {924, 641}, options.theme, {460, 420}, false, options.visual_style}) {
        auto root = std::make_shared<BrowserRoot>([this] {
            if (active_ && split_ && !split_->expanded()) activate(0);
        });
        root->set_padding({16, 12, 16, 12});
        split_button_ = button(L"Split panes", L"browser-split", [this] { toggle_split(); });
        split_button_->set_icon(ButtonIcon::split);
        split_button_->set_fixed_size({32, 38});
        split_button_->on_focus([this] { if (split_ && !split_->expanded()) activate(0); });
        for (std::size_t i = 0; i < panes_.size(); ++i) {
            panes_[i] = std::make_unique<Pane>(options.folder);
            build_pane(i);
        }
        split_ = std::make_shared<SplitView>(panes_[0]->root, panes_[1]->root);
        split_->set_automation_id(L"browser-divider");
        split_->set_secondary_visible(false);
        root->add(split_, 1);
        window_.set_content(root);
        window_.on_key([this](const KeyEvent& event) { return key(event); });
        window_.on_navigation([this](const NavigationEvent& event) {
            auto index = active_;
            auto point = event.position;
            if (!point && event.target) {
                const auto bounds = event.target->bounds();
                point = Point{bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
            }
            if (point) {
                for (std::size_t i = 0; i < panes_.size(); ++i) {
                    if (i && !split_->expanded()) continue;
                    const auto bounds = panes_[i]->root->bounds();
                    if (point->x >= bounds.x && point->x < bounds.x + bounds.width &&
                        point->y >= bounds.y && point->y < bounds.y + bounds.height) { index = i; break; }
                }
            }
            if (index && !split_->expanded()) index = 0;
            activate(index);
            travel(index, event.direction == NavigationDirection::back ? -1 : 1);
            return true;
        });
        load(0, panes_[0]->state.active().history.current());
    }
    int run() {
        const auto result = Application::run(window_);
        for (auto& pane : panes_) if (pane->task) pane->task->cancel();
        return result;
    }
private:
    std::shared_ptr<Button> button(std::wstring text, std::wstring id, std::function<void()> action) {
        auto result = std::make_shared<Button>(std::move(text));
        result->set_automation_id(std::move(id));
        result->on_click(std::move(action));
        return result;
    }
    void build_pane(std::size_t index) {
        auto& p = *panes_[index];
        const auto prefix = index ? L"browser-right-" : L"browser-";
        const auto id = [&](const wchar_t* value) { return std::wstring(prefix) + value; };
        p.root->set_spacing(0);
        auto tab_row = std::make_shared<Stack>(Axis::horizontal);
        tab_row->set_spacing(2);
        p.tabs->set_automation_id(id(L"tabs"));
        p.tabs->set_name(index ? L"Right pane tabs" : L"Left pane tabs");
        tab_row->add(p.tabs, 1);
        p.new_tab = button(L"+", id(L"new-tab"), [this, index] { add_tab(index); });
        p.new_tab->set_icon(ButtonIcon::add);
        p.new_tab->set_fixed_size({32, 38});
        tab_row->add(p.new_tab);
        if (!index) {
            tab_row->add(split_button_);
            auto theme_button = button(L"Theme", L"browser-theme", [this] { theme(); });
            theme_button->set_icon(ButtonIcon::theme);
            theme_button->set_fixed_size({32, 38});
            tab_row->add(theme_button);
        }
        p.root->add(tab_row);
        auto body = std::make_shared<Stack>(Axis::vertical);
        body->set_padding({8, 8, 8, 0});
        body->set_spacing(7);
        p.root->add(body, 1);
        auto navigation = std::make_shared<Stack>(Axis::horizontal);
        navigation->set_spacing(4);
        p.back = button(L"Back", id(L"back"), [this, index] { travel(index, -1); });
        p.forward = button(L"Forward", id(L"forward"), [this, index] { travel(index, 1); });
        p.up = button(L"Up", id(L"up"), [this, index] { go_up(index); });
        p.refresh = button(L"Refresh", id(L"refresh"), [this, index] { refresh(index); });
        p.back->set_icon(ButtonIcon::back);
        p.forward->set_icon(ButtonIcon::forward);
        p.up->set_icon(ButtonIcon::up);
        p.refresh->set_icon(ButtonIcon::refresh);
        for (const auto& control : {p.back, p.forward, p.up, p.refresh}) {
            control->set_fixed_size({32, 40});
            navigation->add(control);
            control->on_focus([this, index] { activate(index); });
        }
        auto path_bar = std::make_shared<Stack>(Axis::horizontal); path_bar->set_spacing(4);
        path_bar->add(p.breadcrumb, 1);
        body->add(path_bar);
        body->add(navigation);
        p.address->set_name(index ? L"Right folder address" : L"Folder address");
        p.address->set_automation_id(id(L"address"));
        p.address->set_maximum_length(32767);
        p.address->set_suggestions(folder_suggestions());
        p.address->set_caption_visible(false);
        p.address->set_preferred_size({0, 40});
        p.address->set_placeholder(L"Enter a folder path");
        p.address->set_shortcut_hint(L"Ctrl+L");
        navigation->add(p.address, 1);
        auto commands = button(L"Commands", id(L"commands"), [this, index] {
            auto items = menu(index);
            std::vector<CommandRecord> records;
            CommandId next{};
            for (auto& item : items) {
                CommandRecord record; record.id = ++next; record.enabled = item.enabled;
                record.kind = item.separator ? CommandKind::separator : CommandKind::action;
                record.label = item.text; record.action = std::move(item.action);
                if (const auto tab = record.label.find(L'\t'); tab != std::wstring::npos) {
                    record.shortcut_hints.push_back(record.label.substr(tab + 1)); record.label.resize(tab);
                }
                records.push_back(std::move(record));
            }
            auto surface = std::make_shared<CommandSurface>(L"File commands");
            surface->set_commands(std::make_shared<CommandSet>(std::move(records)));
            window_.show_commands(surface, *panes_[index]->address);
        });
        commands->set_fixed_size({84, 40}); path_bar->add(commands);
        auto locations = button(L"Locations", id(L"locations"), [this, index] {
            auto picker = std::make_shared<LocationPicker>(L"Path locations");
            const auto paths = panes_[index]->breadcrumb_paths;
            picker->navigation()->set_items(std::make_shared<CachedPlaces>(paths));
            picker->navigation()->on_query([weak = std::weak_ptr<NavigationPane>(picker->navigation()), paths](NavigationQuery request) {
                if (auto pane = weak.lock()) pane->complete(request, std::make_shared<CachedPlaces>(paths, request.text));
            });
            picker->navigation()->on_navigate([this, index, paths, weak = std::weak_ptr<Popup>(picker->popup())](ItemKey key) {
                const auto found = paths.find(key); if (found == paths.end()) return;
                const auto path = found->second;
                if (auto popup = weak.lock()) window_.dismiss_popup(*popup, PopupDismissReason::commit);
                navigate(index, path);
            });
            picker->toolbar()->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
                {1, 0, L"Back", [this, index] { travel(index, -1); }, panes_[index]->back->enabled()},
                {2, 0, L"Up", [this, index] { go_up(index); }, panes_[index]->up->enabled()}}));
            window_.show_location_picker(picker, *panes_[index]->address);
        });
        locations->set_fixed_size({80, 40}); path_bar->add(locations);
        p.breadcrumb->set_automation_id(id(L"breadcrumb")); p.breadcrumb->set_preferred_size({0, 40});
        p.breadcrumb->on_navigate([this, index](ItemKey key) {
            const auto found = panes_[index]->breadcrumb_paths.find(key);
            if (found != panes_[index]->breadcrumb_paths.end()) navigate(index, found->second);
        });
        p.breadcrumb->on_overflow([this, index] {
            const auto& breadcrumb = panes_[index]->breadcrumb;
            auto surface = std::make_shared<CommandSurface>(L"Earlier folders", false);
            surface->set_commands(breadcrumb->overflow_commands()); window_.show_commands(surface, *breadcrumb->overflow_button());
        });
        p.search->set_name(index ? L"Search right files" : L"Search files");
        p.search->set_automation_id(id(L"search"));
        p.search->set_search_style(true);
        p.search->set_preferred_size({0, 40});
        p.search->set_placeholder(L"Filter this folder");
        p.search->set_shortcut_hint(L"Ctrl+F");
        body->add(p.search);
        p.list->set_name(index ? L"Right files" : L"Files");
        p.list->set_automation_id(id(L"files"));
        auto surface = std::make_shared<Stack>(Axis::vertical);
        surface->set_surface(true); surface->set_padding({1, 1, 1, 1});
        surface->add(p.list, 1);
        body->add(surface, 1);
        p.status->set_caption(true);
        p.status->set_preferred_size({0, 26});
        p.status->set_automation_id(id(L"status"));
        p.status->on_context_menu([this, index] {
            return std::vector<MenuItem>{{L"Copy status details", [this, index] { copy(index, panes_[index]->status->text()); }}};
        });
        body->add(p.status);
        p.tabs->on_focus([this, index] { activate(index); });
        p.new_tab->on_focus([this, index] { activate(index); });
        p.address->on_focus([this, index] { activate(index); });
        p.search->on_focus([this, index] { activate(index); });
        p.list->on_focus([this, index] { activate(index); });
        p.tabs->on_select([this, index](auto tab) { switch_tab(index, tab); });
        p.tabs->on_activate([this, index](auto) { activate(index); window_.focus(*panes_[index]->list); });
        p.tabs->on_close([this, index](auto tab) { close_tab(index, tab); });
        p.address->on_submit([this, index] {
            auto& pane = *panes_[index];
            try { navigate(index, resolve_location(pane.address->text(), pane.state.active().history.current().path)); }
            catch (const PathInputError& error) { pane.error = error.message(); update(index); }
            catch (const std::exception&) { pane.error = L"Invalid folder path: " + pane.address->text(); update(index); }
        });
        p.search->on_change([this, index](const std::wstring& query) { filter(index, query); });
        p.search->on_submit([this, index] { window_.focus(*panes_[index]->list); });
        p.list->on_selection_change([this, index] {
            if (panes_[index]->syncing) return;
            save(index); update(index);
        });
        p.list->on_activate([this, index](const FileItem& item, FileActivation) { open(index, item); });
        p.list->set_thumbnails(true);
        p.list->on_thumbnail_error([this, index](ItemId, const std::wstring&) {
            ++panes_[index]->thumbnail_failures;
            update(index);
        });
        p.list->on_context_menu([this, index] { return menu(index); });
        update(index);
    }
    void activate(std::size_t index) {
        if (active_ == index) return;
        active_ = index;
        for (std::size_t i = 0; i < panes_.size(); ++i) if (panes_[i]) update(i);
    }
    void save(std::size_t index) {
        auto& p = *panes_[index];
        if (!p.loaded || p.syncing) return;
        auto& location = p.state.active().history.current();
        if (!p.state.pending()) location.query = p.search->text();
        if (const auto* item = p.list->model().selected_item()) location.selected_path = item->path;
        else location.selected_path.clear();
        if (const auto row = p.list->focused_index())
            location.focused_path = (*p.list->model().items())[p.list->model().visible_indices()[*row]].path;
        location.offset = p.list->offset();
    }
    void ensure_task(std::size_t index) {
        auto& p = *panes_[index];
        if (p.task) return;
        p.task = window_.create_view_task([slot = p.source, source = std::shared_ptr<DirectorySource>{},
            path = std::filesystem::path{}](const CancelCheck& cancel) mutable {
                auto desired = slot->get();
                if (!source || path != desired) {
                    path = std::move(desired);
                    source = std::make_shared<DirectorySource>(path);
                }
                return source->scan(cancel);
            },
            [this, index](ViewResult result) { receive(index, std::move(result)); });
    }
    void load(std::size_t index, Location location, std::optional<std::size_t> history = {}) {
        auto& p = *panes_[index];
        activate(index);
        p.error.clear(); p.notice.clear(); p.filtering = false;
        p.thumbnail_failures = 0;
        p.pending_serial = p.state.begin(location, history).serial;
        p.address->set_text(location.path.wstring());
        p.address->set_suggestion_context(p.state.active().history.current().path.wstring());
        p.search->set_text(location.query);
        p.source->set(location.path);
        p.reload_source = false;
        ensure_task(index);
        p.task->request(location.query, true);
        update(index);
    }
    void navigate(std::size_t index, std::filesystem::path path) {
        save(index);
        load(index, Location{std::move(path)});
    }
    void travel(std::size_t index, int delta) {
        save(index);
        auto& history = panes_[index]->state.active().history;
        if (auto target = history.relative(delta)) load(index, history.at(*target), target);
    }
    void go_up(std::size_t index) {
        const auto current = panes_[index]->state.active().history.current().path;
        const auto parent = parent_location(current);
        if (parent != current) navigate(index, parent);
    }
    void refresh(std::size_t index) {
        save(index);
        auto& p = *panes_[index];
        if (p.state.pending()) load(index, p.state.pending()->location, p.state.pending()->history_index);
        else load(index, p.state.active().history.current());
    }
    void filter(std::size_t index, const std::wstring& query) {
        auto& p = *panes_[index];
        if (p.syncing) return;
        save(index);
        p.state.set_query(query);
        p.error.clear(); p.notice.clear(); p.filtering = true;
        ensure_task(index);
        p.task->request(query, p.reload_source);
        p.reload_source = false;
        update(index);
    }
    void restore(std::size_t index) {
        auto& p = *panes_[index];
        const auto& location = p.state.active().history.current();
        const auto& model = p.list->model();
        std::optional<ItemId> selected, focused;
        for (const auto& item : *model.items()) {
            if (item.path == location.selected_path) selected = item.id;
            if (item.path == location.focused_path) focused = item.id;
        }
        p.list->restore_state(selected, focused, location.offset);
    }
    void receive(std::size_t index, ViewResult result) {
        auto& p = *panes_[index];
        const bool navigating = p.state.pending().has_value();
        const auto attempted = navigating ? p.state.pending()->location.path.wstring() : p.address->text();
        const bool success = result.view && result.error.empty();
        if (navigating && !p.state.finish(p.pending_serial, success)) return;
        p.address->set_suggestion_context(p.state.active().history.current().path.wstring());
        p.filtering = false;
        if (!success) {
            p.error = (result.error.empty() ? L"The scan was cancelled." : result.error) + L"  Path: " + attempted;
            p.address->set_text(p.state.active().history.current().path.wstring());
            p.search->set_text(p.state.active().history.current().query);
            p.source->set(p.state.active().history.current().path);
            p.reload_source = true;
        } else {
            p.syncing = true;
            p.list->set_view(std::move(result.view));
            if (navigating) restore(index);
            p.syncing = false;
            p.loaded = true;
            p.error.clear();
        }
        update(index);
    }
    void reset_view(std::size_t index) {
        auto& p = *panes_[index];
        p.syncing = true;
        p.loaded = false;
        p.list->clear_selection(); p.list->focus_list();
        p.list->set_items(nullptr); p.list->scroll_to(0);
        p.syncing = false;
    }
    void switch_tab(std::size_t index, std::uint64_t tab) {
        auto& p = *panes_[index];
        if (p.state.active().id == tab) return;
        save(index);
        if (!p.state.select(tab)) return;
        reset_view(index);
        load(index, p.state.active().history.current());
    }
    void add_tab(std::size_t index, std::optional<std::filesystem::path> path = {}) {
        auto& p = *panes_[index];
        save(index);
        if (p.state.tabs().size() == PaneState::maximum_tabs) {
            p.error = L"This pane already has 16 tabs. Close a tab first."; update(index); return;
        }
        p.state.add(path.value_or(p.state.active().history.current().path));
        reset_view(index);
        load(index, p.state.active().history.current());
        window_.focus(*p.list);
    }
    void close_tab(std::size_t index, std::uint64_t tab) {
        auto& p = *panes_[index];
        save(index);
        const bool active = p.state.active().id == tab;
        if (!p.state.close(tab)) { p.notice = L"The last tab stays open."; update(index); return; }
        if (active) {
            reset_view(index);
            load(index, p.state.active().history.current());
        } else update(index);
    }
    void open(std::size_t index, const FileItem& item) {
        activate(index);
        if (item.directory) navigate(index, item.path);
        else {
            auto& p = *panes_[index];
            p.error = launch_file(item, launch_associated_file);
            p.notice = p.error.empty() ? L"Opened: " + item.name : L"";
            update(index);
        }
    }
    void copy(std::size_t index, std::wstring path) {
        auto& p = *panes_[index];
        try { window_.copy_text(path); p.notice = L"Copied: " + path; p.error.clear(); }
        catch (const std::exception&) { p.error = L"Cannot copy the path."; }
        update(index);
    }
    void other_pane(std::size_t index, std::filesystem::path path) {
        split_->set_secondary_visible(true); split_button_->set_name(L"Single pane");
        const auto other = 1 - index;
        navigate(other, std::move(path));
        window_.focus(*panes_[other]->list);
    }
    std::vector<MenuItem> menu(std::size_t index) {
        auto& p = *panes_[index];
        activate(index);
        std::optional<FileItem> item;
        if (p.list->model().selected_index()) item = *p.list->model().selected_item();
        const auto folder = p.state.active().history.current().path;
        return {
            {L"Open\tEnter", [this, index, item] { if (item) open(index, *item); }, item.has_value()},
            {L"Open folder in new tab", [this, index, item] { if (item) add_tab(index, item->path); }, item && item->directory},
            {L"Open folder in other pane", [this, index, item] { if (item) other_pane(index, item->path); }, item && item->directory},
            {L"Copy full path\tCtrl+C", [this, index, item] { if (item) copy(index, item->path); }, item.has_value()},
            {{}, {}, true, false, true},
            {L"Copy folder path", [this, index, folder] { copy(index, folder.wstring()); }},
            {L"Copy status details", [this, index] { copy(index, panes_[index]->status->text()); }},
            {L"Refresh\tF5", [this, index] { refresh(index); }},
            {L"New tab\tCtrl+T", [this, index] { add_tab(index); }},
            {L"Close active tab\tCtrl+W", [this, index] { close_tab(index, panes_[index]->state.active().id); }},
            {L"Shell commands (native fallback)…", [this, index, item, folder] {
                try { window_.show_shell_commands(*panes_[index]->list, {item ? item->path : folder.wstring()}); }
                catch (const std::exception&) { panes_[index]->error = L"Shell commands are unavailable for this item."; update(index); }
            }},
        };
    }
    void toggle_split() {
        const bool show = !split_->secondary_visible();
        split_->set_secondary_visible(show);
        split_button_->set_name(show ? L"Single pane" : L"Split panes");
        if (show && !panes_[1]->loaded) load(1, panes_[1]->state.active().history.current());
        if (!show) {
            save(1);
            auto& p = *panes_[1];
            p.state.cancel();
            if (p.task) { p.task->cancel(); p.task.reset(); }
            p.source->set(p.state.active().history.current().path);
            reset_view(1);
            activate(0); window_.focus(*panes_[0]->list);
        }
    }
    void theme() { window_.set_theme(window_.theme() == ThemeMode::dark ? ThemeMode::light : ThemeMode::dark); }
    bool key(const KeyEvent& e) {
        auto index = active_;
        if (index && !split_->expanded()) { index = 0; activate(0); }
        auto& p = *panes_[index];
        if (e.control && e.key == Key::l) { window_.focus(*p.address, true); return true; }
        if (e.control && e.key == Key::f) { window_.focus(*p.search, true); return true; }
        if (e.control && e.key == Key::t) { add_tab(index); return true; }
        if (e.control && e.key == Key::w) { close_tab(index, p.state.active().id); return true; }
        if (e.control && e.key == Key::tab) { p.tabs->step(e.shift ? -1 : 1); return true; }
        if (e.control && e.shift && e.key == Key::p) { toggle_split(); return true; }
        if (e.alt && e.key == Key::left) { travel(index, -1); return true; }
        if (e.alt && e.key == Key::right) { travel(index, 1); return true; }
        if (e.alt && e.key == Key::up) { go_up(index); return true; }
        if (e.key == Key::f5) { refresh(index); return true; }
        if (e.key == Key::f6) {
            if (e.control) theme();
            else if (split_->expanded()) { activate(1 - index); window_.focus(*panes_[1 - index]->list); }
            return true;
        }
        if (e.key == Key::escape && p.state.pending()) {
            p.state.cancel(); p.filtering = false;
            if (p.task) { p.task->cancel(); p.task.reset(); }
            p.error = L"Navigation cancelled. The last valid folder stays open.";
            p.address->set_text(p.state.active().history.current().path.wstring());
            p.search->set_text(p.state.active().history.current().query);
            p.source->set(p.state.active().history.current().path);
            update(index); return true;
        }
        if (e.key == Key::escape && e.target == p.address.get()) {
            p.address->set_text(p.state.active().history.current().path.wstring());
            return true;
        }
        if (e.control && e.key == Key::c && e.target == p.list.get()) {
            if (p.list->model().selected_index()) copy(index, p.list->model().selected_item()->path);
            return true;
        }
        return false;
    }
    void update(std::size_t index) {
        auto& p = *panes_[index];
        const auto& history = p.state.active().history;
        const auto& path = history.current().path;
        if (p.breadcrumb_path != path) {
            std::vector<PathSegment> segments;
            std::filesystem::path prefix;
            std::map<ItemKey, std::filesystem::path> paths;
            for (const auto& part : path) {
                prefix /= part;
                const bool omitted = segments.size() == 64;
                if (omitted) { paths.erase(segments.back().key); segments.pop_back(); }
                std::uint64_t hash = 1469598103934665603ull;
                for (auto ch : prefix.wstring()) { hash ^= static_cast<std::uint64_t>(std::towlower(ch)); hash *= 1099511628211ull; }
                ItemKey key{hash ? hash : 1, 1};
                while (paths.contains(key)) ++key.version;
                segments.push_back({key, (omitted ? L"…\\" : L"") + part.wstring()}); paths.emplace(key, prefix);
            }
            p.breadcrumb->set_segments(std::move(segments)); p.breadcrumb_paths = std::move(paths); p.breadcrumb_path = path;
        }
        std::vector<TabItem> tabs;
        for (const auto& tab : p.state.tabs()) {
            const auto& location = tab.history.current().path;
            auto title = location.filename().wstring();
            if (title.empty()) title = location.wstring();
            tabs.push_back({tab.id, std::move(title)});
        }
        p.tabs->set_tabs(std::move(tabs), p.state.active().id);
        if (active_ == index) window_.set_title(L"XUI/Files - " + path.wstring());
        p.back->set_enabled(history.relative(-1).has_value());
        p.forward->set_enabled(history.relative(1).has_value());
        p.up->set_enabled(parent_location(path) != path);
        p.status->set_tone(p.error.empty() ? TextTone::secondary : TextTone::error);
        if (!p.error.empty()) p.status->set_text(p.error);
        else if (p.state.pending()) p.status->set_text(L"Reading: " + p.state.pending()->location.path.wstring() + L"  (Esc cancels)");
        else if (p.filtering) p.status->set_text(L"Filtering files...");
        else if (!p.notice.empty()) p.status->set_text(p.notice);
        else {
            const auto& model = p.list->model();
            auto text = std::to_wstring(model.visible_indices().size()) + L" of " + std::to_wstring(model.items()->size()) + L" items";
            if (p.thumbnail_failures) text += L"  |  Thumbnail failures: " + std::to_wstring(p.thumbnail_failures);
            if (model.selected_index()) text += L"  |  Selected: " + model.selected_item()->name;
            p.status->set_text(std::move(text));
        }
        p.list->set_empty_text(p.state.pending() ? L"Reading folder..." : !p.error.empty() ? L"Folder unavailable" :
            p.list->model().items()->empty() ? L"Nothing here yet" : L"No matches",
            !p.error.empty() ? L"Check the address or press F5 to retry." : L"Enter opens an item. Right-click shows commands.");
    }
};
}
int run_file_browser(const BrowserOptions& options) { return Browser(options).run(); }
}

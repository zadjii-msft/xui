#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/image.hpp"
#include "xui/suggestions.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/navigation.hpp"
#include "xui/shell_commands.hpp"
#include "xui/titlebar.hpp"
#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include "host_fixtures.hpp"
#include "gallery_catalog.hpp"
#include "parity_samples.hpp"
#include "gallery_reference.hpp"
#include "gallery_links.hpp"
#include "winui_gallery.hpp"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>

namespace {
using namespace xui;
using Panel = std::shared_ptr<Stack>;
std::filesystem::path host_directory() {
    return std::filesystem::absolute(std::filesystem::path(L"gallery-host-fixtures") / std::to_wstring(GetCurrentProcessId()));
}
Panel panel(Axis axis = Axis::vertical) {
    auto result = std::make_shared<Stack>(axis);
    result->set_spacing(10);
    return result;
}
std::shared_ptr<Label> label(Panel parent, std::wstring text, TextTone tone = TextTone::normal) {
    auto result = std::make_shared<Label>(std::move(text));
    result->set_tone(tone);
    result->set_wrapping(true);
    parent->add(result);
    return result;
}
std::shared_ptr<Button> button(Panel parent, std::wstring name, std::function<void()> action) {
    auto result = std::make_shared<Button>(std::move(name));
    result->on_click(std::move(action));
    parent->add(result);
    return result;
}
void set_code(DocumentText& code, std::wstring_view text) {
    code.set_text(std::wstring(text));
    const auto lines = 1 + std::count(code.text().begin(), code.text().end(), L'\r');
    code.set_preferred_size({400, std::clamp(24.0f + 20.0f * static_cast<float>(lines), 100.0f, 300.0f)});
}
class Suggestions final : public SuggestionSource {
public:
    SuggestionResult suggest(const SuggestionRequest& request, const std::function<bool()>& cancelled) override {
        SuggestionResult result;
        for (const auto* text : {L"Documents", L"Downloads", L"Design", L"Desktop"}) {
            if (cancelled()) return {};
            if (gallery::fold(text).find(gallery::fold(request.text)) != std::wstring::npos) result.items.emplace_back(text);
        }
        return result;
    }
};
class SampleShell final : public ShellCommandProvider {
public:
    explicit SampleShell(std::shared_ptr<Label> output) : output_(std::move(output)) {}
    std::vector<ShellCommandInfo> discover(std::stop_token stop) override {
        return stop.stop_requested() ? std::vector<ShellCommandInfo>{} :
            std::vector<ShellCommandInfo>{{{1, 1}, L"Inspect sample", L"inspect"}, {{2, 1}, L"Disabled sample", L"disabled", false}};
    }
    void invoke(ItemKey key) override { output_->set_text(L"Events: synthetic verb " + std::to_wstring(key.id)); }
private:
    std::shared_ptr<Label> output_;
};
class Gallery {
public:
    explicit Gallery(Window& window, const std::wstring& page, const std::wstring& image_path) : window_(window) {
        auto root = panel();
        root->set_padding({16, 12, 16, 12});
        auto header = panel(Axis::horizontal);
        auto title = label(header, L"XUI Control Gallery");
        title->set_heading(true);
        button(header, L"Theme", [this] { cycle_theme(); });
        auto style = button(header, window_.visual_style() == VisualStyle::winui ? L"Style: WinUI" : L"Style: Classic", {});
        style->set_automation_id(L"gallery-style");
        style->on_click([this, control = style.get()] {
            window_.set_visual_style(window_.visual_style() == VisualStyle::winui ? VisualStyle::classic : VisualStyle::winui);
            control->set_name(window_.visual_style() == VisualStyle::winui ? L"Style: WinUI" : L"Style: Classic");
        });
        root->add(header);
        auto body = panel(Axis::horizontal);
        nav_ = std::make_shared<NavigationView>(L"Control catalog");
        nav_->set_automation_id(L"gallery-catalog");
        nav_->set_pane_widths(260, 64);
        nav_->set_items(gallery::navigation_items());
        nav_->items()->set_automation_id(L"gallery-catalog-items");
        search_ = nav_->search();
        search_->set_name(L"Search controls");
        search_->set_placeholder(L"Search controls or categories");
        search_->set_shortcut_hint(L"Ctrl+F");
        search_->set_automation_id(L"gallery-search");
        count_ = std::make_shared<Label>(std::to_wstring(gallery::entries.size()) + L" examples");
        count_->set_tone(TextTone::secondary);
        count_->set_caption(true);
        body->add(nav_);
        pages_ = std::make_shared<PageView>();
        for (std::size_t i = 0; i < gallery::entries.size(); ++i) {
            if (i >= 17) {
                auto slot = panel();
                auto scroll = std::make_shared<ScrollView>(slot, std::wstring(gallery::entries[i].title) + L" example");
                scroll->set_automation_id(L"gallery-example-" + std::wstring(gallery::entries[i].id));
                pages_->add_page(scroll);
                deferred_[i] = [this, i, slot, image_path] {
                    const auto& entry = gallery::entries[i];
                    slot->set_padding({16, 12, 16, 16}); slot->set_surface(true);
                    auto heading = label(slot, entry.title); heading->set_heading(true);
                    heading->set_automation_id(L"gallery-page-" + std::wstring(entry.id));
                    introduction(slot, i);
                    auto demo = panel(); slot->add(demo);
                    auto output = label(slot, L"Events: ready", TextTone::accent);
                    output->set_automation_id(L"gallery-events-" + std::wstring(entry.id));
                    build(i, demo, output, image_path);
                    reference(slot, i);
                };
                continue;
            }
            auto content = panel();
            content->set_padding({16, 12, 16, 16});
            content->set_surface(true);
            const auto& entry = gallery::entries[i];
            label(content, entry.group, TextTone::secondary)->set_caption(true);
            auto heading = label(content, entry.title);
            heading->set_heading(true);
            heading->set_automation_id(L"gallery-page-" + std::wstring(entry.id));
            introduction(content, i);
            auto demo = panel();
            content->add(demo);
            auto output = std::make_shared<Label>(L"Events: ready");
            output->set_automation_id(L"gallery-events-" + std::wstring(entry.id));
            output->set_tone(TextTone::accent);
            build(i, demo, output, image_path);
            content->add(output);
            reference(content, i);
            auto scroll = std::make_shared<ScrollView>(content, std::wstring(entry.title) + L" example");
            scroll->set_automation_id(L"gallery-example-" + std::wstring(entry.id));
            pages_->add_page(scroll);
        }
        auto empty = panel();
        empty->set_padding({24, 24, 24, 24});
        label(empty, L"No matching controls")->set_heading(true);
        label(empty, L"Try a category: Input, Layout, Collections, Navigation, Media, Commands, Appearance.");
        button(empty, L"Clear search", [this] { nav_->set_filter(L""); nav_->set_expanded(true); window_.focus(*search_); });
        pages_->add_page(empty);
        body->add(pages_, 1);
        root->add(body, 1);
        auto footer = panel(Axis::horizontal);
        button(footer, L"Previous example", [this] { step(-1); });
        button(footer, L"Next example", [this] { step(1); });
        location_ = label(footer, L"", TextTone::secondary);
        footer->add(count_);
        root->add(footer);
        nav_->on_select([this](ItemKey key) {
            if (const auto index = gallery::entry_index(key)) show(*index);
            else if (gallery::navigation_link(key)) location_->set_text(L"Press Enter or double-click to open the handbook.");
        });
        nav_->on_activate([this](ItemKey key) {
            if (const auto index = gallery::entry_index(key)) { show(*index); window_.focus(*first_target()); }
            else if (const auto path = gallery::navigation_link(key)) open_documentation(path, location_);
        });
        nav_->on_filter([this](const auto&) { filter(); });
        search_->on_submit([this] {
            const auto keys = visible_entries();
            if (keys.empty()) return;
            const ItemKey displayed{selected_ + 1, 1};
            nav_->select(std::find(keys.begin(), keys.end(), displayed) != keys.end() ? displayed : keys.front());
            window_.focus(*nav_->items());
        });
        std::size_t selected{};
        for (std::size_t i = 0; i < gallery::entries.size(); ++i) if (page == gallery::entries[i].id) selected = i;
        nav_->select({selected + 1, 1});
        show(selected);
        window_.on_key([this](const KeyEvent& event) {
            if (selected_ == 30 && command_key_ && command_key_(event)) return true;
            if (event.control && event.key == Key::f) {
                nav_->set_expanded(true);
                return window_.focus(*search_, true);
            }
            if (event.key == Key::escape && event.target == search_.get()) {
                nav_->set_filter(L""); return true;
            }
            // F6 belongs to DataGrid header navigation while a grid has focus.
            if (event.key == Key::f6 && (!event.target || event.target->role() != ControlRole::data_grid)) {
                cycle_theme(); return true;
            }
            return false;
        });
        window_.set_content(root);
    }
private:
    Window& window_;
    std::shared_ptr<TextInput> search_;
    std::shared_ptr<NavigationView> nav_;
    std::shared_ptr<PageView> pages_;
    std::shared_ptr<Label> count_, location_;
    std::shared_ptr<Toggle> light_;
    std::array<std::shared_ptr<Control>, gallery::entries.size()> targets_;
    std::array<std::function<void()>, gallery::entries.size()> deferred_;
    std::array<std::function<void(std::uint64_t)>, gallery::entries.size()> update_code_;
    std::uint64_t selected_language_{4};
    std::size_t selected_{};
    std::function<bool(const KeyEvent&)> command_key_;
    void introduction(const Panel& parent, std::size_t index) {
        const auto& reference = gallery::references[index];
        label(parent, reference.usage, TextTone::secondary);
        label(parent, L"Try this example")->set_heading(true);
        label(parent, reference.exercise);
    }
    void open_documentation(std::wstring_view path, const std::shared_ptr<Label>& output) {
        output->set_text(gallery::open_documentation(path));
    }
    void select_language(std::uint64_t id) {
        if (selected_language_ == id) return;
        selected_language_ = id;
        for (const auto& update : update_code_) if (update) update(id);
    }
    void reference(const Panel& parent, std::size_t index) {
        const auto& entry = gallery::entries[index];
        const auto& reference = gallery::references[index];
        auto output = std::make_shared<Label>(L"");
        output->set_automation_id(L"gallery-reference-status-" + std::wstring(entry.id));
        output->set_wrapping(true);
        output->set_tone(TextTone::accent);
        label(parent, L"Usage and limits")->set_heading(true);
        label(parent, reference.notes);
        label(parent, L"Code example")->set_heading(true);
        auto language = std::make_shared<TabStrip>(L"Example language");
        language->set_automation_id(L"gallery-language-" + std::wstring(entry.id));
        language->set_tabs({{4, L".xui"}, {2, L"C#"}, {3, L"Rust"}, {1, L"C++"}}, selected_language_);
        parent->add(language);
        auto context = label(parent, L"", TextTone::secondary);
        context->set_automation_id(L"gallery-code-context-" + std::wstring(entry.id));
        auto code = std::make_shared<MultilineText>(std::wstring(entry.title) + L" C++ code");
        code->set_automation_id(L"gallery-code-" + std::wstring(entry.id));
        code->set_read_only(true);
        code->set_monospace(true);
        set_code(*code, entry.code);
        parent->add(code);
        auto actions = panel(Axis::horizontal);
        auto copy = button(actions, L"Copy code", [this, code, output] {
            try { window_.copy_text(code->text()); output->set_text(L"Events: code copied."); }
            catch (const std::exception&) { output->set_text(L"Events: clipboard is unavailable."); }
        });
        copy->set_automation_id(L"gallery-copy-code-" + std::wstring(entry.id));
        parent->add(actions);
        update_code_[index] = [index, code, context, copy, language](std::uint64_t id) {
            const auto& entry = gallery::entries[index];
            const auto& reference = gallery::references[index];
            const auto text = id == 2 ? reference.csharp : id == 3 ? reference.rust : id == 4 ? reference.xui : entry.code;
            const auto name = id == 2 ? L"C#" : id == 3 ? L"Rust" : id == 4 ? L".xui" : L"C++";
            code->set_name(std::wstring(entry.title) + L" " + name + L" code");
            set_code(*code, text);
            language->set_tabs(language->tabs(), id);
            copy->set_enabled(*text != L'\0');
            context->set_text(*text == L'\0' ? L"No example for this language. See Usage and limits." : L"");
            context->set_visible(*text == L'\0');
        };
        update_code_[index](selected_language_);
        language->on_select([this](std::uint64_t id) { select_language(id); });
        label(parent, L"Documentation")->set_heading(true);
        auto links = panel(Axis::horizontal);
        auto docs = button(links, L"Control reference", [this, index, output] {
            open_documentation(gallery::references[index].docs, output);
        });
        docs->set_automation_id(L"gallery-docs-" + std::wstring(entry.id));
        docs->set_help_text(gallery::documentation_url(reference.docs));
        auto copy_link = button(links, L"Copy documentation link", [this, index, output] {
            try {
                window_.copy_text(gallery::documentation_url(gallery::references[index].docs));
                output->set_text(L"Events: documentation link copied.");
            } catch (const std::exception&) { output->set_text(L"Events: clipboard is unavailable."); }
        });
        copy_link->set_automation_id(L"gallery-copy-docs-" + std::wstring(entry.id));
        button(links, L"Gallery source", [output] {
            output->set_text(gallery::open_url(L"https://github.com/zadjii-msft/xui/blob/main/demo/gallery.cpp"));
        });
        parent->add(links);
        parent->add(output);
    }
    Control* first_target() { return selected_ < targets_.size() && targets_[selected_] ? targets_[selected_].get() : search_.get(); }
    void cycle_theme() {
        const auto next = window_.theme() == ThemeMode::dark ? ThemeMode::light :
            window_.theme() == ThemeMode::light ? ThemeMode::high_contrast : ThemeMode::dark;
        window_.set_theme(next);
        light_->set_checked(next == ThemeMode::light);
    }
    void show(std::size_t index) {
        if (index < deferred_.size() && deferred_[index]) {
            auto build = std::move(deferred_[index]); deferred_[index] = {}; build();
        }
        selected_ = index;
        pages_->select(index);
        location_->set_text(index < gallery::entries.size() ? std::wstring(gallery::entries[index].title) + L"  |  Developer reference" : L"No results");
    }
    std::vector<ItemKey> visible_entries() const {
        std::vector<ItemKey> keys;
        const auto source = nav_->items()->source();
        for (std::size_t i = 0; i < source->size(); ++i)
            if (source->selectable(i)) keys.push_back(source->key(i));
        return keys;
    }
    void filter() {
        std::vector<ItemKey> keys;
        for (std::size_t i = 0; i < gallery::entries.size(); ++i)
            if (nav_->item_matches({i + 1, 1})) keys.push_back({i + 1, 1});
        count_->set_text(std::to_wstring(nav_->match_count()) + L" of " + std::to_wstring(gallery::entries.size()) + L" examples");
        const auto selected = nav_->selected();
        if (selected && std::find(keys.begin(), keys.end(), *selected) != keys.end())
            show(*gallery::entry_index(*selected));
        else show(keys.empty() ? gallery::entries.size() : *gallery::entry_index(keys.front()));
    }
    void step(int delta) {
        nav_->set_expanded(true);
        const auto keys = visible_entries();
        if (keys.empty()) return;
        const auto found = std::find(keys.begin(), keys.end(), ItemKey{selected_ + 1, 1});
        const auto current = found == keys.end() ? 0 : found - keys.begin();
        const auto size = static_cast<std::ptrdiff_t>(keys.size());
        const auto next = (current + delta + size) % size;
        nav_->select(keys[next]);
        show(*gallery::entry_index(keys[next]));
    }
    void build(std::size_t index, Panel demo, std::shared_ptr<Label> output, const std::wstring& image_path) {
        switch (index) {
        case 0: {
            label(demo, L"Enable an action from form state")->set_heading(true);
            auto name = std::make_shared<TextInput>(L"Your name");
            name->set_placeholder(L"Enter a name to enable Save");
            demo->add(name); targets_[index] = name;
            auto save = button(demo, L"Save greeting", [] {});
            auto permission = std::make_shared<Toggle>(L"Allow greeting updates");
            permission->set_checked(true); demo->add(permission);
            light_ = std::make_shared<Toggle>(L"Use light theme");
            light_->set_checked(window_.theme() == ThemeMode::light);
            demo->add(light_);
            auto greeting = label(demo, L"Your greeting will appear here.", TextTone::accent);
            auto status = label(demo, L"Enter a name to enable Save.", TextTone::secondary);
            save->set_enabled(false);
            // Callbacks borrow their controls. The page owns them for the window lifetime.
            auto update = [name = name.get(), save = save.get(), permission = permission.get(), status] {
                save->set_enabled(permission->checked() && !name->text().empty());
                status->set_text(!permission->checked() ? L"Updates are paused." :
                    name->text().empty() ? L"Enter a name to enable Save." : L"Ready to save your greeting.");
            };
            name->on_change([update](const auto&) { update(); });
            permission->on_change([update](bool) { update(); });
            save->on_click([name = name.get(), greeting, status, output] {
                greeting->set_text(L"Hello, " + name->text() + L"!");
                status->set_text(L"Greeting saved for this session.");
                output->set_text(L"Events: Save invoked.");
            });
            light_->on_change([this](bool value) { window_.set_theme(value ? ThemeMode::light : ThemeMode::dark); });
            break;
        }
        case 1: {
            auto clicks = std::make_shared<unsigned>();
            auto row = panel(Axis::horizontal); demo->add(row);
            auto action = button(row, L"Run action", [output, clicks] { output->set_text(L"Events: invoked " + std::to_wstring(++*clicks) + L" times."); });
            targets_[index] = action;
            for (const auto icon : {ButtonIcon::back, ButtonIcon::forward, ButtonIcon::refresh, ButtonIcon::add}) {
                auto b = button(row, icon == ButtonIcon::back ? L"Back" : icon == ButtonIcon::forward ? L"Forward" :
                    icon == ButtonIcon::refresh ? L"Refresh" : L"Add", [output] { output->set_text(L"Events: icon action invoked."); });
                b->set_icon(icon);
            }
            auto enabled = std::make_shared<Toggle>(L"Enable action"); enabled->set_checked(true); demo->add(enabled);
            enabled->on_change([action](bool value) { action->set_enabled(value); });
            break;
        }
        case 2: {
            auto a = std::make_shared<Toggle>(L"Allow updates"); demo->add(a); targets_[index] = a;
            auto b = std::make_shared<Toggle>(L"Show descriptions"); demo->add(b);
            a->on_change([output](bool value) { output->set_text(value ? L"Events: updates on." : L"Events: updates off."); });
            b->on_change([output](bool value) { output->set_text(value ? L"Events: descriptions on." : L"Events: descriptions off."); });
            auto disabled = std::make_shared<Toggle>(L"Unavailable choice"); disabled->set_enabled(false); demo->add(disabled);
            break;
        }
        case 3: {
            auto input = std::make_shared<TextInput>(L"Project name"); targets_[index] = input; demo->add(input);
            input->set_maximum_length(40); input->set_placeholder(L"Enter up to 40 UTF-16 units");
            input->on_change([output](const auto& text) { output->set_text(L"Events: " + std::to_wstring(text.size()) + L" UTF-16 units."); });
            input->on_submit([output] { output->set_text(L"Events: Enter submitted."); });
            auto caption = std::make_shared<Toggle>(L"Show caption"); caption->set_checked(true); demo->add(caption);
            caption->on_change([input](bool checked) { input->set_caption_visible(checked); });
            label(demo, L"Use PasswordInput for secrets, MultilineText for paragraphs, and RichText for formatted documents.", TextTone::secondary);
            break;
        }
        case 4: {
            auto input = std::make_shared<TextInput>(L"Suggested location"); targets_[index] = input; demo->add(input);
            input->set_search_style(true); input->set_placeholder(L"Type D, then use Down and Enter");
            input->set_suggestions(std::make_shared<Suggestions>());
            input->on_submit([input = input.get(), output] { output->set_text(L"Events: " + input->text()); });
            label(demo, L"Four in-memory suggestions. No filesystem access.");
            break;
        }
        case 5: {
            auto heading = label(demo, L"A measured heading"); heading->set_heading(true);
            label(demo, L"Body text: Unicode 日本語 and English");
            label(demo, L"Secondary caption", TextTone::secondary)->set_caption(true);
            label(demo, L"Accent tone", TextTone::accent);
            label(demo, L"Error tone", TextTone::error);
            targets_[index] = button(demo, L"Change heading", [heading] { heading->set_text(L"Updated with Label::set_text"); });
            break;
        }
        case 6: {
            auto row = panel(Axis::horizontal); row->set_padding({12, 12, 12, 12}); row->set_surface(true); demo->add(row);
            label(row, L"Fixed label");
            auto stretch = panel(); row->add(stretch, 1); label(stretch, L"Flexible column"); label(stretch, L"Second row");
            auto spacing = std::make_shared<Toggle>(L"Use wider spacing"); demo->add(spacing); targets_[index] = spacing;
            spacing->on_change([row, output](bool value) { row->set_spacing(value ? 24.0f : 10.0f); output->set_text(L"Events: layout updated."); });
            break;
        }
        case 7: {
            auto form = panel(); form->set_padding({12, 12, 12, 12});
            label(form, L"Workspace preferences")->set_heading(true);
            label(form, L"Scroll to review your workspace. Tab reveals each field.");
            auto workspace = std::make_shared<TextInput>(L"Workspace name"); workspace->set_text(L"Personal workspace"); form->add(workspace);
            for (auto text : {L"Restore this workspace when the app opens", L"Show previews in the file browser", L"Keep selected folders"}) form->add(std::make_shared<Toggle>(text));
            auto description = std::make_shared<TextInput>(L"Workspace description"); form->add(description);
            button(form, L"Apply preferences", [output] { output->set_text(L"Workspace preferences applied."); });
            button(form, L"Reset", [workspace, description, output] { workspace->set_text(L"Personal workspace"); description->set_text(L""); output->set_text(L"Preferences reset."); });
            auto scroll = std::make_shared<ScrollView>(form, L"Workspace preferences");
            scroll->set_automation_id(L"workspace-scroll"); scroll->set_preferred_size({480, 260}); demo->add(scroll); targets_[index] = scroll;
            break;
        }
        case 8: {
            auto list = std::make_shared<FileList>(L"Sample files"); list->set_preferred_size({480, 240}); targets_[index] = list;
            auto items = std::make_shared<std::vector<FileItem>>();
            for (unsigned i = 1; i <= 200; ++i) items->push_back({i, L"Fixture " + std::to_wstring(i), L"", i % 4 == 0});
            list->set_items(items); demo->add(list);
            list->on_activate([output](const FileItem& item, FileActivation) { output->set_text(L"Events: selected " + item.name + L". No file action."); });
            auto search = std::make_shared<TextInput>(L"Filter fixture names"); demo->add(search);
            search->set_search_style(true); search->on_change([list](const auto& text) { list->set_filter(text); });
            break;
        }
        case 9: {
            auto grid = std::make_shared<DataGrid>(L"Synthetic data");
            grid->set_columns({{L"Item", 240}, {L"Size", 150, true}});
            grid->set_source(std::make_shared<gallery::Numbers>());
            grid->set_preferred_size({480, 260}); demo->add(grid); targets_[index] = grid;
            grid->on_sort([grid = grid.get(), output](std::size_t, bool descending) {
                grid->set_source(std::make_shared<gallery::Numbers>(descending));
                output->set_text(descending ? L"Events: descending source." : L"Events: ascending source.");
            });
            grid->on_select([grid = grid.get(), output] {
                if (auto key = grid->selected()) output->set_text(L"Events: stable row " + std::to_wstring(key->id));
            });
            label(demo, L"Drag headers to reorder. Drag boundaries to resize.", TextTone::secondary);
            label(demo, L"F6: header  |  Ctrl+arrows: resize  |  Ctrl+Shift+arrows: reorder", TextTone::secondary)->set_caption(true);
            button(demo, L"Reverse columns", [grid] { grid->reorder_column(0, 1); });
            break;
        }
        case 10: {
            auto tabs = std::make_shared<TabStrip>(L"Sample documents"); demo->add(tabs); targets_[index] = tabs;
            tabs->set_tabs({{1, L"Notes"}, {2, L"Preview"}}, 1);
            auto next = std::make_shared<std::uint64_t>(2);
            tabs->on_select([output](auto id) { output->set_text(L"Events: document " + std::to_wstring(id)); });
            tabs->on_close([tabs = tabs.get(), output](auto id) {
                auto items = tabs->tabs(); std::erase_if(items, [id](const auto& item) { return item.id == id; });
                const auto selected = items.empty() ? std::optional<std::uint64_t>{} : items.front().id;
                tabs->set_tabs(std::move(items), selected); output->set_text(L"Events: document closed.");
            });
            button(demo, L"Add document", [tabs, next, output] {
                if (tabs->tabs().size() >= 12) { output->set_text(L"Events: sample limit is 12 tabs."); return; }
                auto items = tabs->tabs(); auto id = ++*next; items.push_back({id, L"Document " + std::to_wstring(id)});
                tabs->set_tabs(std::move(items), id); output->set_text(L"Events: document added.");
            });
            auto custom_colors = std::make_shared<Toggle>(L"Custom tab colors");
            demo->add(custom_colors);
            custom_colors->on_change([tabs](bool enabled) {
                tabs->set_colors(enabled ? TabColors{0x18222e, 0x26465e, 0xffffff,
                    0x202e3d, 0xcbd9e8, 0x34536c, 0x6687a3} : TabColors{});
            });
            break;
        }
        case 11: {
            auto first = panel(), second = panel();
            label(first, L"Primary pane"); first->add(std::make_shared<TextInput>(L"Primary note"));
            label(second, L"Secondary pane"); second->add(std::make_shared<TextInput>(L"Secondary note"));
            auto split = std::make_shared<SplitView>(first, second, L"Sample divider");
            split->set_preferred_size({720, 180}); demo->add(split); targets_[index] = split;
            auto toggle = std::make_shared<Toggle>(L"Show secondary pane"); toggle->set_checked(true); demo->add(toggle);
            toggle->on_change([split](bool value) { split->set_secondary_visible(value); });
            label(demo, L"Two panes need at least 610 DIPs of example width.", TextTone::secondary);
            break;
        }
        case 12: {
            auto pages = std::make_shared<PageView>(); pages->set_preferred_size({480, 110});
            for (auto text : {L"First page field", L"Second page field"}) {
                auto page = panel(); page->add(std::make_shared<TextInput>(text)); pages->add_page(page);
            }
            demo->add(pages);
            targets_[index] = button(demo, L"Switch content page", [pages, output] {
                pages->select(1 - pages->selected()); output->set_text(L"Events: page " + std::to_wstring(pages->selected() + 1));
            });
            break;
        }
        case 13: {
            auto path = std::make_shared<TextInput>(L"Preview image path");
            path->set_placeholder(L"Enter a PNG, JPEG, BMP, GIF, or TIFF path");
            path->set_text(image_path); demo->add(path); targets_[index] = path;
            auto image = std::make_shared<Image>(L"Workspace image preview"); image->set_preferred_size({320, 144}); demo->add(image);
            auto actions = panel(Axis::horizontal); demo->add(actions);
            button(actions, L"Load image", [image, path, output] {
                image->set_source(path->text(), {320, 144}); image->reload();
                output->set_text(L"Events: image requested. Decode errors appear in the preview.");
            });
            button(actions, L"Unload image", [image, output] { image->unload(); output->set_text(L"Events: image unloaded."); });
            break;
        }
        case 14: {
            auto chart = std::make_shared<HistoryChart>(L"Deterministic history"); chart->set_preferred_size({480, 180}); demo->add(chart);
            for (unsigned i = 0; i < 45; ++i) chart->append((i * 17) % 100);
            auto next = std::make_shared<unsigned>(45);
            targets_[index] = button(demo, L"Append sample", [chart, next, output] {
                chart->append(((*next)++ * 17) % 100); output->set_text(L"Events: retained samples " + std::to_wstring(chart->size()));
            });
            button(demo, L"Append gap", [chart] { chart->append(std::nullopt); });
            label(demo, L"No timer, sampler, or process metrics run on this page.");
            break;
        }
        case 15: {
            auto checked = std::make_shared<bool>(true);
            auto menu = button(demo, L"Context menu target", [output] { output->set_text(L"Events: right-click this button or press Shift+F10."); });
            targets_[index] = menu;
            label(demo, L"Right-click the target, or focus it and press Shift+F10.", TextTone::secondary);
            menu->on_context_menu([checked, output] {
                return std::vector<MenuItem>{
                    {L"&Run example\tEnter", [output] { output->set_text(L"Events: menu action invoked."); }},
                    {L"&Show details", [checked, output] { *checked = !*checked; output->set_text(*checked ? L"Events: details on." : L"Events: details off."); }, true, *checked},
                    {L"", {}, false, false, true},
                    {L"Unavailable action", {}, false}};
            });
            button(demo, L"Show confirmation", [this, output] {
                output->set_text(window_.confirm(L"Gallery confirmation", L"Apply this sample action? No files will change.") ?
                    L"Events: confirmation accepted." : L"Events: confirmation cancelled.");
            });
            label(demo, L"Confirmation uses TaskDialog or MessageBox, not a custom ContentDialog.", TextTone::secondary);
            break;
        }
        case 16: {
            for (auto mode : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast}) {
                auto b = button(demo, mode == ThemeMode::dark ? L"Dark theme" : mode == ThemeMode::light ? L"Light theme" : L"High contrast theme",
                    [this, mode, output] { window_.set_theme(mode); light_->set_checked(mode == ThemeMode::light); output->set_text(L"Events: theme changed."); });
                if (!targets_[index]) targets_[index] = b;
            }
            label(demo, L"Tab: move focus. Space: toggle. Enter: invoke.");
            label(demo, L"F6 cycles themes outside grids. Ctrl+F focuses catalog search.");
            label(demo, L"Native EDIT exposes ValuePattern. TextPattern depends on Windows.");
            label(demo, L"Language examples identify available binding APIs. The control reference describes native behavior.", TextTone::secondary);
            break;
        }
        case 17: {
            auto radio = std::make_shared<RadioGroup>(L"View mode");
            radio->set_automation_id(L"foundation-radio");
            radio->set_items({{10, L"List"}, {20, L"Details"}, {30, L"Unavailable view", false}}, 10);
            radio->set_preferred_size({320, 102});
            radio->on_change([output](auto id) { output->set_text(L"Events: view ID " + std::to_wstring(id)); });
            demo->add(radio); targets_[index] = radio;
            break;
        }
        case 18: {
            auto combo = std::make_shared<ComboBox>(L"Output format");
            combo->set_automation_id(L"foundation-combo");
            combo->set_items({{11, L"Text"}, {22, L"Markdown"}, {33, L"HTML"}}, 11);
            combo->on_change([output](auto id) { output->set_text(L"Events: committed format ID " + std::to_wstring(id)); });
            demo->add(combo); targets_[index] = combo;
            auto editable = std::make_shared<ComboBox>(L"Editable format", true);
            editable->set_items({{11, L"Text"}, {22, L"Markdown"}, {33, L"HTML"}}, 11);
            editable->on_edit([output](const auto& text) { output->set_text(L"Events: native text " + text); });
            editable->on_change([output](auto id) { output->set_text(L"Events: editable committed ID " + std::to_wstring(id)); });
            demo->add(editable);
            break;
        }
        case 19: {
            auto anchor = std::make_shared<Button>(L"Open retained popup");
            anchor->set_automation_id(L"foundation-popup");
            demo->add(anchor); targets_[index] = anchor;
            auto content = panel(); content->set_padding({10, 10, 10, 10});
            auto edit = std::make_shared<TextInput>(L"Popup native input"); edit->set_caption_visible(false);
            content->add(edit);
            auto range = std::make_shared<RangeInput>(L"Popup range"); content->add(range);
            auto nested_anchor = std::make_shared<Button>(L"Nested popup"); content->add(nested_anchor);
            auto popup = std::make_shared<Popup>(content); popup->set_preferred_size({340, 170});
            popup->on_dismiss([output](auto reason) { output->set_text(L"Events: popup dismissed (" + std::to_wstring(static_cast<int>(reason)) + L")."); });
            auto nested_content = panel(); nested_content->set_padding({10, 10, 10, 10});
            auto nested_toggle = std::make_shared<Toggle>(L"Nested independent choice"); nested_content->add(nested_toggle);
            auto nested = std::make_shared<Popup>(nested_content); nested->set_preferred_size({260, 64}); nested->set_placement(PopupPlacement::right);
            std::weak_ptr<Button> weak_anchor = anchor, weak_nested = nested_anchor;
            anchor->on_click([this, popup, weak_anchor, edit] { if (auto a = weak_anchor.lock()) window_.show_popup(popup, *a, edit.get()); });
            nested_anchor->on_click([this, nested, weak_nested] { if (auto a = weak_nested.lock()) window_.show_popup(nested, *a); });
            break;
        }
        case 20: {
            auto action = button(demo, L"Hover or focus for help", [output] { output->set_text(L"Events: help action invoked."); });
            action->set_automation_id(L"foundation-tooltip");
            action->set_help_text(L"Help appears after 600 ms. Focus stays on this action.");
            action->set_tooltip_delay(600); targets_[index] = action;
            break;
        }
        case 21: {
            auto repeat = button(demo, L"Hold to repeat", [output, count = 0]() mutable { output->set_text(L"Events: repeat " + std::to_wstring(++count)); });
            repeat->set_automation_id(L"foundation-repeat"); repeat->set_behavior(ButtonBehavior::repeat);
            auto toggle = std::make_shared<Button>(L"Toggle action"); toggle->set_behavior(ButtonBehavior::toggle);
            toggle->on_toggle([output](bool value) { output->set_text(value ? L"Events: action on." : L"Events: action off."); }); demo->add(toggle);
            auto split = std::make_shared<SplitButton>(L"Run primary", L"Run options");
            split->primary()->on_click([output] { output->set_text(L"Events: primary action only."); });
            auto content = panel(); label(content, L"Run options");
            auto option = std::make_shared<Toggle>(L"Run with diagnostics"); content->add(option);
            auto popup = std::make_shared<Popup>(content); popup->set_preferred_size({260, 80});
            std::weak_ptr<Button> secondary = split->secondary();
            split->secondary()->on_click([this, secondary, popup] { if (auto anchor = secondary.lock()) window_.show_popup(popup, *anchor); });
            demo->add(split); targets_[index] = repeat;
            break;
        }
        case 22: {
            auto number = std::make_shared<NumericInput>(L"Copies"); number->set_automation_id(L"foundation-number");
            number->set_range({1, 99, 1, 10}); number->set_value(3);
            number->on_change([output](double value) { output->set_text(L"Events: copies " + std::to_wstring(value)); });
            demo->add(number); targets_[index] = number->editor();
            label(demo, L"Invalid text stays visible with an error border. Up/Down changes the value.", TextTone::secondary);
            break;
        }
        case 23: {
            auto row = panel(Axis::horizontal);
            for (auto axis : {Axis::horizontal, Axis::vertical}) {
                auto range = std::make_shared<RangeInput>(axis == Axis::horizontal ? L"Horizontal scale" : L"Vertical scale");
                range->set_range({0, 100, 5, 20}); range->set_value(40); range->set_orientation(axis);
                range->set_fixed_size(axis == Axis::horizontal ? Size{280, 42} : Size{44, 160});
                range->on_preview([output](double value) { output->set_text(L"Events: preview " + std::to_wstring(value)); });
                range->on_change([output](double value) { output->set_text(L"Events: committed " + std::to_wstring(value)); });
                range->on_cancel([output](double value) { output->set_text(L"Events: cancelled, value " + std::to_wstring(value)); });
                if (axis == Axis::horizontal) { range->set_automation_id(L"foundation-range"); targets_[index] = range; }
                row->add(range);
            }
            demo->add(row);
            break;
        }
        case 24: {
            auto content = panel();
            auto text = std::make_shared<TextInput>(L"Detail note"); content->add(text);
            auto choice = std::make_shared<Toggle>(L"Keep detail selection"); content->add(choice);
            auto expander = std::make_shared<Expander>(L"Details", content);
            expander->set_automation_id(L"foundation-disclosure");
            expander->on_change([output](bool expanded) { output->set_text(expanded ? L"Events: details expanded." : L"Events: details collapsed."); });
            demo->add(expander); targets_[index] = expander;
            break;
        }
        case 25:
        case 50: {
            const bool circular = index == 50;
            std::shared_ptr<Progress> progress;
            if (circular) {
                progress = std::make_shared<ProgressRing>(L"Load preview");
                progress->set_fixed_size({64, 64});
            } else {
                progress = std::make_shared<Progress>(L"Sample task");
            }
            progress->set_value(40);
            progress->set_automation_id(circular ? L"gallery-progress-ring" : L"foundation-progress");
            demo->add(progress);
            auto row = panel(Axis::horizontal);
            targets_[index] = button(row, L"Advance", [progress, output] {
                progress->set_state(ProgressState::determinate); progress->set_value(std::min(100.0, progress->value() + 10));
                output->set_text(L"Events: progress advanced.");
            });
            button(row, L"Indeterminate", [progress, output] { progress->set_state(ProgressState::indeterminate); output->set_text(L"Events: indeterminate progress."); });
            button(row, L"Pause", [progress, output] { progress->set_state(ProgressState::paused); output->set_text(L"Events: progress paused."); });
            button(row, L"Error", [progress, output] { progress->set_state(ProgressState::error); output->set_text(L"Events: progress error."); });
            demo->add(row);
            auto visibility = std::make_shared<ToggleSwitch>(L"Show indicator"); visibility->set_checked(true);
            visibility->on_change([progress](bool value) { progress->set_visible(value); }); demo->add(visibility);
            label(demo, L"Only visible indeterminate indicators animate. Windows animation preferences also apply.", TextTone::secondary);
            if (!circular) {
                auto capacity = std::make_shared<Progress>(L"Storage capacity"); capacity->set_capacity(48, 128, L"GB"); demo->add(capacity);
                auto unknown = std::make_shared<Progress>(L"Unknown capacity"); unknown->set_state(ProgressState::unknown); demo->add(unknown);
            }
            break;
        }
        case 26: {
            auto items = std::make_shared<ItemsView>(L"Synthetic items"); items->set_automation_id(L"collection-items");
            auto full = std::make_shared<gallery::FixtureItems>(); items->set_items(full, full);
            items->set_preferred_size({480, 250}); items->set_help_text(L"Ctrl toggles. Shift selects a range. Drag tiles for a rectangle. F2 invokes the inline action.");
            auto modes = panel(Axis::horizontal);
            for (const auto mode : {ItemsPresentation::list, ItemsPresentation::tiles, ItemsPresentation::grouped}) {
                button(modes, mode == ItemsPresentation::list ? L"List" : mode == ItemsPresentation::tiles ? L"Tiles" : L"Groups",
                    [items, mode] { items->set_presentation(mode); });
            }
            demo->add(modes); demo->add(items); targets_[index] = items;
            auto actions = panel(Axis::horizontal);
            button(actions, L"Select filtered", [items, output] { items->set_select_all_scope(SelectAllScope::filtered); items->select_all(); output->set_text(L"Events: filtered selection, one range."); });
            button(actions, L"Select full source", [items, output] { items->set_select_all_scope(SelectAllScope::full_source); items->select_all(); output->set_text(L"Events: full-source selection, one range."); });
            demo->add(actions);
            auto even = std::make_shared<Toggle>(L"Show even IDs"); demo->add(even);
            even->on_change([items, full](bool value) { items->set_items(value ? std::make_shared<gallery::FixtureItems>(50000, 2, 2) : full, full); });
            items->on_selection([items = items.get(), output] {
                const auto focus = items->selection().focused();
                output->set_text(L"Events: focus " + std::to_wstring(focus ? focus->id : 0) + L", selection terms " + std::to_wstring(items->selection().storage_size()));
            });
            items->on_action([output](ItemKey key) { output->set_text(L"Events: inline item " + std::to_wstring(key.id)); });
            items->on_activate([output](ItemKey key) { output->set_text(L"Events: open item " + std::to_wstring(key.id)); });
            break;
        }
        case 27: {
            auto tree = std::make_shared<TreeView>(L"Synthetic tree"); tree->set_automation_id(L"collection-tree");
            tree->set_tree(std::make_shared<gallery::FixtureTree>()); tree->set_preferred_size({480, 260}); demo->add(tree); targets_[index] = tree;
            tree->on_request([tree = tree.get(), output](TreeRequest request) {
                tree->complete(request, std::make_shared<gallery::FixtureItems>(100000, request.node.id * 1000000 + 1, 1, false));
                output->set_text(L"Events: cached branch " + std::to_wstring(request.node.id) + L", 100,000 virtual children.");
            });
            tree->on_action([output](ItemKey key) { output->set_text(L"Events: tree action " + std::to_wstring(key.id)); });
            button(demo, L"Expand first branch", [tree] { tree->disclose({1, 1}, true); });
            button(demo, L"Collapse first branch", [tree] { tree->disclose({1, 1}, false); });
            label(demo, L"Right: expand or enter children. Left: collapse or select parent.", TextTone::secondary)->set_caption(true);
            break;
        }
        case 28: {
            auto navigation = std::make_shared<ItemsView>(L"Adaptive navigation");
            navigation->set_items(std::make_shared<gallery::FixtureItems>(8, 1, 1, false)); navigation->set_item_size({120, 40});
            auto detail = std::make_shared<Grid>();
            detail->set_tracks({{TrackSizing::automatic}, {TrackSizing::star}}, {{}, {}});
            detail->set_gap(8, 8); detail->set_padding({8, 8, 8, 8});
            auto title = std::make_shared<Label>(L"Shared detail controls"); detail->add(title, 0, 0, 1, 2);
            auto wrap = std::make_shared<Wrap>(); wrap->set_item_width(110);
            for (int i = 1; i <= 4; ++i) {
                auto action = std::make_shared<Button>(L"Action " + std::to_wstring(i));
                action->on_click([output, i] { output->set_text(L"Events: wrapped action " + std::to_wstring(i)); }); wrap->add(action);
            }
            detail->add(wrap, 1, 0, 1, 2);
            auto adaptive = std::make_shared<AdaptiveLayout>(navigation, detail); adaptive->set_preferred_size({600, 260});
            adaptive->set_breakpoint(520); adaptive->set_navigation_extent(170); demo->add(adaptive); targets_[index] = navigation;
            auto compact = std::make_shared<Toggle>(L"Compact recipe"); demo->add(compact);
            compact->on_change([adaptive, output](bool value) { adaptive->set_breakpoint(value ? 10000.0f : 520.0f); output->set_text(value ? L"Events: compact panes, same controls." : L"Events: width-based panes, same controls."); });
            auto overlay = std::make_shared<Toggle>(L"Overlay navigation"); demo->add(overlay);
            overlay->on_change([adaptive, output](bool value) {
                adaptive->set_compact_navigation(value ? CompactNavigation::overlay : CompactNavigation::stacked);
                adaptive->set_navigation_open(true);
                output->set_text(value ? L"Events: compact overlay uses the same navigation." : L"Events: compact stacked navigation.");
            });
            button(demo, L"Show navigation", [adaptive] { adaptive->set_navigation_open(true); });
            navigation->on_selection([navigation = navigation.get(), title] {
                if (const auto key = navigation->selection().focused()) title->set_text(L"Details for ID " + std::to_wstring(key->id));
            });
            break;
        }
        case 29: {
            auto grid = std::make_shared<DataGrid>(L"Filterable data"); grid->set_automation_id(L"collection-grid");
            grid->set_columns({{L"Item", 290, false, true, true}, {L"Size", 150, true}});
            grid->set_source(std::make_shared<gallery::Numbers>()); grid->set_full_source(grid->source());
            grid->set_preferred_size({480, 240}); demo->add(grid); targets_[index] = grid;
            grid->on_filter([grid = grid.get(), output](GridFilterRequest request) {
                const bool even = request.filters[0] == L"even";
                if (grid->complete_filter(request, std::make_shared<gallery::Numbers>(grid->descending(), even)))
                    output->set_text(even ? L"Events: even-ID filter, 50,000 rows." : L"Events: filter cleared, 100,000 rows.");
            });
            grid->on_sort([grid = grid.get()](std::size_t, bool descending) {
                grid->set_source(std::make_shared<gallery::Numbers>(descending, grid->filters()[0] == L"even"));
            });
            grid->on_filter_open([this, grid = grid.get()](std::size_t column) {
                auto content = panel(); content->set_padding({10, 10, 10, 10});
                auto edit = std::make_shared<TextInput>(L"Header filter"); edit->set_text(grid->filters()[column]);
                edit->set_placeholder(L"Type even, or leave empty"); content->add(edit);
                auto apply = std::make_shared<Button>(L"Apply filter"); content->add(apply);
                auto popup = std::make_shared<Popup>(content); popup->set_preferred_size({320, 140});
                std::weak_ptr<Popup> weak = popup;
                apply->on_click([this, grid, column, edit, weak] {
                    const auto text = edit->text();
                    if (auto p = weak.lock()) window_.dismiss_popup(*p);
                    grid->filter(column, text);
                });
                window_.show_popup(popup, *grid, edit.get());
            });
            auto actions = panel(Axis::horizontal);
            button(actions, L"Select all rows", [grid] { grid->select_all(); });
            button(actions, L"Even IDs", [grid] { grid->filter(0, L"even"); });
            button(actions, L"Clear filter", [grid] { grid->filter(0, L""); }); demo->add(actions);
            button(demo, L"Reorder table columns", [grid] { grid->reorder_column(0, 1); });
            label(demo, L"F6: headers. F4: sort/filter/check. Space: act. Ctrl+A: select all.", TextTone::secondary)->set_caption(true);
            break;
        }
        case 30: {
            auto surface = std::make_shared<CommandSurface>(L"Gallery commands");
            surface->menu()->set_automation_id(L"command-menu");
            surface->editor()->set_automation_id(L"command-search");
            std::vector<CommandRecord> records{
                {7, 0, L"Sample commands", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::section},
                {1, 0, L"Open sample", [output] { output->set_text(L"Events: Open sample."); }, true, {}, ButtonIcon::forward,
                    {L"Enter", L"Ctrl+O"}, L"Pin", [output] { output->set_text(L"Events: Pin only. Primary did not run."); }},
                {2, 0, L"Checked command", [output] { output->set_text(L"Events: checked action."); }, true, true},
                {3, 0, L"Disabled command", {}, false},
                {4, 0, L"", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::separator},
                {8, 0, L"Other actions", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::section},
                {5, 0, L"More actions", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::submenu},
                {6, 5, L"Nested action", [output] { output->set_text(L"Events: nested action."); }}};
            auto commands = std::make_shared<CommandSet>(std::move(records)); surface->set_commands(commands);
            auto bindings = std::make_shared<CommandBindings>(); bindings->bind({static_cast<std::uint16_t>(Key::o), true}, 1);
            command_key_ = [commands, bindings](const KeyEvent& event) {
                return bindings->invoke(*commands, {static_cast<std::uint16_t>(event.key), event.control, event.shift, event.alt});
            };
            auto open = std::make_shared<Button>(L"Open command palette"); demo->add(open); targets_[index] = open;
            open->on_click([this, open = open.get(), surface] { window_.show_commands(surface, *open); });
            auto bar = std::make_shared<CommandBar>(); bar->set_fixed_size({340, 40});
            std::vector<CommandRecord> toolbar;
            for (std::uint64_t i = 1; i <= 6; ++i) toolbar.push_back({i, 0, L"Action " + std::to_wstring(i),
                [output, i] { output->set_text(L"Events: toolbar action " + std::to_wstring(i)); }});
            bar->set_commands(std::make_shared<CommandSet>(std::move(toolbar)));
            bar->on_overflow([this, bar = bar.get()] {
                auto overflow = std::make_shared<CommandSurface>(L"Toolbar overflow", false);
                overflow->set_commands(bar->overflow_commands()); window_.show_commands(overflow, *bar->overflow_button());
            });
            demo->add(bar); label(demo, L"Search groups matching commands by section. F2 runs only the independent pin action.", TextTone::secondary);
            break;
        }
        case 31: {
            auto path = std::make_shared<Breadcrumb>(); path->set_fixed_size({460, 38});
            path->set_segments({{{1, 1}, L"Home"}, {{2, 1}, L"Projects"}, {{3, 1}, L"Library"}, {{4, 1}, L"Samples"}, {{5, 1}, L"Current"}});
            path->on_navigate([output](ItemKey key) { output->set_text(L"Events: navigate request " + std::to_wstring(key.id)); });
            path->on_overflow([this, path = path.get()] {
                auto surface = std::make_shared<CommandSurface>(L"Earlier locations", false);
                surface->set_commands(path->overflow_commands()); window_.show_commands(surface, *path->overflow_button());
            });
            demo->add(path);
            auto picker = std::make_shared<LocationPicker>();
            picker->navigation()->set_items(std::make_shared<gallery::FixtureItems>(12, 1, 1, false));
            picker->navigation()->on_navigate([this, weak = std::weak_ptr<Popup>(picker->popup()), output](ItemKey key) {
                if (auto popup = weak.lock()) window_.dismiss_popup(*popup, PopupDismissReason::commit);
                output->set_text(L"Events: chosen location " + std::to_wstring(key.id));
            });
            picker->navigation()->on_query([weak = std::weak_ptr<NavigationPane>(picker->navigation())](NavigationQuery request) {
                if (auto pane = weak.lock()) pane->complete(request, std::make_shared<gallery::FixtureItems>(request.text.empty() ? 12 : 4, 1, 1, false));
            });
            picker->toolbar()->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
                {1, 0, L"Back", [output] { output->set_text(L"Events: provider back request."); }},
                {2, 0, L"Up", [output] { output->set_text(L"Events: provider parent request."); }}}));
            auto open = std::make_shared<Button>(L"Choose location"); demo->add(open); targets_[index] = open;
            open->on_click([this, open = open.get(), picker] { window_.show_location_picker(picker, *open); });
            label(demo, L"These locations are synthetic. Escape never sends a navigation request.", TextTone::secondary);
            break;
        }
        case 32: {
            auto nav = std::make_shared<NavigationPane>(); nav->set_fixed_size({220, 250});
            nav->set_items(std::make_shared<gallery::FixtureItems>(20));
            nav->on_navigate([output](ItemKey key) { output->set_text(L"Events: quick access " + std::to_wstring(key.id)); });
            auto items = std::make_shared<ItemsView>(L"Navigation content");
            items->set_items(std::make_shared<gallery::FixtureItems>(100));
            auto layout = std::make_shared<AdaptiveLayout>(nav, items); layout->set_preferred_size({520, 250});
            layout->set_breakpoint(450); layout->set_navigation_extent(220); layout->set_compact_navigation(CompactNavigation::overlay);
            demo->add(layout); targets_[index] = items;
            auto picker = std::make_shared<LocationPicker>(L"Anchored quick access");
            picker->navigation()->set_items(nav->items()->source());
            picker->navigation()->on_navigate([output, weak = std::weak_ptr<NavigationPane>(nav)](ItemKey key) {
                if (auto pane = weak.lock()) pane->items()->select(key);
                output->set_text(L"Events: anchored quick access " + std::to_wstring(key.id));
            });
            auto open = std::make_shared<Button>(L"Open quick access"); demo->add(open);
            open->on_click([this, open = open.get(), picker, nav] {
                picker->navigation()->items()->set_selection(nav->items()->selection());
                window_.show_location_picker(picker, *open);
            });
            auto view = std::make_shared<ViewPicker>(items);
            auto settings = std::make_shared<Button>(L"View settings"); demo->add(settings);
            settings->on_click([this, settings = settings.get(), view, output] {
                window_.show_popup(view->popup(), *settings, view->choices().get());
                output->set_text(L"Events: radio and size controls change the visible ItemsView.");
            });
            break;
        }
        case 33: {
            auto session = std::make_shared<ShellCommandSession>(std::make_shared<SampleShell>(output));
            targets_[index] = button(demo, L"Discover synthetic Shell commands", [session, output] {
                session->discover(); output->set_text(L"Events: 2 synthetic commands discovered. No verb ran.");
            });
            button(demo, L"Invoke synthetic inspect", [session] { session->invoke({1, 1}); });
            auto path = std::make_shared<TextInput>(L"Shell item path"); path->set_maximum_length(32767); demo->add(path);
            auto open = std::make_shared<Button>(L"Shell commands (native fallback)"); open->set_enabled(false); demo->add(open);
            path->on_change([weak = std::weak_ptr<Button>(open)](const std::wstring& text) { if (auto button = weak.lock()) button->set_enabled(!text.empty()); });
            open->on_click([this, open = open.get(), path, output] {
                try { window_.show_shell_commands(*open, {path->text()}); }
                catch (const std::exception&) { output->set_text(L"Events: Shell commands are unavailable for this item."); }
            });
            label(demo, L"Native Shell verbs can change files. Only an explicit choice runs a verb. Third-party calls cannot always cancel.", TextTone::secondary);
            break;
        }
        case 34: {
            if (auto caption = window_.titlebar()) {
                caption->tabs()->set_tabs({{1, L"Gallery"}, {2, L"Preview"}}, 1);
                caption->tabs()->on_select([output](std::uint64_t id) { output->set_text(L"Events: titlebar tab " + std::to_wstring(id)); });
                targets_[index] = button(demo, L"Select Preview tab", [caption] { caption->tabs()->select(2); });
                label(demo, L"The real window caption contains these tabs. Drag empty caption space. Right-click opens the system menu.");
                label(demo, L"Caption buttons send Windows system commands. The window title remains XUI Control Gallery.", TextTone::secondary);
            } else targets_[index] = button(demo, L"System titlebar is active", [output] { output->set_text(L"Events: start without --system-titlebar for the custom caption."); });
            break;
        }
        case 35: {
            auto content = std::make_shared<Stack>(Axis::vertical);
            auto editor = std::make_shared<TextInput>(L"Document title"); editor->set_placeholder(L"A title is required"); content->add(editor);
            auto dialog = std::make_shared<ContentDialog>(L"Save document", content);
            dialog->on_validate([editor] { return editor->text().empty() ? L"Enter a document title." : L""; });
            dialog->on_result([output](DialogResult result) { output->set_text(result == DialogResult::primary ? L"Events: dialog saved" : L"Events: dialog canceled"); });
            auto open = std::make_shared<Button>(L"Open content dialog"); demo->add(open); targets_[index] = open;
            open->on_click([this, open = open.get(), dialog, editor] { window_.show_dialog(dialog, *open, editor.get()); });
            label(demo, L"Owner controls stay disabled until OK or Cancel closes the dialog. Empty titles remain visible.", TextTone::secondary);
            break;
        }
        case 36: {
            auto status = std::make_shared<InlineStatus>(L"The document is ready."); status->set_dismissible(true); demo->add(status);
            status->set_action(L"Inspect", [output] { output->set_text(L"Events: status action"); });
            status->on_dismiss([output] { output->set_text(L"Events: status dismissed"); });
            targets_[index] = button(demo, L"Show warning", [status] { status->set_message(L"The document has unsaved changes.", StatusSeverity::warning); status->show(); });
            button(demo, L"Show error", [status] { status->set_message(L"Save failed. The document has unsaved changes.", StatusSeverity::error); status->show(); });
            break;
        }
        case 37: {
            auto text = std::make_shared<MultilineText>(L"Multiline notes");
            text->set_text(L"Native Unicode notes\rSecond paragraph: \u65e5\u672c\u8a9e \U0001f642\rSelect, edit, undo, and scroll.");
            text->on_change([output](const std::wstring& value) { output->set_text(L"Events: document length " + std::to_wstring(value.size())); });
            demo->add(text); targets_[index] = text;
            auto read_only = std::make_shared<Toggle>(L"Read-only document"); demo->add(read_only);
            read_only->on_change([text](bool value) { text->set_read_only(value); });
            button(demo, L"Undo document edit", [text] { text->command(TextCommand::undo); });
            button(demo, L"Redo document edit", [text] { text->command(TextCommand::redo); });
            break;
        }
        case 38: {
            label(demo, L"Use a test password only. This example never authenticates or logs the value.", TextTone::secondary);
            auto password = std::make_shared<PasswordInput>(L"Test password"); password->set_automation_id(L"gallery-password");
            password->on_change([output] { output->set_text(L"Events: password changed (value hidden)"); });
            demo->add(password); targets_[index] = password;
            password->set_reveal_policy(PasswordRevealPolicy::explicit_request);
            auto reveal = std::make_shared<Toggle>(L"Reveal test password"); demo->add(reveal);
            reveal->on_change([password](bool value) { password->set_revealed(value); });
            button(demo, L"Clear password", [password, reveal] { password->set_password(L""); password->set_revealed(false); reveal->set_checked(false); });
            break;
        }
        case 39: {
            auto rich = std::make_shared<RichText>(L"Rich document"); rich->set_read_only(true);
            rich->set_runs({{L"Styled document\r", true}, {L"Native italic text\r", false, true},
                {L"An explicit documentation link", false, false, true, L"https://example.com/docs"}});
            rich->on_link([output](const std::wstring&) { output->set_text(L"Events: link requested (no browser launched)"); });
            demo->add(rich); targets_[index] = rich;
            auto editable = std::make_shared<Toggle>(L"Edit rich document"); demo->add(editable);
            editable->on_change([rich](bool value) { rich->set_read_only(!value); });
            label(demo, L"Select a link and press Ctrl+Enter, or click it. Paste accepts plain Unicode only.", TextTone::secondary);
            break;
        }
        case 40: {
            auto date = std::make_shared<DateTimePicker>(L"Document date");
            date->set_value({2026, 9, 13}); date->set_range({2020, 1, 1}, {2030, 12, 31});
            date->on_change([output](DateTimeValue value) { output->set_text(L"Events: date day " + std::to_wstring(value.day)); });
            demo->add(date); targets_[index] = date;
            auto time = std::make_shared<DateTimePicker>(L"Document time", DateTimePresentation::time);
            time->set_value({2026, 9, 13, 14, 30}); demo->add(time);
            time->on_change([output](DateTimeValue value) { output->set_text(L"Events: time hour " + std::to_wstring(value.hour)); });
            auto calendar = std::make_shared<DateTimePicker>(L"Document calendar", DateTimePresentation::calendar);
            calendar->set_value({2026, 9, 13}); demo->add(calendar);
            calendar->on_change([output](DateTimeValue value) { output->set_text(L"Events: calendar day " + std::to_wstring(value.day)); });
            label(demo, L"Windows owns locale formats and the calendar popup. Native date controls keep system styling.", TextTone::secondary);
            break;
        }
        case 41: {
            auto color = std::make_shared<ColorPicker>(L"Document color"); color->set_value({45, 100, 230, 180});
            color->on_change([output](RgbaColor value) { output->set_text(L"Events: RGBA " + std::to_wstring(value.red) + L", " +
                std::to_wstring(value.green) + L", " + std::to_wstring(value.blue) + L", " + std::to_wstring(value.alpha)); });
            demo->add(color); targets_[index] = color->channels()[0]->editor();
            label(demo, L"Alpha changes the checkerboard preview. Arrow keys change channel values. Each swatch has an RGBA name.", TextTone::secondary);
            break;
        }
        case 42: {
            auto canvas = std::make_shared<VectorCanvas>(L"Retained vector scene"); canvas->set_automation_id(L"gallery-vector");
            auto box = VectorShape::rectangle(11, {24, 35, 110, 95});
            box.fill = {0.1f, 0.55f, 0.9f, 1}; box.name = L"Transformed blue rectangle"; box.interactive = true;
            box.transform = {0.94, 0.34, -0.34, 0.94, 40, 0}; box.clip = Rect{10, 10, 260, 170};
            auto circle = VectorShape::ellipse(12, {200, 50, 110, 95});
            circle.fill = {0.9f, 0.55f, 0.15f, 1}; circle.name = L"Gold ellipse"; circle.interactive = true;
            canvas->set_scene(std::make_shared<const VectorScene>(std::vector<VectorShape>{box, circle}));
            canvas->on_select([output](ShapeId id) { output->set_text(L"Events: selected shape " + std::to_wstring(id)); });
            canvas->set_preferred_size({480, 280}); demo->add(canvas); targets_[index] = canvas->accessible_items();
            label(demo, L"Click a shape or select its named list entry. Ellipses use 64 straight segments.", TextTone::secondary);
            break;
        }
        case 43: {
            auto map = std::make_shared<MapView>(); map->set_automation_id(L"gallery-map");
            map->set_overlay({{{101, {0, 179}, L"Authored east marker: 0 N, 179 E"},
                {102, {10, -179}, L"Authored west marker: 10 N, 179 W"}}, {{{{0, 179}, {10, -179}}}}});
            map->set_view({5, 179}, 2);
            map->on_select([output](ShapeId id) { output->set_text(L"Events: selected map marker " + std::to_wstring(id)); });
            map->set_preferred_size({480, 320}); demo->add(map); targets_[index] = map;
            auto actions = panel(Axis::horizontal); demo->add(actions);
            button(actions, L"Map zoom in", [map] { const auto b = map->canvas_bounds(); map->zoom_at(1, {b.width / 2, b.height / 2}); });
            button(actions, L"Map zoom out", [map] { const auto b = map->canvas_bounds(); map->zoom_at(-1, {b.width / 2, b.height / 2}); });
            button(actions, L"Map reset", [map] { map->set_view({5, 179}, 2); });
            label(demo, L"Drag or use arrows to pan. Plus/minus zoom. Markers are authored coordinates, not a geographic dataset.", TextTone::secondary);
            break;
        }
        case 44: {
            auto media = std::make_shared<MediaPlayback>(); media->set_automation_id(L"gallery-media");
            media->set_preferred_size({480, 240}); demo->add(media); targets_[index] = media;
            auto actions = panel(Axis::horizontal); demo->add(actions);
            button(actions, L"Load owned tone", [media] {
                auto file = host_directory() / L"tone.wav"; host_fixtures::wave(file); media->load_local(file.wstring());
            });
            button(actions, L"Load owned video", [media] {
                auto file = host_directory() / L"video.avi"; host_fixtures::video(file); media->load_local(file.wstring());
            });
            auto playback = panel(Axis::horizontal); demo->add(playback);
            auto play = button(playback, L"Play", [media] { media->play(); });
            auto pause = button(playback, L"Pause", [media] { media->pause(); });
            auto stop = button(playback, L"Stop", [media] { media->stop(); });
            auto unload = button(playback, L"Unload media", [media] { media->unload(); });
            auto seek = std::make_shared<RangeInput>(L"Seek seconds"); seek->set_range({0, 3, 0.1, 1}); seek->on_change([media](double value) { media->seek(value); }); demo->add(seek);
            auto volume = std::make_shared<RangeInput>(L"Playback volume"); volume->set_range({0, 1, 0.05, 0.2}); volume->set_value(0.5);
            volume->on_change([media](double value) { media->set_volume(value); }); demo->add(volume);
            std::array<std::weak_ptr<Control>, 5> transport{play, pause, stop, seek, unload};
            for (const auto& weak : transport) if (auto control = weak.lock()) control->set_enabled(false);
            media->on_state([output, transport, weak_media = std::weak_ptr<MediaPlayback>(media)](HostState state) {
                output->set_text(L"Events: media state " + std::to_wstring(static_cast<int>(state)));
                const bool loaded = state == HostState::ready || state == HostState::playing || state == HostState::paused || state == HostState::stopped;
                const bool enabled[]{loaded && state != HostState::playing, state == HostState::playing,
                    loaded && state != HostState::stopped, loaded, state != HostState::idle && state != HostState::suspended};
                for (std::size_t i = 0; i < transport.size(); ++i) if (auto control = transport[i].lock()) control->set_enabled(enabled[i]);
                if (state == HostState::ready) if (auto player = weak_media.lock()) if (auto slider = transport[3].lock())
                    static_cast<RangeInput&>(*slider).set_range({0, std::min(player->duration(), 604800.0), 0.1, 1});
            });
            label(demo, L"Load creates an owned tone or video. Play is explicit. Unload before opening an XUI popup.", TextTone::secondary);
            break;
        }
        case 45: {
            auto web = std::make_shared<WebContent>(); web->set_automation_id(L"gallery-web");
            web->set_profile_root((host_directory() / L"web-profiles").wstring());
            web->set_preferred_size({480, 270}); demo->add(web); targets_[index] = web;
            web->on_state([output](HostState state) { output->set_text(L"Events: web state " + std::to_wstring(static_cast<int>(state))); });
            auto actions = panel(Axis::horizontal); demo->add(actions);
            button(actions, L"Load owned HTML", [web] { web->set_html(L"<!doctype html><html><body style='background:#18324d;color:white;font:20px sans-serif'><h1 id='title'>Owned WebView2 content</h1><button onclick=\"document.getElementById('title').textContent='DOM changed'\">Change DOM</button><input aria-label='Owned web editor' value='Native web editing'></body></html>"); });
            button(actions, L"Read DOM", [web, output] { web->evaluate(L"document.getElementById('title').textContent", [output](std::wstring json, std::wstring error) { output->set_text(L"Events: " + (error.empty() ? json : error)); }); });
            button(actions, L"Focus web", [web] { web->focus_content(); });
            auto lifecycle = panel(Axis::horizontal); demo->add(lifecycle);
            button(lifecycle, L"Stop web", [web] { web->stop(); }); button(lifecycle, L"Reload web", [web] { web->reload(); });
            button(lifecycle, L"Unload web", [web] { web->unload(); });
            label(demo, L"Requires XUI_ENABLE_WEBVIEW2, the pinned SDK, and an installed Edge WebView2 runtime. No installer runs.", TextTone::secondary);
            label(demo, L"No external origins are allowed. Hiding unloads the engine. Unload before an XUI popup.", TextTone::secondary);
            break;
        }
        case 46: {
            auto nav = std::make_shared<NavigationView>(L"Workspace navigation");
            nav->set_automation_id(L"gallery-navigation-view");
            nav->set_maximum_size({480, 360});
            nav->search()->set_name(L"Filter workspace");
            nav->set_items({
                {{101, 1}, {}, L"Workspace home", ButtonIcon::home, {}, {}, true, true, true, NavigationSection::header},
                {{102, 1}, {}, L"Workspace settings", ButtonIcon::settings, {}, {}, true, true, true, NavigationSection::footer},
                {{1, 1}, {}, L"Projects", ButtonIcon::folder, {}, {}, true, false},
                {{2, 1}, ItemKey{1, 1}, L"Reports", ButtonIcon::folder, L"analytics", {}, true, false},
                {{3, 1}, ItemKey{2, 1}, L"Weekly report", ButtonIcon::library, L"summary", L"3"},
                {{4, 1}, ItemKey{2, 1}, L"Archived report", ButtonIcon::library, {}, L"Locked", false},
                {{5, 1}, ItemKey{1, 1}, L"Project notes", ButtonIcon::library},
                {{6, 1}, {}, L"Reference", ButtonIcon::folder, {}, {}, true, false, false},
                {{7, 1}, ItemKey{6, 1}, L"API reference", ButtonIcon::search, L"help"}
            });
            nav->select({3, 1});
            nav->on_select([output](ItemKey key) { output->set_text(L"Events: workspace selected " + std::to_wstring(key.id)); });
            nav->on_activate([output](ItemKey key) { output->set_text(L"Events: workspace activated " + std::to_wstring(key.id)); });
            nav->on_filter([output, weak = std::weak_ptr<NavigationView>(nav)](const auto&) {
                if (auto view = weak.lock()) output->set_text(L"Events: workspace matches " + std::to_wstring(view->match_count()));
            });
            auto frame = panel(Axis::horizontal); frame->add(nav); demo->add(frame);
            targets_[index] = nav->items();
            auto actions = panel(Axis::horizontal); demo->add(actions);
            button(actions, L"Filter reports", [nav] { nav->set_expanded(true); nav->set_filter(L"reports"); });
            button(actions, L"Reset workspace filter", [nav] { nav->set_filter(L""); });
            label(demo, L"Use the menu button to collapse the pane. Group arrows reveal nested items. Header and footer shortcuts stay visible.",
                TextTone::secondary);
            break;
        }
        case 47: {
            label(demo, L"Select a folder to show its children. Select a document to remove later columns.", TextTone::secondary);
            label(demo, L"Use Left and Right between columns. Enter or double-click reports activation without an external action.",
                TextTone::secondary);
            auto path = std::make_shared<gallery::FixtureMillerPath>();
            auto columns = std::make_shared<MillerColumns>(L"Project library");
            columns->set_automation_id(L"gallery-miller-columns");
            columns->set_column_width(200);
            columns->set_preferred_size({640, 300});
            columns->set_columns(path->columns());
            columns->on_selection([this, index, view = columns.get(), path, output](std::size_t column, ItemKey key) {
                if (!path->select(column, key)) return;
                view->set_columns(path->columns());
                targets_[index] = view->column_list(view->active_column());
                const auto& source = path->columns()[column].source;
                const auto row = source->find(key);
                output->set_text(L"Events: selected " + source->item(*row).primary + L" in column " +
                    std::to_wstring(column + 1) + L". " + std::to_wstring(path->columns().size()) + L" columns.");
            });
            columns->on_activate([view = columns.get(), output](std::size_t column, ItemKey key) {
                const auto& source = view->columns()[column].source;
                if (const auto row = source->find(key))
                    output->set_text(L"Events: activated " + source->item(*row).primary + L" in column " +
                        std::to_wstring(column + 1) + L". No external action.");
            });
            demo->add(columns);
            targets_[index] = columns->column_list(0);
            auto actions = panel(Axis::horizontal); demo->add(actions);
            button(actions, L"Show deep path", [this, index, columns, path, output] {
                *path = gallery::FixtureMillerPath{};
                for (std::size_t column = 0; column + 1 < gallery::FixtureMillerItems::levels; ++column)
                    path->select(column, path->columns()[column].source->key(0));
                columns->set_columns(path->columns());
                columns->set_active_column(path->columns().size() - 1);
                targets_[index] = columns->column_list(columns->active_column());
                output->set_text(L"Events: eight columns. Scroll horizontally to inspect the path.");
            })->set_automation_id(L"gallery-miller-deep-path");
            button(actions, L"Reset path", [this, index, columns, path, output] {
                *path = gallery::FixtureMillerPath{};
                columns->set_columns(path->columns());
                columns->set_active_column(0);
                targets_[index] = columns->column_list(0);
                output->set_text(L"Events: project library reset.");
            });
            label(demo, L"Eight levels and 28 siblings per column use immutable in-memory sources. No filesystem or network access.",
                TextTone::secondary);
            break;
        }
        case 48: {
            auto notifications = std::make_shared<ToggleSwitch>(L"Send notifications");
            notifications->set_automation_id(L"gallery-toggle-switch");
            notifications->set_checked(true);
            notifications->on_change([output](bool value) {
                output->set_text(value ? L"Events: notifications on." : L"Events: notifications off.");
            });
            demo->add(notifications); targets_[index] = notifications;
            auto enabled = std::make_shared<Toggle>(L"Enable switch"); enabled->set_checked(true);
            enabled->on_change([notifications](bool value) { notifications->set_enabled(value); }); demo->add(enabled);
            auto unavailable = std::make_shared<ToggleSwitch>(L"Unavailable preference");
            unavailable->set_checked(true); unavailable->set_enabled(false); demo->add(unavailable);
            label(demo, L"Space changes the focused switch on release. Enter leaves its value unchanged.", TextTone::secondary);
            break;
        }
        case 49: {
            auto pin = std::make_shared<ToggleButton>(L"Pin preview");
            pin->set_automation_id(L"gallery-toggle-button");
            pin->set_checked(true);
            pin->on_toggle([output](bool value) { output->set_text(value ? L"Events: preview pinned." : L"Events: preview unpinned."); });
            demo->add(pin); targets_[index] = pin;
            auto enabled = std::make_shared<Toggle>(L"Enable pin action"); enabled->set_checked(true);
            enabled->on_change([pin](bool value) { pin->set_enabled(value); }); demo->add(enabled);
            auto unavailable = std::make_shared<ToggleButton>(L"Unavailable action");
            unavailable->set_checked(true); unavailable->set_enabled(false); demo->add(unavailable);
            button(demo, L"Reset pin", [pin, output] {
                pin->set_checked(true);
                output->set_text(L"Events: pin reset without a toggle callback.");
            });
            break;
        }
        case 51: {
            auto check = std::make_shared<CheckBox>(L"Include attachments");
            check->set_automation_id(L"gallery-checkbox");
            check->set_three_state(true);
            check->set_state(CheckState::indeterminate);
            check->on_change([output](CheckState state) {
                output->set_text(state == CheckState::indeterminate ? L"Events: mixed attachments." :
                    state == CheckState::checked ? L"Events: all attachments." : L"Events: no attachments.");
            });
            demo->add(check); targets_[index] = check;
            auto three = std::make_shared<ToggleSwitch>(L"Cycle through three states"); three->set_checked(true);
            three->on_change([check](bool value) { check->set_three_state(value); }); demo->add(three);
            auto enabled = std::make_shared<Toggle>(L"Enable checkbox"); enabled->set_checked(true);
            enabled->on_change([check](bool value) { check->set_enabled(value); }); demo->add(enabled);
            button(demo, L"Set mixed state", [check, output] {
                check->set_state(CheckState::indeterminate);
                output->set_text(L"Events: mixed state set without a change callback.");
            });
            label(demo, L"Mixed describes a group with different values. Existing Toggle and ToggleSwitch remain binary.", TextTone::secondary);
            break;
        }
        case 52: {
            auto link = std::make_shared<HyperlinkButton>(L"Learn about this sample");
            link->set_automation_id(L"gallery-hyperlink-button");
            link->on_click([output] { output->set_text(L"Events: help requested. No browser was opened."); });
            demo->add(link); targets_[index] = link;
            auto enabled = std::make_shared<Toggle>(L"Enable help link"); enabled->set_checked(true);
            enabled->on_change([link](bool value) { link->set_enabled(value); }); demo->add(enabled);
            auto unavailable = std::make_shared<HyperlinkButton>(L"Unavailable documentation");
            unavailable->set_enabled(false); demo->add(unavailable);
            label(demo, L"The application handles activation. This sample performs no navigation or network request.", TextTone::secondary);
            break;
        }
        case 53: {
            auto selector = std::make_shared<SelectorBar>(L"Task filter");
            selector->set_automation_id(L"gallery-selector-bar");
            selector->set_items({{1, L"All"}, {2, L"Active"}, {3, L"Completed"}, {4, L"Archived", false}}, 1);
            selector->on_change([output](std::uint64_t id) {
                output->set_text(L"Events: task filter ID " + std::to_wstring(id));
            });
            demo->add(selector); targets_[index] = selector;
            auto enabled = std::make_shared<Toggle>(L"Enable task filter"); enabled->set_checked(true);
            enabled->on_change([selector](bool value) { selector->set_enabled(value); }); demo->add(enabled);
            button(demo, L"Reset task filter", [selector, output] {
                selector->set_selected(1);
                output->set_text(L"Events: task filter reset without a change callback.");
            });
            label(demo, L"One Tab stop. Arrow keys change the selection and skip disabled choices.", TextTone::secondary);
            break;
        }
        case 54: {
            auto row = panel(Axis::horizontal);
            label(row, L"Unread notifications");
            auto badge = std::make_shared<InfoBadge>(L"Unread notifications");
            badge->set_automation_id(L"gallery-info-badge"); badge->set_count(7);
            row->add(badge); demo->add(row);
            auto actions = panel(Axis::horizontal);
            targets_[index] = button(actions, L"Add notification", [badge, output] {
                badge->set_count(badge->count() + 1);
                output->set_text(L"Events: notification count " + std::to_wstring(badge->count()));
            });
            button(actions, L"Show dot", [badge, output] { badge->set_dot(); output->set_text(L"Events: notification dot."); });
            button(actions, L"Show icon", [badge, output] { badge->set_icon(ButtonIcon::bookmark); output->set_text(L"Events: notification icon."); });
            demo->add(actions);
            button(demo, L"Reset notification count", [badge, output] {
                badge->set_count(7); output->set_text(L"Events: notification count reset.");
            });
            label(demo, L"The badge describes status and never receives keyboard focus.", TextTone::secondary);
            break;
        }
        case 55: {
            auto menu = std::make_shared<MenuBar>(L"Document menu");
            menu->set_automation_id(L"gallery-menu-bar");
            menu->set_commands(gallery::menu_bar_commands([output](std::wstring message) {
                output->set_text(L"Events: " + message);
            }));
            demo->add(menu);
            auto editor = std::make_shared<TextInput>(L"Document title"); editor->set_text(L"Untitled sample");
            demo->add(editor); targets_[index] = editor;
            auto enabled = std::make_shared<Toggle>(L"Enable document menu"); enabled->set_checked(true);
            enabled->on_change([menu](bool value) { menu->set_enabled(value); }); demo->add(enabled);
            label(demo, L"F10 enters the menu. Alt+F opens File. Arrow keys navigate; Escape returns focus.", TextTone::secondary);
            label(demo, L"Recent samples contains a nested menu. Publish and Cut are disabled. No external actions run.", TextTone::secondary);
            break;
        }
        }
    }
};
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::wstring page = L"forms", image_path;
    WindowOptions options{L"XUI Control Gallery", {1040, 700}};
    bool experiment = false;
#ifdef XUI_WINUI_GALLERY
    options.visual_style = VisualStyle::winui;
    experiment = true;
#endif
    options.custom_titlebar = true;
    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view arg = argv[i];
            if (arg == L"--page" && i + 1 < argc) page = argv[++i];
            else if (arg == L"--image" && i + 1 < argc) image_path = argv[++i];
            else if (arg == L"--light") options.theme = ThemeMode::light;
            else if (arg == L"--high-contrast") options.theme = ThemeMode::high_contrast;
            else if (arg == L"--winui") {
                options.visual_style = VisualStyle::winui;
                experiment = true;
            }
            else if (arg == L"--winui-catalog") {
                options.visual_style = VisualStyle::winui;
                experiment = false;
            }
            else if (arg == L"--system-titlebar") options.custom_titlebar = false;
        }
        LocalFree(argv);
    }
    Window window(options);
    if (experiment) {
        auto gallery = winui_gallery::compose(window);
        return Application::run(window);
    }
    Gallery gallery(window, page, image_path);
    return Application::run(window);
}

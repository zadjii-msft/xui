#include "browser.hpp"
#include "directory.hpp"
#include "xui/application.hpp"

namespace xui {
int run_file_browser(const BrowserOptions& options) {
    Window window({L"XUI - " + options.folder.wstring(), {924, 641}, options.theme, {420, 300}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({20, 16, 20, 12});
    root->set_spacing(8);
    auto title = std::make_shared<Stack>(Axis::vertical);
    auto brand = std::make_shared<Label>(L"XUI  /  FILES");
    brand->set_tone(TextTone::accent);
    brand->set_caption(true);
    brand->set_preferred_size({0, 20});
    auto folder_name = options.folder.filename().wstring();
    if (folder_name.empty()) folder_name = options.folder.wstring();
    auto heading = std::make_shared<Label>(std::move(folder_name));
    heading->set_heading(true);
    heading->set_preferred_size({0, 31});
    auto path = std::make_shared<Label>(options.folder.wstring());
    path->set_tone(TextTone::secondary);
    path->set_caption(true);
    path->set_preferred_size({0, 21});
    auto title_line = std::make_shared<Stack>(Axis::horizontal);
    auto search_caption = std::make_shared<Label>(L"Search files");
    search_caption->set_tone(TextTone::secondary);
    search_caption->set_caption(true);
    search_caption->set_preferred_size({110, 20});
    title_line->add(brand, 1);
    title_line->add(search_caption);
    title->add(title_line);
    title->add(heading);
    title->add(path);
    root->add(title);
    auto search = std::make_shared<TextInput>(L"Search files");
    search->set_search_style(true);
    search->set_placeholder(L"Filter this folder");
    search->set_shortcut_hint(L"Ctrl+F");
    search->set_preferred_size({0, 44});
    root->add(search);
    auto columns = std::make_shared<Stack>(Axis::horizontal);
    columns->set_separator_after(true);
    columns->set_padding({40, 0, 36, 0});
    auto name = std::make_shared<Label>(L"NAME");
    auto type = std::make_shared<Label>(L"TYPE");
    for (const auto& label : {name, type}) {
        label->set_tone(TextTone::secondary);
        label->set_caption(true);
        label->set_preferred_size({80, 28});
    }
    columns->add(name, 1);
    columns->add(type);
    auto surface = std::make_shared<Stack>(Axis::vertical);
    surface->set_surface(true);
    surface->set_padding({1, 1, 1, 1});
    surface->set_spacing(8);
    surface->add(columns);
    auto list = std::make_shared<FileList>();
    list->set_automation_id(L"browser-files");
    surface->add(list, 1);
    root->add(surface, 1);
    auto status = std::make_shared<Label>(L"Reading folder...");
    status->set_caption(true);
    status->set_automation_id(L"browser-status");
    status->set_tone(TextTone::secondary);
    status->set_preferred_size({0, 24});
    root->add(status);
    window.set_content(root);

    struct State { bool loading{}, filtering{}; std::wstring error, query; };
    auto state = std::make_shared<State>();
    auto update_status = [state, list, status] {
        status->set_tone(state->error.empty() ? TextTone::secondary : TextTone::error);
        if (!state->error.empty()) {
            status->set_text(state->error);
            list->set_empty_text(L"Folder unavailable", L"See the status below. Press F5 to try again.");
        } else if (state->loading) {
            status->set_text(L"Reading folder... (F5 restarts the scan)");
            list->set_empty_text(L"Reading folder...", L"You can keep using the window while files load.");
        } else if (state->filtering) {
            status->set_text(L"Filtering files...");
        } else {
            const auto& model = list->model();
            auto text = std::to_wstring(model.visible_indices().size()) + L" of " +
                std::to_wstring(model.items()->size()) + L" items";
            if (model.selected_index()) text += L" | Selected: " + model.selected_item()->name;
            else text += L"  |  Tab: focus    Ctrl+C: copy path    F5: refresh    F6: theme";
            status->set_text(std::move(text));
            list->set_empty_text(model.items()->empty() ? L"Nothing here yet" : L"No matches",
                model.items()->empty() ? L"This folder has no items." : L"Try a different file name.");
        }
    };
    auto source = std::make_shared<DirectorySource>(options.folder);
    auto task = window.create_view_task([source](const CancelCheck& cancel) { return source->scan(cancel); },
        [state, list, update_status](ViewResult result) {
            state->loading = state->filtering = false;
            state->error = std::move(result.error);
            if (result.view) list->set_view(std::move(result.view));
            update_status();
        });
    auto refresh = [state, task, update_status] {
        task->request(state->query, true);
        state->loading = true;
        state->error.clear();
        update_status();
    };
    auto filter = [state, task, search, update_status] {
        if (search->text() == state->query) return;
        state->query = search->text();
        task->request(state->query);
        state->filtering = true;
        update_status();
    };
    search->on_change([filter](const std::wstring&) { filter(); });
    search->on_submit([&window, list, filter] { filter(); window.focus(*list); });
    list->on_selection_change(update_status);
    auto copy = [&window, list, status] {
        if (!list->model().selected_index()) return;
        const auto& path = list->model().selected_item()->path;
        try {
            window.copy_text(path);
            status->set_text(L"Copied path: " + path);
        } catch (const std::exception&) {
            status->set_tone(TextTone::error);
            status->set_text(L"Cannot copy the selected path.");
        }
    };
    auto focus_search = [&window, search] { window.focus(*search, true); };
    window.on_key([&window, list, copy, refresh, focus_search](const KeyEvent& event) {
        if (event.control && event.key == Key::f) { focus_search(); return true; }
        if (event.key == Key::f5) { refresh(); return true; }
        if (event.key == Key::f6) {
            window.set_theme(window.theme() == ThemeMode::dark ? ThemeMode::light : ThemeMode::dark);
            return true;
        }
        if (event.control && event.key == Key::c && event.target == list.get()) { copy(); return true; }
        return false;
    });
    list->on_context_menu([&window, list, copy, refresh, focus_search] {
        return std::vector<MenuItem>{
            {L"Copy selected path\tCtrl+C", copy, list->model().selected_index().has_value()},
            {L"Refresh\tF5", refresh}, {L"Search\tCtrl+F", focus_search},
            {{}, {}, true, false, true},
            {L"Dark theme", [&window] { window.set_theme(ThemeMode::dark); }, true, window.theme() == ThemeMode::dark},
            {L"Light theme", [&window] { window.set_theme(ThemeMode::light); }, true, window.theme() == ThemeMode::light}
        };
    });
    refresh();
    const int result = Application::run(window);
    list->on_selection_change({});
    list->on_context_menu({});
    search->on_change({});
    search->on_submit({});
    return result;
}
}

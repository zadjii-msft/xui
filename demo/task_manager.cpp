#include "xui/application.hpp"
#include "xui/data_grid.hpp"
#include "processes.hpp"
#include <windows.h>
#include <shellapi.h>
#include <filesystem>

namespace {
using namespace xui;
using namespace task_manager;
std::shared_ptr<Label> label(std::wstring text, const wchar_t* id = L"", float height = 26) {
    auto value = std::make_shared<Label>(std::move(text));
    value->set_auto_size(false); value->set_preferred_size({800, height}); value->set_automation_id(id);
    return value;
}
std::shared_ptr<Stack> stack(Axis axis, float spacing = 8) {
    auto value = std::make_shared<Stack>(axis); value->set_spacing(spacing); return value;
}
struct TaskManager {
    Window window{{L"XUI Task Manager", {1080, 780}, ThemeMode::dark, {540, 540}}};
    std::shared_ptr<Sampler> sampler = std::make_shared<Sampler>();
    std::shared_ptr<SampleTask> task;
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<const ProcessView> view;
    std::shared_ptr<DataGrid> grid = std::make_shared<DataGrid>(L"Processes");
    std::shared_ptr<PageView> pages = std::make_shared<PageView>();
    std::shared_ptr<TabStrip> tabs = std::make_shared<TabStrip>(L"Task Manager pages");
    std::shared_ptr<TextInput> search = std::make_shared<TextInput>(L"Search processes");
    std::shared_ptr<Button> pause = std::make_shared<Button>(L"Pause");
    std::shared_ptr<Button> rate = std::make_shared<Button>(L"Rate: 1 s");
    std::shared_ptr<Button> end = std::make_shared<Button>(L"End task");
    std::shared_ptr<Button> location = std::make_shared<Button>(L"Open file location");
    std::shared_ptr<Label> cpu = label(L"\u2014", L"cpu-value", 32);
    std::shared_ptr<Label> memory = label(L"\u2014", L"memory-value", 32);
    std::shared_ptr<Label> counts = label(L"\u2014", L"process-count", 32);
    std::shared_ptr<Label> status = label(L"Collecting the first snapshot...", L"sample-status", 24);
    std::shared_ptr<Label> action = label(L"End task requires confirmation. Protected processes remain listed.", L"action-status", 24);
    std::shared_ptr<Label> selection = label(L"Select a process to inspect it.", L"selection-summary", 26);
    std::shared_ptr<Label> details_title = label(L"No process selected", L"details-title", 36);
    std::shared_ptr<Label> details_identity = label(L"Select a row in Processes.", L"details-identity");
    std::shared_ptr<Label> details_metrics = label(L"", L"details-metrics");
    std::shared_ptr<Label> details_memory = label(L"", L"details-memory");
    std::shared_ptr<Label> details_path = label(L"", L"details-path", 32);
    std::shared_ptr<Label> details_availability = label(L"", L"details-availability", 32);
    std::shared_ptr<Label> performance_memory = label(L"", L"performance-memory");
    std::shared_ptr<Label> performance_commit = label(L"", L"performance-commit");
    std::shared_ptr<Label> performance_cpu = label(L"", L"performance-cpu");
    std::shared_ptr<Label> history_hint = label(L"60 samples \u00b7 1-second interval \u00b7 gaps mark unavailable data", L"history-hint");
    std::shared_ptr<HistoryChart> cpu_chart = std::make_shared<HistoryChart>(L"CPU history \u00b7 0\u2013100%");
    std::shared_ptr<HistoryChart> memory_chart = std::make_shared<HistoryChart>(L"Physical RAM history");
    bool paused{};
    bool metadata_hint{};
    unsigned seconds{1};
    std::uint64_t samples{};
    std::uint64_t action_sequence{};
    std::wstring selected_path;
    std::wstring sampling_error;

    TaskManager() {
        auto root = stack(Axis::vertical, 10);
        root->set_padding({22, 18, 22, 16});
        auto title_row = stack(Axis::horizontal);
        auto title = label(L"Task Manager", L"task-manager-title", 40); title->set_heading(true);
        auto theme = std::make_shared<Button>(L"Theme"); theme->set_automation_id(L"theme");
        theme->on_click([this] {
            window.set_theme(window.theme() == ThemeMode::dark ? ThemeMode::light :
                window.theme() == ThemeMode::light ? ThemeMode::high_contrast : ThemeMode::dark);
        });
        title_row->add(title, 1); title_row->add(theme); root->add(title_row);
        auto summary = stack(Axis::horizontal, 12);
        const auto tile = [&](const wchar_t* name, const auto& value) {
            auto tile = stack(Axis::vertical, 0); tile->set_padding({14, 8, 14, 8}); tile->set_surface(true);
            auto caption = label(name, L"", 22); caption->set_caption(true); caption->set_tone(TextTone::secondary);
            value->set_heading(true); tile->add(caption); tile->add(value); summary->add(tile, 1);
        };
        tile(L"CPU", cpu); tile(L"PHYSICAL RAM", memory); tile(L"PROCESSES / THREADS", counts);
        root->add(summary);
        auto commands = stack(Axis::horizontal, 8);
        pause->set_automation_id(L"pause"); rate->set_automation_id(L"sample-rate");
        auto refresh = std::make_shared<Button>(L"Refresh"); refresh->set_automation_id(L"refresh");
        auto sampling = label(L"System activity", L"sampling-title", 38); sampling->set_tone(TextTone::secondary);
        commands->add(sampling, 1); commands->add(pause); commands->add(rate); commands->add(refresh); root->add(commands);
        pause->on_click([this] { set_paused(!paused); });
        rate->on_click([this] {
            seconds = seconds == 1 ? 2 : seconds == 2 ? 5 : 1; task->set_interval(seconds * 1000);
            rate->set_name(L"Rate: " + std::to_wstring(seconds) + L" s");
            history_hint->set_text(L"60 samples \u00b7 " + std::to_wstring(seconds) + L"-second interval \u00b7 gaps mark unavailable data");
            cpu_chart->append({}); memory_chart->append({});
        });
        refresh->on_click([this] { task->refresh(); });
        tabs->set_automation_id(L"task-pages");
        tabs->set_tabs({{1, L"Processes"}, {2, L"Performance"}, {3, L"Details"}}, 1);
        tabs->on_select([this](auto id) { pages->select(static_cast<std::size_t>(id - 1)); });
        root->add(tabs);
        auto process_page = stack(Axis::vertical, 8);
        search->set_search_style(true); search->set_preferred_size({700, 44});
        search->set_automation_id(L"process-search"); search->set_placeholder(L"Search by process name or PID");
        search->set_shortcut_hint(L"Ctrl+F");
        search->on_change([this](const auto&) { rebuild(); }); search->on_submit([this] { window.focus(*grid); });
        process_page->add(search);
        grid->set_automation_id(L"process-grid");
        grid->set_columns({{L"Name", 260}, {L"PID", 80, true}, {L"CPU", 92, true},
            {L"Working set", 142, true}, {L"Threads", 92, true}, {L"I/O total", 132, true}, {L"Counter access", 190}});
        grid->set_sort(2, true);
        grid->on_sort([this](auto, auto) { rebuild(); });
        grid->on_select([this] {
            sampler->select(grid->selected()); update_details();
            metadata_hint = paused;
            if (paused) action->set_text(L"Sampling is paused. Refresh once to load the selected image path.");
        });
        grid->on_activate([this] { show_details(); });
        grid->on_context_menu([this] {
            const auto* p = selected();
            return std::vector<MenuItem>{
                {L"Show details", [this] { show_details(); }, p != nullptr},
                {L"Copy PID", [this] { if (const auto* p = selected()) window.copy_text(std::to_wstring(p->pid)); }, p != nullptr},
                {L"End task...", [this] { end_selected(); }, p && can_end(*p, GetCurrentProcessId())},
                {L"", {}, false, false, true},
                {paused ? L"Resume updates" : L"Pause updates", [this] { set_paused(!paused); }},
                {L"Refresh", [this] { task->refresh(); }}};
        });
        process_page->add(grid, 1); process_page->add(selection);
        auto process_actions = stack(Axis::horizontal);
        auto help = label(L"F6: headers \u00b7 Enter: details \u00b7 Shift+wheel: horizontal scroll", L"grid-help", 38);
        help->set_caption(true); help->set_tone(TextTone::secondary);
        auto inspect = std::make_shared<Button>(L"Details"); inspect->set_automation_id(L"show-details");
        inspect->on_click([this] { show_details(); });
        end->set_automation_id(L"end-task"); end->set_enabled(false); end->on_click([this] { end_selected(); });
        process_actions->add(help, 1); process_actions->add(inspect); process_actions->add(end);
        process_page->add(process_actions);
        pages->add_page(process_page);
        auto perf = stack(Axis::vertical, 10);
        perf->add(performance_cpu); perf->add(performance_memory); perf->add(performance_commit);
        cpu_chart->set_automation_id(L"cpu-history"); memory_chart->set_automation_id(L"memory-history");
        cpu_chart->set_preferred_size({700, 170}); memory_chart->set_preferred_size({700, 170});
        perf->add(cpu_chart); perf->add(memory_chart);
        history_hint->set_caption(true); history_hint->set_tone(TextTone::secondary); perf->add(history_hint);
        auto caveat = label(L"I/O includes file, network, and other transfers. Working set includes shared resident pages.", L"metric-definitions");
        caveat->set_caption(true); perf->add(caveat);
        pages->add_page(std::make_shared<ScrollView>(perf, L"Performance graphs"));
        auto detail = stack(Axis::vertical, 12); detail->set_padding({18, 14, 18, 14}); detail->set_surface(true);
        details_title->set_heading(true);
        detail->add(details_title); detail->add(details_identity); detail->add(details_metrics); detail->add(details_memory);
        auto path_caption = label(L"IMAGE PATH", L"", 22); path_caption->set_caption(true); path_caption->set_tone(TextTone::secondary);
        detail->add(path_caption); detail->add(details_path); detail->add(details_availability);
        auto detail_actions = stack(Axis::horizontal);
        location->set_automation_id(L"open-location"); location->set_enabled(false); location->on_click([this] { open_location(); });
        auto copy = std::make_shared<Button>(L"Copy details"); copy->set_automation_id(L"copy-details");
        copy->on_click([this] {
            if (!selected()) { action->set_text(L"Select a visible process first."); return; }
            window.copy_text(details_title->text() + L"\r\n" + details_identity->text() + L"\r\n" +
                details_metrics->text() + L"\r\n" + details_memory->text() + L"\r\n" + details_path->text());
        });
        auto back = std::make_shared<Button>(L"Back to processes"); back->set_automation_id(L"back-processes");
        back->on_click([this] { tabs->select(1); window.focus(*grid); });
        detail_actions->add(location); detail_actions->add(copy); detail->add(detail_actions); detail->add(back);
        auto safe = label(L"No debug privilege, elevation, or automatic process termination.", L"action-safety");
        safe->set_caption(true); safe->set_tone(TextTone::secondary); detail->add(safe);
        pages->add_page(std::make_shared<ScrollView>(detail, L"Selected process details"));
        root->add(pages, 1);
        status->set_caption(true); status->set_tone(TextTone::secondary);
        action->set_caption(true); action->set_tone(TextTone::secondary);
        root->add(status); root->add(action); window.set_content(root);
        window.on_key([this](const KeyEvent& event) {
            if (event.control && event.key == Key::f) { tabs->select(1); window.focus(*search, true); return true; }
            if (event.key == Key::f5) { task->refresh(); return true; }
            if (event.control && event.key == Key::p) { set_paused(!paused); return true; }
            if (event.control && event.key >= Key::digit1 && event.key <= Key::digit3) {
                tabs->select(1 + static_cast<unsigned>(event.key) - static_cast<unsigned>(Key::digit1)); return true;
            }
            return false;
        });
        task = window.create_sample_task([state = sampler](std::stop_token stop, bool reset) {
            return state->sample(stop, reset);
        }, [this](auto payload, const auto& error) { receive(std::static_pointer_cast<const Snapshot>(payload), error); });
    }
    const Process* selected() const {
        const auto row = view && grid->selected() ? view->find(*grid->selected()) : std::nullopt;
        return row ? &view->process(*row) : nullptr;
    }
    void rebuild() {
        view = std::make_shared<ProcessView>(snapshot, search->text(), grid->sort_column(), grid->descending());
        grid->set_source(view); update_details(); update_status();
    }
    void update_status() {
        if (!sampling_error.empty()) {
            status->set_text(L"Sampling failed: " + sampling_error); status->set_tone(TextTone::error); return;
        }
        if (!snapshot) return;
        wchar_t timing[40]{}; swprintf_s(timing, L"%.1f ms", snapshot->duration_ms);
        status->set_tone(snapshot->error.empty() ? TextTone::secondary : TextTone::error);
        status->set_text((paused ? L"Paused" : L"Live \u00b7 " + std::to_wstring(seconds) + L" s") +
            L"  \u00b7  " + std::to_wstring(view->size()) + L" shown  \u00b7  Sample " + std::to_wstring(samples) +
            L"  \u00b7  Collection " + timing + (snapshot->error.empty() ? L"" : L"  \u00b7  " + snapshot->error));
    }
    void update_details() {
        const auto* p = selected();
        end->set_enabled(p && can_end(*p, GetCurrentProcessId()));
        selected_path.clear(); location->set_enabled(false);
        if (!p) {
            selection->set_text(grid->selected() ? L"The selected process exited or is outside the current filter." : L"Select a process to inspect it.");
            details_title->set_text(L"No visible process selected"); details_identity->set_text(selection->text());
            details_metrics->set_text(L""); details_memory->set_text(L""); details_path->set_text(L""); details_availability->set_text(L"");
            return;
        }
        const auto name = p->name + L"  \u00b7  PID " + std::to_wstring(p->pid);
        selection->set_text(name + L"  \u00b7  " + percent(p->cpu) + L" CPU  \u00b7  " + mib(p->working_set));
        details_title->set_text(p->name);
        details_identity->set_text(L"PID " + std::to_wstring(p->pid) + L"  \u00b7  " + std::to_wstring(p->threads) +
            L" threads  \u00b7  " + (p->identity_known ? L"Creation time verified" : L"Identity unavailable; actions disabled"));
        details_metrics->set_text(L"CPU " + percent(p->cpu) + L"  \u00b7  I/O total " + view->text(*view->find(p->key), 5));
        details_memory->set_text(L"Working set " + mib(p->working_set) + L"  \u00b7  Private commit " + mib(p->private_commit));
        if (snapshot && snapshot->details_key == p->key) {
            details_path->set_text(snapshot->selected_path);
            if (!snapshot->selected_path.starts_with(L"Path unavailable:")) selected_path = snapshot->selected_path;
            details_identity->set_text(details_identity->text() + L"  \u00b7  " + snapshot->selected_architecture);
        } else details_path->set_text(p->identity_known ? L"Image metadata will arrive with the next sample." : L"Image path unavailable.");
        location->set_enabled(!selected_path.empty());
        details_availability->set_text(p->access_error ? L"Process counters: " + windows_error(p->access_error) :
            p->memory_error ? L"Memory counters: " + windows_error(p->memory_error) :
            p->io_error ? L"I/O counters: " + windows_error(p->io_error) :
            L"\u2014 means unavailable, a new process, or a reset sample baseline.");
    }
    void show_details() { update_details(); tabs->select(3); }
    void set_paused(bool value) {
        paused = value; task->pause(value); pause->set_name(paused ? L"Resume" : L"Pause");
        if (paused) status->set_text(L"Paused \u00b7 no scheduled sampling or rendering \u00b7 Refresh takes one snapshot");
        else {
            cpu_chart->append({}); memory_chart->append({});
            status->set_text(L"Resuming \u00b7 resetting CPU and I/O baselines...");
        }
    }
    void receive(std::shared_ptr<const Snapshot> value, const std::wstring& error) {
        if (!error.empty() || !value) { sampling_error = error.empty() ? L"No snapshot returned." : error; update_status(); return; }
        sampling_error.clear();
        snapshot = std::move(value); ++samples;
        cpu->set_text(percent(snapshot->cpu));
        memory->set_text(snapshot->memory_available ? percent(100.0 * (snapshot->ram_total - snapshot->ram_available) / snapshot->ram_total) : L"\u2014");
        counts->set_text(std::to_wstring(snapshot->processes.size()) + L" / " + std::to_wstring(snapshot->threads));
        performance_cpu->set_text(L"CPU " + percent(snapshot->cpu) + L"  \u00b7  " + std::to_wstring(snapshot->logical_processors) +
            L" logical processors" + (snapshot->processor_groups > 1 ? L"  \u00b7  system CPU covers the calling processor group" : L""));
        performance_memory->set_text(snapshot->memory_available ? L"Physical RAM: " + gib(snapshot->ram_total - snapshot->ram_available) +
            L" used  \u00b7  " + gib(snapshot->ram_available) + L" available  \u00b7  " + gib(snapshot->ram_total) + L" total" : L"Physical RAM unavailable");
        performance_commit->set_text(snapshot->commit_available ? L"System commit: " + gib(snapshot->commit_used) + L" / " + gib(snapshot->commit_limit) +
            L" limit (not resident RAM)" : L"System commit unavailable");
        cpu_chart->append(snapshot->cpu); cpu_chart->set_name(L"CPU " + percent(snapshot->cpu) + L" \u00b7 0\u2013100%");
        if (snapshot->memory_available && snapshot->ram_total) {
            memory_chart->set_scale(static_cast<double>(snapshot->ram_total));
            memory_chart->append(static_cast<double>(snapshot->ram_total - snapshot->ram_available));
            memory_chart->set_name(L"Physical RAM " + gib(snapshot->ram_total - snapshot->ram_available) + L" \u00b7 0\u2013" + gib(snapshot->ram_total));
        } else memory_chart->append({});
        rebuild();
        if (snapshot->action_sequence != action_sequence) {
            action_sequence = snapshot->action_sequence; action->set_text(snapshot->action_result);
        } else if (metadata_hint && snapshot->details_key == grid->selected() && !selected_path.empty()) {
            metadata_hint = false; action->set_text(L"Selected image metadata updated.");
        }
    }
    void end_selected() {
        const auto* p = selected();
        if (!p || !can_end(*p, GetCurrentProcessId())) { action->set_text(L"End task is unavailable for this selection."); return; }
        const auto key = p->key; const auto name = p->name;
        const bool confirmed = window.confirm(L"End task?", L"End " + name + L" (PID " + std::to_wstring(key.id) +
            L")?\n\nUnsaved work can be lost. The process identity and critical-process status will be checked before termination.");
        if (!confirmed) { action->set_text(L"End task cancelled."); return; }
        metadata_hint = false;
        sampler->request_end(key); task->refresh(); action->set_text(L"Checking process identity and requesting termination...");
    }
    void open_location() {
        if (selected_path.empty() || !selected()) return;
        const auto folder = std::filesystem::path(selected_path).parent_path().wstring();
        SHELLEXECUTEINFOW info{sizeof(info)};
        info.fMask = SEE_MASK_FLAG_NO_UI; info.lpVerb = L"open"; info.lpFile = folder.c_str(); info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) action->set_text(L"Cannot open file location: " + windows_error(GetLastError()));
        else action->set_text(L"Opened the selected image folder.");
    }
};
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    TaskManager app;
    return xui::Application::run(app.window);
}

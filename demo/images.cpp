#include "thumbnail_grid.hpp"
#include "branding.hpp"
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <cwctype>

namespace {
struct FolderState {
    std::mutex mutex;
    std::wstring path;
};
xui::SourceResult enumerate(const std::shared_ptr<FolderState>& state, const xui::CancelCheck& cancel) {
    std::wstring path;
    { std::lock_guard lock(state->mutex); path = state->path; }
    auto files = std::make_shared<std::vector<xui::FileItem>>();
    if (path.empty()) return {xui::FileSnapshot::build(files, cancel), L"Enter a folder path to load images."};
    std::error_code error;
    auto directory = std::filesystem::directory_iterator(path, error);
    if (error) throw std::runtime_error("Cannot open the image folder.");
    std::size_t path_units{};
    for (const auto& entry : directory) {
        if (cancel()) return {};
        const bool regular = entry.is_regular_file(error);
        if (error) throw std::runtime_error("Cannot read an image folder entry.");
        if (!regular) continue;
        auto extension = entry.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return std::towlower(c); });
        if (extension != L".png" && extension != L".jpg" && extension != L".jpeg" && extension != L".bmp" &&
            extension != L".gif" && extension != L".tif" && extension != L".tiff") continue;
        auto full_path = std::filesystem::absolute(entry.path(), error).wstring();
        if (error) throw std::runtime_error("Cannot resolve an image path.");
        path_units += full_path.size();
        if (files->size() >= 20000 || path_units > 4 * 1024 * 1024)
            throw std::runtime_error("The folder exceeds 20,000 images or 4 million path characters.");
        files->push_back({files->size() + 1, entry.path().filename().wstring(), std::move(full_path), false});
    }
    return {xui::FileSnapshot::build(std::move(files), cancel), {}};
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace xui;
    Window window({L"XUI Images", {900, 740}, ThemeMode::dark, {560, 420}});
    xui::demo::set_application_icon(window);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({24, 20, 24, 20});
    root->set_spacing(10);
    auto heading = std::make_shared<Label>(L"A closer look");
    heading->set_heading(true);
    root->add(heading);
    auto path = std::make_shared<TextInput>(L"Image folder");
    path->set_placeholder(L"Enter a folder, then choose Open folder");
    root->add(path);
    auto actions = std::make_shared<Stack>(Axis::horizontal);
    actions->set_spacing(8);
    auto open = std::make_shared<Button>(L"Open folder");
    auto clear = std::make_shared<Button>(L"Unload");
    auto stats = std::make_shared<Button>(L"Resource usage");
    actions->add(open); actions->add(clear); actions->add(stats);
    root->add(actions);
    auto status = std::make_shared<Label>(L"Enter a folder path to load images.");
    status->set_caption(true);
    root->add(status);
    auto grid = std::make_shared<sample::ThumbnailGrid>();
    auto scroll = std::make_shared<ScrollView>(grid, L"Image thumbnails");
    scroll->set_automation_id(L"image-scroll");
    scroll->set_maximum_size({10000, 960});
    grid->on_offset([weak = std::weak_ptr(scroll)] { auto value = weak.lock(); return value ? value->offset() : 0; });
    root->add(scroll, 1);
    auto help = std::make_shared<Label>(L"Tab to thumbnails. Use arrows, Page Up, Page Down, Home, End, or the mouse wheel.");
    help->set_caption(true);
    help->set_tone(TextTone::secondary);
    root->add(help);
    auto folder = std::make_shared<FolderState>();
    auto task = window.create_view_task([folder](const CancelCheck& cancel) { return enumerate(folder, cancel); },
        [grid, scroll, status](ViewResult result) {
            scroll->set_offset(0);
            grid->set_view(std::move(result.view));
            status->set_text(!result.error.empty() ? result.error : grid->count() ?
                std::to_wstring(grid->count()) + L" images. Only visible thumbnails decode." : L"This folder has no supported images.");
            ImageResources::clear_unused();
        });
    const auto load = [folder, path, task, grid, scroll, status] {
        { std::lock_guard lock(folder->mutex); folder->path = path->text(); }
        grid->set_view({});
        scroll->set_offset(0);
        status->set_text(L"Reading the folder...");
        task->request(L"", true);
    };
    open->on_click(load);
    path->on_submit(load);
    clear->on_click([folder, task, grid, scroll, status] {
        { std::lock_guard lock(folder->mutex); folder->path.clear(); }
        task->request(L"", true);
        grid->set_view({});
        scroll->set_offset(0);
        status->set_text(L"Images unloaded.");
        ImageResources::clear_unused();
    });
    stats->on_click([status] {
        ImageResources::clear_unused();
        const auto s = ImageResources::statistics();
        status->set_text(L"Pixels " + std::to_wstring(s.cpu_bytes / 1024) + L" KiB / 8192. Bitmaps " +
            std::to_wstring(s.gpu_bytes / 1024) + L" KiB / 8192. Decodes " + std::to_wstring(s.decoded) +
            L". Cache hits " + std::to_wstring(s.cache_hits) + L".");
    });
    window.on_key([&window](const KeyEvent& event) {
        if (event.key != Key::f6) return false;
        window.set_theme(window.theme() == ThemeMode::dark ? ThemeMode::light : ThemeMode::dark);
        return true;
    });
    int count{};
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args && count > 1) { path->set_text(args[1]); load(); }
    if (args) LocalFree(args);
    window.set_content(root);
    return Application::run(window);
}

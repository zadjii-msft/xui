#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/image.hpp"
#include "../src/drawing.hpp"
#include "image_fixtures.hpp"
#include "owned_window_capture.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <commctrl.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static void observe(void (*callback)(HWND)) { Drawing::present_observer_ = callback; }
    static void lose_native() { Drawing::native_result_override_ = D2DERR_RECREATE_TARGET; }
};
}
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Frame {
    int width{}, height{};
    std::vector<DWORD> pixels;
    explicit Frame(HWND window, bool owned_only = false) {
        if (owned_only) {
            auto captured = owned_window_capture::capture(window);
            width = captured.width; height = captured.height;
            pixels = std::move(captured.data);
            return;
        }
        RECT rect{};
        GetClientRect(window, &rect);
        POINT origin{};
        ClientToScreen(window, &origin);
        width = rect.right; height = rect.bottom;
        pixels.resize(static_cast<size_t>(width) * height);
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void* data{};
        HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        require(bitmap && dc, "Create frame capture");
        auto previous = SelectObject(dc, bitmap);
        const auto copied = BitBlt(dc, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT);
        GdiFlush();
        memcpy(pixels.data(), data, pixels.size() * sizeof(DWORD));
        SelectObject(dc, previous);
        DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
        require(copied != FALSE, "Capture presented desktop frame");
    }
    void save(const std::filesystem::path& path) const {
        std::ofstream file(path, std::ios::binary);
        BITMAPFILEHEADER header{0x4d42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) +
            sizeof(BITMAPINFOHEADER) + pixels.size() * 4), 0, 0,
            sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
        BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(&info), sizeof(info));
        file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size() * 4);
    }
};
struct Region { std::string name; RECT rect; std::vector<size_t> ink; };
std::vector<Region> regions;
std::filesystem::path output;
std::ofstream samples;
int frames{}, missing{};
UINT fixture_dpi{96};
int native_paints{}, native_text_sets{}, native_prints{};
std::map<std::string, int> missing_by_name;
std::string phase;
DWORD background(const Frame& frame, RECT rect) {
    std::map<DWORD, int> colors;
    for (int y = rect.top; y < rect.bottom; ++y)
        for (int x = rect.left; x < rect.right; ++x) ++colors[frame.pixels[y * frame.width + x] & 0xffffff];
    return std::max_element(colors.begin(), colors.end(), [](auto a, auto b) { return a.second < b.second; })->first;
}
int distance(DWORD a, DWORD b) {
    return abs(int(a & 255) - int(b & 255)) + abs(int((a >> 8) & 255) - int((b >> 8) & 255)) +
        abs(int((a >> 16) & 255) - int((b >> 16) & 255));
}
std::unique_ptr<Frame> reference;
void observe(HWND window) {
    // This deliberately exposes the interval before any post-EndDraw GDI repair.
    DwmFlush();
    Frame frame(window);
    ++frames;
    if (frames == 1) frame.save(output / (phase + "-presented.bmp"));
    for (const auto& region : regions) {
        size_t retained{};
        for (auto index : region.ink)
            retained += distance(frame.pixels[index], reference->pixels[index]) < 50;
        samples << phase << ',' << frames << ',' << region.name << ',' << retained << ',' << region.ink.size() << '\n';
        if (retained * 100 < region.ink.size() * 90) {
            ++missing;
            ++missing_by_name[region.name];
            if (missing < 8) {
                std::cout << phase << " frame=" << frames << " missing=" << region.name
                    << " retained=" << retained << "/" << region.ink.size() << '\n';
                frame.save(output / (phase + "-" + std::to_string(frames) + ".bmp"));
            }
        }
    }
}
HWND named(HWND root, const wchar_t* cls, const wchar_t* name) {
    struct Search { const wchar_t* cls; const wchar_t* name; HWND found{}; } search{cls, name};
    EnumChildWindows(root, [](HWND child, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t cls[80]{}, name[120]{};
        GetClassNameW(child, cls, 80); GetWindowTextW(child, name, 120);
        if (_wcsicmp(cls, search.cls) == 0 && std::wstring_view(name) == search.name) {
            search.found = child; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.found) std::wcerr << L"Missing " << cls << L": " << name << L" root=" << root << '\n';
    require(search.found != nullptr, "Find fixture control");
    return search.found;
}
void region(HWND root, HWND child, const char* name, bool custom = false) {
    RECT rect{};
    GetWindowRect(child, &rect);
    MapWindowPoints(nullptr, root, reinterpret_cast<POINT*>(&rect), 2);
    const auto scale = fixture_dpi / 96.0f;
    if (custom) {
        const auto inset = static_cast<LONG>(8 * scale);
        InflateRect(&rect, -inset, -inset);
    } else {
        rect.left += static_cast<int>(2 * scale);
        rect.right = std::min(rect.right - 2, rect.left + static_cast<LONG>(170 * scale));
        rect.bottom = std::min(rect.bottom - 1, rect.top + static_cast<LONG>(22 * scale));
    }
    for (auto parent = GetParent(child); parent; parent = parent == root ? nullptr : GetParent(parent)) {
        RECT clip{};
        GetClientRect(parent, &clip);
        MapWindowPoints(parent, root, reinterpret_cast<POINT*>(&clip), 2);
        require(IntersectRect(&rect, &rect, &clip) != FALSE, "Sampled glyph region is visible");
    }
    Region result{name, rect};
    auto bg = background(*reference, rect);
    for (int y = rect.top; y < rect.bottom; ++y)
        for (int x = rect.left; x < rect.right; ++x) {
            const size_t index = y * reference->width + x;
            if (distance(reference->pixels[index], bg) > 170) result.ink.push_back(index);
        }
    require(result.ink.size() > 30, "Reference region contains visible glyphs");
    regions.push_back(std::move(result));
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
LRESULT CALLBACK count_native(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_PAINT) ++native_paints;
    if (message == WM_SETTEXT) ++native_text_sets;
    if (message == WM_PRINTCLIENT) ++native_prints;
    return DefSubclassProc(hwnd, message, wp, lp);
}
struct DeferredRootPaint {
    HWND window;
    explicit DeferredRootPaint(HWND hwnd) : window(hwnd) {
        require(SetWindowSubclass(window, procedure, 78, 0) != FALSE, "Defer root painting during layout capture");
    }
    ~DeferredRootPaint() { finish(); }
    void finish() {
        if (!window) return;
        RemoveWindowSubclass(window, procedure, 78);
        InvalidateRect(window, nullptr, FALSE);
        window = nullptr;
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
        if (message == WM_PAINT) { ValidateRect(hwnd, nullptr); return 0; }
        return DefSubclassProc(hwnd, message, wp, lp);
    }
};
void scroll_frames(xui::VisualStyle style, bool native, bool images) {
    xui::Window window({L"XUI scroll frame boundary", {620, 580}});
    window.set_visual_style(style);
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto outside = std::make_shared<xui::Button>(L"Unscrolled focus target");
    root->add(outside);
    auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
    content->set_spacing(7);
    auto input = std::make_shared<xui::TextInput>(L"Native scroll caption");
    input->set_text(L"Native scroll value");
    auto document = std::make_shared<xui::MultilineText>();
    document->set_text(L"Native document\nSecond line");
    document->set_preferred_size({300, 70});
    for (int i = 0; i < 16; ++i) {
        auto label = std::make_shared<xui::Label>(L"Label " + std::to_wstring(i) + L" - unchanged frame pixels");
        label->set_preferred_size({300, 28});
        content->add(label);
        content->add(std::make_shared<xui::Button>(L"Action " + std::to_wstring(i)));
        if (native && i == 1) { content->add(input); content->add(document); }
    }
    if (images) {
        auto image = std::make_shared<xui::Image>();
        image->set_preferred_size({300, 60});
        content->add(image);
    }
    auto scroll = std::make_shared<xui::ScrollView>(content, L"Scroll frame viewport");
    root->add(scroll, 1);
    root->add(std::make_shared<xui::Label>(L"Unscrolled lower boundary"));
    window.set_content(root);
    bool finished{};
    window.post([&] {
        HWND host{};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM data) -> BOOL {
            wchar_t title[80]{};
            GetWindowTextW(hwnd, title, 80);
            if (std::wstring_view(title) == L"XUI scroll frame boundary") {
                *reinterpret_cast<HWND*>(data) = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&host));
        require(host != nullptr, "Find scroll frame window");
        require(window.focus(*outside), "Keep native caret out of scroll samples");
        for (auto dpi : {96u, 144u, 192u}) {
            RECT suggested{40, 40, 740, 740};
            SendMessageW(host, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
            for (auto theme : {xui::ThemeMode::dark, xui::ThemeMode::light, xui::ThemeMode::high_contrast}) {
                window.set_theme(theme);
                scroll->set_offset(0);
                pump();
                RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                DwmFlush();
                const auto viewport = scroll->bounds();
                const auto scale = dpi / 96.0f;
                const RECT sampled{static_cast<LONG>(std::lround(viewport.x * scale)),
                    static_cast<LONG>(std::lround(viewport.y * scale)),
                    static_cast<LONG>(std::lround((viewport.x + viewport.width) * scale)),
                    static_cast<LONG>(std::lround((viewport.y + viewport.height) * scale))};
                const auto difference = [&](const Frame& a, const Frame& b) {
                    require(a.width == b.width && a.height == b.height &&
                        sampled.left >= 0 && sampled.top >= 0 && sampled.right <= a.width && sampled.bottom <= a.height,
                        "Scroll capture geometry remains inside the client area");
                    size_t changed{};
                    for (int y = sampled.top; y < sampled.bottom; ++y)
                        for (int x = sampled.left; x < sampled.right; ++x)
                            changed += distance(a.pixels[y * a.width + x], b.pixels[y * b.width + x]) > 10;
                    return changed;
                };
                for (const auto offset : {13.5f, 47.0f, 131.5f, 6.0f, 0.0f}) {
                    const auto prefix = std::to_string(native) + "-" + std::to_string(images) + "-" +
                        std::to_string(dpi) + "-" + std::to_string(static_cast<int>(theme)) + "-" + std::to_string(offset);
                    const Frame previous(host, true);
                    // Graphics Capture can dispatch messages through COM. Hold only the
                    // root paint so capture cannot hide intermediate child pixel copies.
                    DeferredRootPaint deferred(host);
                    const auto presentations = SendMessageW(host, WM_APP + 60, 0, 0);
                    scroll->set_offset(offset);
                    // Run layout, but deliberately leave WM_PAINT queued. The window must
                    // retain the previous complete frame, not copied pieces of moved HWNDs.
                    SendMessageW(host, WM_APP + 12, 0, 0);
                    require(SendMessageW(host, WM_APP + 60, 0, 0) == presentations,
                        "Scroll layout does not present a partial root frame");
                    DwmFlush();
                    const Frame between(host, true);
                    require(SendMessageW(host, WM_APP + 60, 0, 0) == presentations,
                        "Layout capture does not dispatch a root presentation");
                    const auto changed = difference(previous, between);
                    if (changed) {
                        previous.save(output / (prefix + "-before-scroll.bmp"));
                        between.save(output / (prefix + "-during-scroll.bmp"));
                        std::cerr << "Scroll changed " << changed << " pixels before root presentation: " << prefix << '\n';
                    }
                    require(changed == 0, "Layout preserves the previous complete frame until root presentation");
                    deferred.finish();
                    UpdateWindow(host);
                    DwmFlush();
                    const Frame presented(host, true);
                    require(difference(previous, presented) > 100, "Root presentation draws the new scroll position");
                    RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                    DwmFlush();
                    const Frame settled(host, true);
                    const auto repaired = difference(presented, settled);
                    if (repaired) {
                        presented.save(output / (prefix + "-presented.bmp"));
                        settled.save(output / (prefix + "-settled.bmp"));
                        std::cerr << "Scroll repaired " << repaired << " pixels after root presentation: " << prefix << '\n';
                    }
                    require(repaired == 0, "First scroll frame matches the settled native and custom pixels");
                }
            }
        }
        if (native) {
            require(window.focus(*input), "Scrolled native input remains focusable");
            const auto edit = GetFocus();
            SendMessageW(edit, EM_SETSEL, 0, -1);
            SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited after scrolling"));
            require(input->text() == L"Edited after scrolling", "Scrolled input retains native editing");
            SendMessageW(edit, EM_UNDO, 0, 0);
            require(input->text() == L"Native scroll value", "Scrolled input retains native undo");
        }
        finished = true;
        window.close();
    });
    const auto result = xui::Application::run(window);
    if (!window.error().empty()) std::wcerr << window.error() << '\n';
    require(result == 0 && finished, "Scroll frame experiment completed");
    require(xui::Drawing::live_targets() == 0, "Scroll fixture releases its root target");
}
void run(xui::VisualStyle style) {
    xui::Window window({L"XUI frame boundary test", {720, 620}});
    window.set_visual_style(style);
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto filled = std::make_shared<xui::TextInput>(L"Stable native caption");
    filled->set_text(L"Stable native value");
    auto empty = std::make_shared<xui::TextInput>(L"Empty native caption");
    empty->set_placeholder(L"Stable placeholder");
    auto label = std::make_shared<xui::Label>(L"Stable custom label");
    auto button = std::make_shared<xui::Button>(L"Stable custom button");
    button->set_icon(xui::ButtonIcon::refresh);
    auto status = std::make_shared<xui::Label>(L"Status");
    auto hover = std::make_shared<xui::Button>(L"Changing hover");
    auto thumbnails = std::make_shared<xui::FileList>();
    thumbnails->set_preferred_size({320, 40});
    thumbnails->set_thumbnails(true);
    content->add(filled); content->add(empty); content->add(label); content->add(button);
    // More content than the viewport, with a native field on either side of its lower edge.
    auto clipped = std::make_shared<xui::TextInput>(L"Clipped native caption");
    clipped->set_text(L"Clipped native value");
    content->add(clipped);
    auto view = std::make_shared<xui::ScrollView>(content);
    auto right = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto search = std::make_shared<xui::TextInput>(L"Hidden search caption");
    search->set_search_style(true);
    search->set_placeholder(L"Search placeholder");
    right->add(search);
    auto compact = std::make_shared<xui::TextInput>(L"Hidden address caption");
    compact->set_text(L"Compact native address");
    compact->set_caption_visible(false);
    compact->set_preferred_size({0, 40});
    right->add(compact);
    auto split = std::make_shared<xui::SplitView>(view, right);
    split->set_preferred_size({640, 220});
    auto boundary = std::make_shared<xui::Label>(L"Stable text below clipped viewport");
    root->add(split); root->add(boundary); root->add(status); root->add(hover); root->add(thumbnails);
    window.set_content(root);
    std::atomic<bool> finished{};
    std::atomic<HWND> fixture{};
    window.on_key([&](const xui::KeyEvent& event) {
        if (event.key != xui::Key::f1) return false;
        HWND host = fixture.load();
        SetWindowPos(host, HWND_TOPMOST, 40, 40, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        window.focus(*button);
        EnumChildWindows(host, [](HWND child, LPARAM) -> BOOL {
            wchar_t cls[80]{}; GetClassNameW(child, cls, 80);
            if (!_wcsicmp(cls, L"EDIT") || !_wcsicmp(cls, L"STATIC"))
                require(SetWindowSubclass(child, count_native, 77, 0) != FALSE, "Attach native paint counter");
            return TRUE;
        }, 0);
        for (auto dpi : {96u, 144u, 192u}) {
        fixture_dpi = dpi;
        RECT suggested{40, 40, 920, 920};
        SendMessageW(host, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        for (auto theme : {xui::ThemeMode::dark, xui::ThemeMode::light, xui::ThemeMode::high_contrast}) {
            split->set_secondary_visible(theme != xui::ThemeMode::light);
            window.set_theme(theme);
            filled->set_text(L"Stable native value " + std::to_wstring(dpi));
            filled->set_name(L"Stable native caption " + std::to_wstring(dpi));
            compact->set_caption_visible(true);
            pump();
            compact->set_caption_visible(false);
            // Flush the queued model update before fixing the reference.
            pump();
            view->set_offset(theme == xui::ThemeMode::high_contrast ? 6.0f : 0.0f);
            pump();
            RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            DwmFlush();
            reference = std::make_unique<Frame>(host);
            phase = std::to_string(dpi) + "-" + std::to_string(static_cast<int>(theme));
            const auto thumbnail = std::filesystem::absolute(output / (phase + ".png"));
            image_fixture::png(thumbnail, 96, 96, 0xff4080b0);
            reference->save(output / (phase + "-reference.bmp"));
            regions.clear();
            region(host, named(host, L"STATIC", filled->name().c_str()), "caption");
            region(host, named(host, L"EDIT", filled->text().c_str()), "edit");
            region(host, named(host, L"EDIT", L""), "placeholder");
            region(host, named(host, L"Xui.Control.1", L"Stable custom label"), "label");
            region(host, named(host, L"Xui.Control.1", L"Stable custom button"), "button", true);
            if (style == xui::VisualStyle::winui && theme != xui::ThemeMode::high_contrast) {
                for (const auto index : regions.back().ink) {
                    const auto pixel = reference->pixels[index];
                    const auto red = static_cast<int>((pixel >> 16) & 255);
                    const auto green = static_cast<int>((pixel >> 8) & 255);
                    const auto blue = static_cast<int>(pixel & 255);
                    require(std::abs(red - green) <= 1 && std::abs(green - blue) <= 1,
                        "Monochrome Fluent glyphs have no ClearType color fringes");
                }
            }
            region(host, named(host, L"Xui.Control.1", boundary->name().c_str()), "viewport-boundary");
            require(!IsWindowVisible(named(host, L"STATIC", L"Hidden search caption")),
                "Search caption stays hidden");
            require(!IsWindowVisible(named(host, L"STATIC", L"Hidden address caption")),
                "Compact address caption stays hidden after a visible-caption transition");
            if (split->expanded()) {
                const auto parent = GetParent(named(host, L"STATIC", L"Hidden search caption"));
                region(host, FindWindowExW(parent, nullptr, L"EDIT", nullptr), "right-placeholder");
                region(host, named(host, L"EDIT", L"Compact native address"), "compact-address");
            }
            const auto edits_before = native_paints, text_sets_before = native_text_sets;
            xui::DrawingTestAccess::observe(observe);
            auto rows = std::make_shared<std::vector<xui::FileItem>>();
            rows->push_back({1, L"New thumbnail", thumbnail.wstring(), false});
            thumbnails->set_items(rows);
            for (int i = 0; i < 24; ++i) {
                status->set_name(L"Unrelated status " + std::to_wstring(i));
                const auto changing = named(host, L"Xui.Control.1", L"Changing hover");
                SendMessageW(changing, i % 2 ? WM_MOUSELEAVE : WM_MOUSEMOVE, 0, MAKELPARAM(4, 4));
                pump();
                RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
            }
            require(native_paints == edits_before && native_text_sets == text_sets_before,
                "Unrelated updates do not repaint or reset native controls");
            require(SendMessageW(host, WM_APP + 60, 23, 0) == 1,
                "An asynchronous thumbnail arrived during frame observation");
            const auto before_loss = frames;
            xui::DrawingTestAccess::lose_native();
            RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
            require(frames == before_loss, "Device loss does not publish an incomplete frame");
            pump();
            require(frames > before_loss && xui::Drawing::live_targets() == 1,
                "Device loss recreates exactly one complete target");
            xui::DrawingTestAccess::observe(nullptr);
            const auto idle = SendMessageW(host, WM_APP + 60, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            pump();
            require(SendMessageW(host, WM_APP + 60, 0, 0) == idle, "No idle repaint timer");
        }
        }
        window.focus(*filled);
        const auto edit = named(host, L"EDIT", filled->text().c_str());
        SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        pump();
        RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        HideCaret(edit); ShowCaret(edit);
        DwmFlush();
        GUITHREADINFO gui{sizeof(gui)};
        require(GetGUIThreadInfo(GetCurrentThreadId(), &gui) && gui.hwndCaret == edit,
            "Native EDIT still owns the caret");
        RECT caret = gui.rcCaret;
        MapWindowPoints(edit, host, reinterpret_cast<POINT*>(&caret), 2);
        Frame caret_before(host);
        RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
        DwmFlush();
        Frame caret_after(host);
        size_t caret_changes{}, caret_ink{};
        const auto caret_background = caret_before.pixels[
            ((caret.top + caret.bottom) / 2) * caret_before.width + caret.right + 5];
        for (int y = caret.top; y < caret.bottom; ++y)
            for (int x = caret.left; x < caret.right; ++x) {
                const auto index = y * caret_before.width + x;
                caret_ink += distance(caret_before.pixels[index], caret_background) > 50;
                caret_changes += distance(caret_before.pixels[index], caret_after.pixels[index]) > 50;
            }
        std::cout << "caret_ink=" << caret_ink << " caret_pixels_changed=" << caret_changes << '\n';
        require(caret_ink > 0, "Reference contains the visible native caret");
        require(caret_changes == 0, "Root presentation preserves the visible native caret");
        finished = true;
        window.close();
        return true;
    });
    std::jthread driver([&] {
        HWND host{};
        for (int i = 0; i < 500 && !host; ++i) {
            EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
                DWORD process{}; GetWindowThreadProcessId(hwnd, &process);
                wchar_t title[80]{}; GetWindowTextW(hwnd, title, 80);
                if (process == GetCurrentProcessId() && std::wstring_view(title) == L"XUI frame boundary test") {
                    *reinterpret_cast<HWND*>(data) = hwnd; return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&host));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        fixture = host;
        if (host) PostMessageW(host, WM_KEYDOWN, VK_F1, 0);
    });
    const int result = xui::Application::run(window);
    if (!window.error().empty()) std::wcerr << window.error() << '\n';
    require(result == 0 && window.error().empty(), "Window completes without a rendering error");
    require(finished, "Frame experiment completed");
}
}
int main(int argc, char** argv) {
    try {
        const std::filesystem::path root = argc > 1 ? argv[1] : "build\\flicker\\frames";
        const bool scroll_only = argc > 2 && std::string_view(argv[2]) == "--scroll";
        const auto style = (argc > 2 && std::string_view(argv[2]) == "--winui") ||
            (argc > 3 && std::string_view(argv[3]) == "--winui") ?
            xui::VisualStyle::winui : xui::VisualStyle::classic;
        std::filesystem::create_directories(root);
        if (scroll_only) {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            struct Apartment {
                ~Apartment() { winrt::clear_factory_cache(); winrt::uninit_apartment(); }
            } apartment;
            output = root;
            scroll_frames(style, false, false);
            scroll_frames(style, true, false);
            scroll_frames(style, true, true);
            std::cout << "Scroll layout preserves complete frames with custom, native, and image peers\n";
            return 0;
        }
        samples.open(root / "samples.csv");
        samples << "phase,frame,region,retained,reference\n";
        DWORD warm_handles{}, warm_gdi{}, warm_user{};
        for (int cycle = 0; cycle < 3; ++cycle) {
            output = root / std::to_string(cycle);
            std::filesystem::create_directories(output);
            run(style);
            require(xui::Drawing::live_targets() == 0, "Closing releases the native-compatible target");
            DWORD handles{};
            GetProcessHandleCount(GetCurrentProcess(), &handles);
            const auto gdi = GetGuiResources(GetCurrentProcess(), 0);
            const auto user = GetGuiResources(GetCurrentProcess(), 1);
            PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            std::cout << "closed_cycle=" << cycle << " handles=" << handles << " gdi=" << gdi
                << " user=" << user << " private_bytes=" << memory.PrivateUsage
                << " working_set=" << memory.WorkingSetSize << '\n';
            if (!cycle) { warm_handles = handles; warm_gdi = gdi; warm_user = user; }
            else require(handles <= warm_handles + 4 && gdi <= warm_gdi + 2 && user <= warm_user + 2,
                "Repeated create/close does not retain native drawing resources");
        }
        std::cout << "presented_frames=" << frames << " missing_glyph_regions=" << missing << '\n';
        for (const auto& [name, count] : missing_by_name) std::cout << name << "=" << count << '\n';
        std::cout << "native_paints=" << native_paints << " native_text_sets=" << native_text_sets
            << " native_prints=" << native_prints << '\n';
        require(missing == 0, "Every presented frame preserves all stable text");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

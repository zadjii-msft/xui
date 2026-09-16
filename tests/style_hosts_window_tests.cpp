#include "xui/application.hpp"
#include "xui/image.hpp"
#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include "../src/drawing.hpp"
#include "owned_window_capture.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static std::size_t rounded_clip_count(const Drawing& drawing) { return drawing.rounded_clips_.size(); }
};
}
namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr uint32_t fill = 0x24364a, border = 0xd83b71, ink = 0x43e8bd, highlight = 0xe7c841;
PartStyleValues surface() {
    PartStyleValues value;
    value.background = ThemeColor{fill, 0x10202f}; value.foreground = ThemeColor{ink};
    value.border_brush = ThemeColor{border};
    value.border_thickness = Insets{3, 3, 3, 3}; value.padding = Insets{9, 9, 9, 9};
    value.corner_radius = 0.0f;
    return value;
}
std::size_t count(const owned_window_capture::Pixels& pixels, Rect bounds, float scale, uint32_t color) {
    const int left = std::clamp(static_cast<int>(bounds.x * scale), 0, pixels.width);
    const int top = std::clamp(static_cast<int>(bounds.y * scale), 0, pixels.height);
    const int right = std::clamp(static_cast<int>((bounds.x + bounds.width) * scale), left, pixels.width);
    const int bottom = std::clamp(static_cast<int>((bounds.y + bounds.height) * scale), top, pixels.height);
    std::size_t result{};
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
        const auto value = pixels.data[static_cast<std::size_t>(y) * pixels.width + x];
        bool match = true;
        for (int shift : {0, 8, 16})
            match &= std::abs(int((value >> shift) & 255) - int((color >> shift) & 255)) <= 5;
        result += match;
    }
    return result;
}
void flush(HWND hwnd) {
    SendMessageW(hwnd, WM_APP + 12, 0, 0);
    InvalidateRect(hwnd, nullptr, FALSE); UpdateWindow(hwnd);
}
void clip_cache_contract() {
    const auto hwnd = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Owned host clip cache",
        WS_POPUP, 0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(hwnd != nullptr, "Create hidden owned clip fixture");
    struct Destroy { HWND window; ~Destroy() { DestroyWindow(window); } } destroy{hwnd};
    Drawing drawing;
    drawing.initialize();
    require(drawing.begin(hwnd, 96, D2D1::ColorF(0)), "Begin hidden clip cache fixture");
    for (unsigned i = 0; i < 32; ++i) {
        require(drawing.push_rounded_clip({static_cast<float>(i), static_cast<float>(i), 64, 64}, 8),
            "Positive rounded clip pushes a mask");
        drawing.pop_rounded_clip();
    }
    require(DrawingTestAccess::rounded_clip_count(drawing) == 1, "Moving the same frame reuses its local-origin clip geometry");
    require(!drawing.push_rounded_clip({0, 0, 64, 64}, 0), "Square frames do not allocate or push rounded geometry");
    for (unsigned i = 1; i <= 20; ++i) {
        drawing.push_rounded_clip({0, 0, 64, 64}, static_cast<float>(i));
        drawing.pop_rounded_clip();
    }
    require(DrawingTestAccess::rounded_clip_count(drawing) == 8, "Rounded clip geometry storage stays bounded");
    require(drawing.end(), "Complete hidden clip cache fixture");
    drawing.discard();
    require(DrawingTestAccess::rounded_clip_count(drawing) == 0, "Target discard releases owned clip geometry");
}
}
int main() {
    try {
        clip_cache_contract();
        Window window({L"XUI host style owned rendering", {520, 760}, ThemeMode::light});
        auto root = std::make_shared<Stack>(Axis::vertical);
        root->set_spacing(6); root->set_padding({8, 8, 8, 8});
        auto image = std::make_shared<Image>();
        auto vector = std::make_shared<VectorCanvas>();
        auto map = std::make_shared<MapView>();
        auto media = std::make_shared<MediaPlayback>();
        auto web = std::make_shared<WebContent>();
        const std::shared_ptr<Control> controls[]{image, vector, map, media, web};
        const StyleTarget targets[]{StyleTarget::image, StyleTarget::vector_canvas, StyleTarget::map_view,
            StyleTarget::media_playback, StyleTarget::web_content};
        for (unsigned i = 0; i < 5; ++i) {
            controls[i]->set_fixed_size({480, i < 3 ? 118.0f : 154.0f});
            controls[i]->set_control_style(ControlStyle::create(targets[i], {{StylePart::root, surface()}}, {}));
            root->add(controls[i]);
        }
        PartStyleValues selected; selected.foreground = ThemeColor{highlight};
        vector->set_control_style_values(StylePart::selection, selected);
        auto shape = VectorShape::rectangle(19, {30, 10, 90, 30});
        shape.fill = {0, 0, 1, 1}; shape.stroke = {0, 0, 1, 1};
        shape.interactive = true; shape.name = L"Owned blue shape";
        const auto scene = std::make_shared<const VectorScene>(std::vector<VectorShape>{shape});
        vector->set_scene(scene); vector->set_selected(19);
        const auto request = map->request_overlay();
        map->complete(request, {}, L"Owned provider failure");
        media->publish_state(HostState::error, L"Owned inactive media failure");
        web->publish_state(HostState::stopped, L"Owned stopped browser state");
        window.set_content(root);
        std::string error;
        std::jthread worker([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            window.post([&] {
                try {
                    const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI host style owned rendering");
                    require(hwnd != nullptr, "Owned render window exists");
                    const float scale = GetDpiForWindow(hwnd) / 96.0f;
                    const auto media_revision = media->revision(), web_revision = web->revision();
                    const auto semantic_id = vector->accessible_items()->id();
                    flush(hwnd);
                    auto pixels = owned_window_capture::capture(hwnd);
                    for (auto& control : controls) {
                        require(count(pixels, control->bounds(), scale, fill) > 150, "Actual host branch paints the authored frame fill");
                        require(count(pixels, control->bounds(), scale, border) > 60, "Actual host branch paints the authored border");
                    }
                    for (auto& control : {std::static_pointer_cast<Control>(image), std::static_pointer_cast<Control>(map),
                        std::static_pointer_cast<Control>(media), std::static_pointer_cast<Control>(web)})
                        require(count(pixels, control->bounds(), scale, ink) > 8, "Placeholder, coordinate, error, or status text uses authored foreground");
                    require(count(pixels, media->status()->bounds(), scale, ink) > 8 &&
                        count(pixels, web->status()->bounds(), scale, ink) > 8,
                        "Retained unstyled status children use the parent host error/status text defaults");
                    PartStyleValues child_text; child_text.foreground = ThemeColor{0xee7755};
                    media->status()->set_control_style_values(StylePart::message, child_text);
                    flush(hwnd); pixels = owned_window_capture::capture(hwnd);
                    require(count(pixels, media->status()->bounds(), scale, 0xee7755) > 8,
                        "Explicit child message styling wins over the parent host error defaults");
                    media->status()->set_control_style_values(StylePart::message, {});
                    flush(hwnd); pixels = owned_window_capture::capture(hwnd);
                    require(count(pixels, vector->bounds(), scale, 0x0000ff) > 100, "Scene data remains authored blue");
                    require(count(pixels, vector->bounds(), scale, highlight) > 25, "Scene selection uses its owned highlight color");
                    window.set_theme(ThemeMode::dark); flush(hwnd);
                    pixels = owned_window_capture::capture(hwnd);
                    require(count(pixels, image->bounds(), scale, 0x10202f) > 100, "Host frame resolves dark theme resources");
                    vector->set_scene({}); flush(hwnd);
                    pixels = owned_window_capture::capture(hwnd);
                    require(count(pixels, vector->bounds(), scale, ink) > 8, "Actual empty scene text uses its style");
                    auto corner_shape = VectorShape::rectangle(20, {0, 0, 480, 118});
                    corner_shape.fill = {0, 0, 1, 1}; corner_shape.stroke_width = 0;
                    vector->set_scene(std::make_shared<const VectorScene>(std::vector<VectorShape>{corner_shape}));
                    PartStyleValues rounded; rounded.padding = Insets{}; rounded.border_thickness = Insets{}; rounded.corner_radius = 32.0f;
                    vector->set_control_style_values(StylePart::root, rounded);
                    flush(hwnd); pixels = owned_window_capture::capture(hwnd);
                    const auto rounded_bounds = vector->bounds();
                    require(count(pixels, {rounded_bounds.x + 1, rounded_bounds.y + 1, 5, 5}, scale, 0x0000ff) == 0,
                        "Rounded owned clip removes scene pixels outside the frame even with zero padding");
                    require(count(pixels, {rounded_bounds.x + 40, rounded_bounds.y + 40, 20, 20}, scale, 0x0000ff) > 100,
                        "Rounded frame clipping does not recolor scene interior pixels");
                    vector->set_control_style_values(StylePart::root, {});
                    vector->set_scene(scene); vector->set_selected(19);
                    for (auto& control : controls) control->set_control_style(nullptr);
                    vector->set_control_style_values(StylePart::selection, {});
                    flush(hwnd);
                    require(vector->scene() == scene && vector->accessible_items()->id() == semantic_id,
                        "Style clearing preserves retained scene and semantic child identity");
                    require(media->revision() == media_revision && web->revision() == web_revision &&
                        media->source().empty() && !web->has_source(), "Native pixel checks never activate media or web runtimes");
                    require(ImageResources::statistics().decoded == 0, "Empty image rendering never starts decode");
                } catch (const std::exception& failure) { error = failure.what(); }
                window.close();
            });
        });
        const auto result = Application::run(window);
        require(result == 0 && error.empty(), error.empty() ? "Owned host render loop succeeds" : error.c_str());
        std::cout << "Actual host frame, text, selection, empty-state, theme, and opt-in rendering passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include "image_memory_fixture.hpp"
#include "xui/reveal.hpp"
#include <dwmapi.h>

namespace image_memory_window {
using namespace xui;
struct Fixture {
    Window window{{L"Owned memory image window", {500, 400}, ThemeMode::light}};
    std::shared_ptr<Stack> root = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<ContentHost> host = std::make_shared<ContentHost>();
    std::shared_ptr<Stack> content = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<Image> image = std::make_shared<Image>(L"Visible only after Ready");
    std::shared_ptr<Image> deferred = std::make_shared<Image>(L"Deferred but ready image");
    std::shared_ptr<Reveal> reveal;
    HWND hwnd{}, picture{};
    unsigned ready{}, deferred_ready{}, failures{};
    bool advanced{}, done{}, in_native{};
    Rect before{};
    static ImageMemoryPlan plan(uint64_t generation, bool reduced = false) {
        return {generation, ImageMemoryFormat::png, {3, 2}, reduced ? ImageSize{2, 1} : ImageSize{3, 2},
            reduced ? ImageSize{2, 2} : ImageSize{64, 64}};
    }
    Fixture() {
        image->cancel_memory(); deferred->cancel_memory();
        const auto desired = image->measure({1000, 1000});
        check(desired.width == 192 && desired.height == 144, "Unsized memory image uses logical default viewport");
        image->set_visible(false);
        image->set_axis_constraints(AxisConstraints{120.0f}, AxisConstraints{120.0f});
        PartStyleValues style; style.padding = Insets{}; style.border_thickness = Insets{};
        style.background = ThemeColor{0x112233}; style.corner_radius = 0.0f;
        image->set_control_style_values(StylePart::root, style);
        reveal = std::make_shared<Reveal>(deferred);
        window.apply_portable_reveal(*reveal, false, 0, RevealDirection::bottom, true);
        content->add(image); content->add(reveal);
        root->add(host, 1); window.set_content(root); window.replace_content(*host, content);
        image->on_memory_state([this] {
            const auto state = image->memory_state();
            check(window.post([this, state] { notice(state); }), "Post immutable native image result");
        });
        deferred->on_memory_state([this] {
            const auto state = deferred->memory_state();
            check(window.post([this, state] {
                check(state.state == ImageStatus::ready && !reveal->open(), "Closed deferred subtree can become Ready");
                ++deferred_ready; proceed();
            }), "Post deferred image result");
        });
        image->set_memory_source(image_memory_fixture::bytes(), plan(1));
        deferred->set_memory_source(image_memory_fixture::bytes(), plan(1));
    }
    void flush() { SendMessageW(hwnd, WM_APP + 12, 0, 0); UpdateWindow(hwnd); DwmFlush(); }
    static HWND find_picture(HWND owner) {
        HWND result{};
        EnumChildWindows(owner, [](HWND hwnd, LPARAM data) -> BOOL {
            wchar_t name[128]{}; GetWindowTextW(hwnd, name, 128);
            if (std::wstring_view(name) == L"Visible only after Ready") {
                *reinterpret_cast<HWND*>(data) = hwnd; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&result));
        check(result != nullptr, "Find owned Image HWND"); return result;
    }
    void proceed() {
        if (advanced || ready != 1 || deferred_ready != 1) return;
        advanced = true;
        hwnd = FindWindowW(L"Xui.Window.1", L"Owned memory image window"); check(hwnd != nullptr, "Find owned memory window");
        picture = find_picture(hwnd);
        check(!image->visible() && !IsWindowVisible(picture) && image->memory_pixels(),
            "Image readiness and assigned native pixels do not require child visibility");
        image->set_visible(true); flush();
        before = image->bounds(); check(before.width == 120 && before.height == 120, "Authored axes control logical size");
        image->set_memory_source(image_memory_fixture::bytes(), plan(2, true));
        window.refresh_image_memory();
    }
    void pixels() {
        flush();
        const auto bounds = image->bounds();
        check(bounds.x == before.x && bounds.y == before.y && bounds.width == before.width && bounds.height == before.height,
            "Decode quality changes retain actual native image bounds");
        const auto scale = GetDpiForWindow(hwnd) / 96.0f;
        const auto dc = GetDC(hwnd); check(dc != nullptr, "Read owned image client pixels");
        const auto pixel = [&](float y) {
            return GetPixel(dc, static_cast<int>(std::lround((bounds.x + 60) * scale)),
                static_cast<int>(std::lround((bounds.y + y) * scale)));
        };
        const auto top = pixel(10), inside = pixel(25), bottom = pixel(110);
        ReleaseDC(hwnd, dc);
        if (top != RGB(0x11, 0x22, 0x33) || inside == top || bottom != top)
            std::cout << "Image contain pixels top=" << top << " inside=" << inside << " bottom=" << bottom << '\n';
        check(top == RGB(0x11, 0x22, 0x33) && bottom == top && inside != top && inside != CLR_INVALID,
            "Actual contain uses original 3:2 source aspect, not the rounded 2:1 decoded thumbnail");
    }
    void notice(ImageMemoryState state) {
        check(!in_native, "Authored image results run after native upload/refresh unwinds");
        if (state.presentation_failed) {
            ++failures;
            check(window.state() == WindowState::open && image->memory_state().presentation_failed,
                "Post-Ready presentation failure is reported before owner teardown");
            window.replace_content(*host, {});
            window.refresh_image_memory();
            const auto stats = ImageResources::statistics();
            check(!stats.cpu_bytes && !stats.cpu_reserved && !stats.gpu_bytes && !stats.gpu_reserved && !stats.encoded_bytes,
                "Fatal owner cleanup retires CPU/GPU/encoded ownership");
            check(!IsWindow(picture) && ready == 2 && deferred_ready == 1 && failures == 1,
                "Fatal completion is distinct from initial decode errors and destroys the owned native peer");
            done = true; window.close(); return;
        }
        check(state.state == ImageStatus::ready, "Valid memory image reaches Ready");
        ++ready;
        if (state.generation == 1) { proceed(); return; }
        check(state.generation == 2 && state.pixels == ImageSize{2, 1} && state.source == ImageSize{3, 2}, "Native completion preserves source and output dimensions");
        pixels();
        image->set_visible(false); flush();
        check(image->status() == ImageStatus::ready && image->memory_pixels(), "Hidden Ready images retain owned decoded presentation");
        const auto stats = ImageResources::statistics();
        SendMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0); window.refresh_image_memory();
        check(image->status() == ImageStatus::ready && ImageResources::statistics().decoded == stats.decoded &&
            ImageResources::statistics().uploaded > stats.uploaded && ready == 2,
            "Target recreation reuploads retained CPU pixels without redecode, generation change, or duplicate Ready");
        DrawingTestAccess::fail_image_upload();
        in_native = true;
        SendMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0); window.refresh_image_memory();
        in_native = false;
        check(window.state() == WindowState::open && image->memory_state().presentation_failed,
            "Native presentation failure keeps the callback transport alive for posted host teardown");
    }
    void run() {
        const auto result = Application::run(window);
        if (result) std::wcerr << window.error() << L'\n';
        check(result == 0 && done, "Owned memory image window completes");
    }
};
void run() { Fixture fixture; fixture.run(); std::cout << "Memory image hidden/deferred readiness, source-aspect pixels, target recreation and fatal cleanup passed\n"; }
}

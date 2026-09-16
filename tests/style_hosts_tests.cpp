#include "xui/image.hpp"
#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#ifdef _WIN32
#include "../src/images.hpp"
#endif

namespace {
thread_local bool count_allocations{};
thread_local std::size_t allocations{};
}
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) {
        if (count_allocations) ++allocations;
        return value;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void require_near(float actual, float expected) { require(std::abs(actual - expected) < 0.001f, "Owned content metric matches"); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Unsupported style combination must fail");
}
PartStyleValues foreground(uint32_t color) { PartStyleValues value; value.foreground = ThemeColor{color}; return value; }
PartStyleValues frame() {
    PartStyleValues value;
    value.background = ThemeColor{0x102030, 0x304050};
    value.foreground = ThemeColor{0xabcdef};
    value.border_brush = ThemeColor{0x112233};
    value.padding = Insets{10, 12, 14, 16};
    value.border_thickness = Insets{2, 3, 4, 5};
    value.corner_radius = 8.0f;
    return value;
}
void schema_boundaries() {
    const auto root = frame();
    for (const auto target : {StyleTarget::image, StyleTarget::vector_canvas, StyleTarget::map_view,
        StyleTarget::media_playback, StyleTarget::web_content}) {
        validate_part_values(target, StylePart::root, root);
        PartStyleValues size; size.size = 10.0f;
        rejects([&] { validate_part_values(target, StylePart::root, size); });
        PartStyleValues alignment; alignment.horizontal_alignment = StyleAlignment::end;
        rejects([&] { validate_part_values(target, StylePart::root, alignment); });
        rejects([&] { validate_part_values(target, StylePart::icon, foreground(0)); });
    }
    PartStyleValues fill; fill.background = ThemeColor{0};
    for (auto target : {StyleTarget::vector_canvas, StyleTarget::map_view})
        rejects([&] { validate_part_values(target, StylePart::selection, fill); });
    rejects([] { ControlStyle::create(StyleTarget::web_content, {}, {{StylePart::root, style_states::playing, {}}}); });
    rejects([] { ControlStyle::create(StyleTarget::web_content, {}, {{StylePart::root, style_states::paused, {}}}); });
    rejects([] { ControlStyle::create(StyleTarget::vector_canvas, {}, {{StylePart::root, style_states::error, {}}}); });
    rejects([] { ControlStyle::create(StyleTarget::map_view, {}, {{StylePart::root, style_states::empty, {}}}); });
    rejects([] { ControlStyle::create(StyleTarget::image, {}, {{StylePart::root, style_states::selected, {}}}); });
}
void host_typography_inheritance() {
    const std::pair<StyleTarget, StylePart> cases[]{
        {StyleTarget::image, StylePart::placeholder}, {StyleTarget::image, StylePart::error},
        {StyleTarget::vector_canvas, StylePart::empty},
        {StyleTarget::map_view, StylePart::coordinate}, {StyleTarget::map_view, StylePart::error},
        {StyleTarget::media_playback, StylePart::placeholder}, {StyleTarget::media_playback, StylePart::caption},
        {StyleTarget::media_playback, StylePart::status}, {StyleTarget::media_playback, StylePart::error},
        {StyleTarget::web_content, StylePart::placeholder}, {StyleTarget::web_content, StylePart::caption},
        {StyleTarget::web_content, StylePart::status}, {StyleTarget::web_content, StylePart::error}
    };
    PartStyleValues typography;
    typography.font_family = make_style_font_family("Segoe UI");
    typography.font_size = 19.0f; typography.font_weight = 650; typography.font_style = StyleFontStyle::italic;
    for (auto [target, part] : cases) {
        PartStyleValues alignment;
        alignment.vertical_alignment = StyleAlignment::stretch;
        rejects([&] { validate_part_values(target, part, alignment); });
        alignment.vertical_alignment = StyleAlignment::end;
        alignment.horizontal_alignment = StyleAlignment::stretch;
        validate_part_values(target, part, alignment);
        ControlStyleAttachment attachment(target);
        attachment.assign_local(StylePart::root, typography, 0);
        auto* values = attachment.effective(part, 0);
        require(values && values->font_family == typography.font_family && values->font_size == typography.font_size &&
            values->font_weight == typography.font_weight && values->font_style == typography.font_style,
            "Every actual host text part inherits all four root font fields");
        PartStyleValues local; local.font_size = 11.0f; local.horizontal_alignment = StyleAlignment::end;
        attachment.assign_local(part, local, 0);
        values = attachment.effective(part, 0);
        require(values && values->font_size == 11 && values->font_weight == 650 &&
            values->horizontal_alignment == StyleAlignment::end, "Named text font and alignment override inherited defaults");
        attachment.assign_local(part, {}, 0);
        require(attachment.effective(part, 0)->font_size == 19, "Clearing a text override exposes root typography again");
    }
}
void scene_geometry_and_identity() {
    VectorCanvas canvas;
    canvas.arrange({100, 200, 400, 300});
    auto shape = VectorShape::rectangle(7, {20, 20, 60, 50});
    shape.fill = {0.2f, 0.4f, 0.6f, 1}; shape.stroke = {0.8f, 0.6f, 0.4f, 1};
    shape.interactive = true; shape.name = L"Authored shape";
    const auto scene = std::make_shared<const VectorScene>(std::vector<VectorShape>{shape});
    canvas.set_scene(scene);
    const auto child = canvas.accessible_items();
    auto values = frame();
    auto selected = foreground(0xff00ff);
    auto style = ControlStyle::create(StyleTarget::vector_canvas, {{StylePart::root, values}},
        {{StylePart::selection, style_states::selected, selected}});
    canvas.set_control_style(style);
    canvas.arrange({100, 200, 400, 300});
    const auto area = canvas.canvas_bounds();
    require_near(area.x, 12); require_near(area.y, 15); require_near(area.width, 370);
    require_near(canvas.content_bounds().height, 264);
    require(canvas.hit_test({42, 45}) == 7 && !canvas.hit_test({8, 8}), "Hit testing uses the inset scene origin");
    require(child->bounds().x == 112 && child->bounds().width == 370, "Retained UIA list uses owned frame metrics");
    canvas.set_selected(7);
    require(canvas.effective_control_style_values(StylePart::selection)->foreground == selected.foreground, "Real selection resolves highlight");
    require(canvas.scene() == scene && canvas.accessible_items() == child, "Styling preserves scene and semantic child identity");
    require(scene->shapes()[0].fill.blue == 0.6f && scene->shapes()[0].stroke.red == 0.8f, "Authored shape pixels retain data colors");
    canvas.set_control_style(nullptr);
    canvas.arrange({100, 200, 400, 300});
    require_near(canvas.canvas_bounds().x, 0); require_near(canvas.canvas_bounds().width, 400);
    require(canvas.selected() == 7 && canvas.scene() == scene, "Clearing a style preserves selection and scene");
    auto empty = ControlStyle::create(StyleTarget::vector_canvas, {},
        {{StylePart::empty, style_states::empty, foreground(0x123456)}});
    canvas.set_control_style(empty); canvas.set_scene({});
    require(canvas.effective_control_style_values(StylePart::empty)->foreground == ThemeColor{0x123456}, "Empty follows the real scene");
    canvas.set_scene(scene);
    PartStyleValues inset; inset.padding = Insets{9, 9, 9, 9};
    auto metric_state = ControlStyle::create(StyleTarget::vector_canvas, {},
        {{StylePart::root, style_states::selected, inset}});
    canvas.set_control_style(metric_state);
    int layouts{}, paints{};
    canvas.set_invalidator([&](Invalidation kind) { if (kind == Invalidation::layout) ++layouts; else ++paints; });
    canvas.set_control_style(metric_state);
    require(layouts == 0 && paints == 0, "Same style identity does not invalidate the host");
    canvas.set_selected(7);
    require(layouts > 0, "Real selection requests layout when its style changes frame metrics");
    canvas.arrange({100, 200, 400, 300});
    require_near(canvas.canvas_bounds().x, 9);
    canvas.set_invalidator({});
}
void map_request_and_state() {
    MapView map;
    map.arrange({0, 0, 400, 300});
    const auto request = map.request_overlay();
    auto root = frame();
    auto loading = foreground(0x667788), error = foreground(0xee1122);
    auto style = ControlStyle::create(StyleTarget::map_view, {{StylePart::root, root}},
        {{StylePart::coordinate, style_states::loading, loading}, {StylePart::error, style_states::error, error}});
    map.set_control_style(style);
    map.arrange({0, 0, 400, 300});
    require(!request.stop.stop_requested() && map.loading(), "Frame styling does not cancel a pending provider request");
    require(map.effective_control_style_values(StylePart::coordinate)->foreground == loading.foreground, "Request loading is observable");
    require_near(map.screen_point(map.center()).x, map.canvas_bounds().x + map.canvas_bounds().width / 2);
    const auto location = map.location_at({80, 90});
    const auto screen = map.screen_point(location);
    require_near(screen.x, 80); require_near(screen.y, 90);
    require(map.complete(request, {}, L"Provider failed") && !map.loading(), "Error completion retains owner token semantics");
    require(map.effective_control_style_values(StylePart::error)->foreground == error.foreground, "Provider error drives styling");
    const auto replacement = map.request_overlay();
    require(map.error().empty() && map.loading(), "A new request clears obsolete error presentation");
    map.set_control_style(nullptr); map.arrange({0, 0, 400, 300});
    require(!replacement.stop.stop_requested(), "Style removal preserves a pending request");
    map.cancel_request();
    require(replacement.stop.stop_requested() && !map.loading(), "Explicit cancellation still cancels");
    map.set_control_style(style);
    map.set_overlay({{{21, map.center(), L"Styled marker"}}, {}});
    map.arrange({0, 0, 400, 300});
    require(map.hit_test(map.screen_point(map.center())) == 21, "Map projection and hit testing use the same styled content origin");
    const auto anchored = map.location_at({80, 90});
    map.zoom_at(1, {80, 90});
    const auto after_zoom = map.screen_point(anchored);
    require_near(after_zoom.x, 80); require_near(after_zoom.y, 90);
}
void runtime_states_and_identity() {
    constexpr HostState states[]{HostState::idle, HostState::loading, HostState::ready, HostState::playing,
        HostState::paused, HostState::stopped, HostState::suspended, HostState::error};
    constexpr StyleStateMask bits[]{style_states::idle, style_states::loading, style_states::ready, style_states::playing,
        style_states::paused, style_states::stopped, style_states::suspended, style_states::error};
    MediaPlayback media;
    int operations{};
    media.bind([&](MediaOperation, double) { ++operations; });
    media.load_local(L"C:\\owned\\style-fixture.wav");
    const auto revision = media.revision(), status_id = media.status()->id();
    std::vector<StyleRule> rules;
    for (unsigned i = 0; i < 8; ++i) rules.push_back({StylePart::status, bits[i], foreground(0x101010 + i)});
    auto style = ControlStyle::create(StyleTarget::media_playback, {{StylePart::root, frame()}}, std::move(rules));
    media.set_control_style(style); media.arrange({100, 200, 400, 300});
    require_near(media.visual_bounds().x, 12); require_near(media.visual_bounds().y, 15);
    require_near(media.visual_bounds().width, 370); require_near(media.visual_bounds().height, 220);
    require_near(media.status()->bounds().x, 112); require_near(media.status()->bounds().y, 435);
    for (unsigned i = 0; i < 8; ++i) {
        media.publish_state(states[i], L"Actual adapter state");
        require(media.effective_control_style_values(StylePart::status)->foreground == ThemeColor{0x101010 + i},
            "Each actual media state selects its own style");
    }
    PartStyleValues local; local.padding = Insets{1, 1, 1, 1};
    media.set_control_style_values(StylePart::root, local);
    media.set_control_style(nullptr);
    require(media.has_control_styling(), "Style removal preserves local metrics");
    media.set_control_style_values(StylePart::root, {});
    require(!media.has_control_styling(), "Clearing all locals returns to the allocation-free path");
    require(media.revision() == revision && media.status()->id() == status_id && operations == 1,
        "Styles do not issue media commands or replace retained content");
    WebContent web;
    int web_operations{};
    web.bind([&](WebOperation) { ++web_operations; }, {});
    auto web_style = ControlStyle::create(StyleTarget::web_content, {{StylePart::root, frame()}},
        {{StylePart::status, style_states::stopped, foreground(0x123456)}});
    web.set_control_style(web_style);
    web.publish_state(HostState::stopped, L"Web loading stopped");
    require(web.effective_control_style_values(StylePart::status)->foreground == ThemeColor{0x123456},
        "Web stop is a real native state even though the inventory originally omitted it");
    web.set_control_style(nullptr);
    require(!web.has_source() && web.revision() == 0 && web_operations == 0, "Web styles never opt in to a runtime");
    MediaPlayback constrained;
    constrained.set_control_style_values(StylePart::root, frame());
    constrained.arrange({20, 30, 5, 7});
    require_near(constrained.visual_bounds().x, 5); require_near(constrained.visual_bounds().y, 7);
    require_near(constrained.visual_bounds().width, 0); require_near(constrained.visual_bounds().height, 0);
    require_near(constrained.status()->bounds().x, 25); require_near(constrained.status()->bounds().y, 37);
}
void default_path() {
    VectorCanvas canvas; MediaPlayback media; WebContent web;
    require(!canvas.has_control_styling() && !media.has_control_styling() && !web.has_control_styling(), "Default hosts have no style attachments");
    allocations = 0; count_allocations = true;
    for (unsigned i = 0; i < 100; ++i) {
        (void)canvas.canvas_bounds(); (void)media.visual_bounds(); (void)web.visual_bounds();
        (void)canvas.effective_control_style_values(StylePart::root);
    }
    count_allocations = false;
    require(allocations == 0, "Default host style reads and geometry allocate nothing");
}
#ifdef _WIN32
void image_model() {
    Image image; image.arrange({0, 0, 200, 120});
    auto style = ControlStyle::create(StyleTarget::image, {{StylePart::root, frame()}},
        {{StylePart::placeholder, style_states::empty, foreground(0x110011)},
         {StylePart::placeholder, style_states::loading, foreground(0x220022)},
         {StylePart::root, style_states::ready, foreground(0x330033)},
         {StylePart::error, style_states::error, foreground(0x440044)}});
    image.set_control_style(style);
    require(image.effective_control_style_values(StylePart::placeholder)->foreground == ThemeColor{0x110011}, "Image empty status resolves");
    image.set_source(L"C:\\owned\\not-decoded.png");
    const auto revision = image.revision();
    require(image.effective_control_style_values(StylePart::placeholder)->foreground == ThemeColor{0x220022}, "Image loading status resolves");
    require_near(image.content_bounds().x, 12); require_near(image.content_bounds().width, 170);
    ImagePeer peer(image);
    peer.revision = revision; peer.visible = true;
    const auto pending = std::make_shared<ImageRequest>();
    peer.request = pending;
    image.set_control_style(nullptr);
    require_near(image.content_bounds().x, 2); require_near(image.content_bounds().width, 196);
    require(image.revision() == revision && image.status() == ImageStatus::loading, "Style replacement does not revise the decode source");
    require(peer.request == pending && !pending->cancelled, "Style replacement preserves decode request identity");
    image.set_control_style(style);
    const auto pixels = std::make_shared<ImagePixels>();
    pending->pixels = pixels; pending->done = true;
    require(peer.deliver() && image.status() == ImageStatus::ready, "Owned decode completion drives ready state");
    require(image.effective_control_style_values(StylePart::root)->foreground == ThemeColor{0x330033}, "Ready image style resolves");
    image.set_control_style(nullptr); image.set_control_style(style);
    require(peer.pixels == pixels && image.revision() == revision, "Style replacement preserves retained decoded pixels");
    peer.request = std::make_shared<ImageRequest>();
    peer.request->done = true; peer.request->error = L"Owned decode failure";
    require(peer.deliver() && image.status() == ImageStatus::error, "Owned decode failure drives error state");
    require(image.effective_control_style_values(StylePart::error)->foreground == ThemeColor{0x440044}, "Error image style resolves");
    peer.request = std::make_shared<ImageRequest>();
    const auto cancelled = peer.request;
    peer.detach();
    require(cancelled->cancelled && image.status() == ImageStatus::empty, "Explicit detach still cancels the owned decode");
    require(ImageResources::statistics().decoded == 0, "Unattached styled image never starts decoding");
}
#endif
}
int main() {
    try {
        schema_boundaries(); host_typography_inheritance(); scene_geometry_and_identity(); map_request_and_state();
        runtime_states_and_identity(); default_path();
#ifdef _WIN32
        image_model();
#endif
        std::cout << "Host style schema, geometry, states, identity, cancellation, and default allocations passed\n";
        return 0;
    } catch (const std::exception& error) { count_allocations = false; std::cerr << error.what() << '\n'; return 1; }
}

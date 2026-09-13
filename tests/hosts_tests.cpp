#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace xui;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) { bool rejected{}; try { f(); } catch (const std::exception&) { rejected = true; } require(rejected, "Invalid value rejected"); }
void near(double a, double b, double tolerance = 0.001) { require(std::abs(a - b) < tolerance, "Coordinate roundtrip"); }
}
int main() {
    try {
        auto rectangle = VectorShape::rectangle(1, {0, 0, 100, 100});
        rectangle.fill = {0, 0, 1, 1}; rectangle.interactive = true; rectangle.name = L"Blue";
        rectangle.transform = {0, 1, -1, 0, 150, 10}; rectangle.clip = Rect{60, 0, 50, 200};
        auto ellipse = VectorShape::ellipse(2, {70, 30, 20, 20}); ellipse.fill = {1, 0, 0, 1}; ellipse.name = L"Red"; ellipse.interactive = true;
        auto scene = std::make_shared<const VectorScene>(std::vector<VectorShape>{rectangle, ellipse});
        require(scene->hit_test({80, 40}) == 2, "Topmost shape wins");
        require(scene->hit_test({65, 50}) == 1, "Transformed polygon hit");
        require(!scene->hit_test({55, 50}), "Canvas-space clip applies to hit testing");
        require(!scene->hit_test({140, 50}), "Clipped transformed geometry has no hit");
        { auto empty_clip = rectangle; empty_clip.clip = Rect{65, 50, 0, 0};
          VectorScene invisible({empty_clip}); require(!invisible.hit_test({65, 50}), "An empty clip never produces a hit"); }
        rejects([&] { auto invalid = rectangle; invalid.points[0].x = std::numeric_limits<float>::infinity(); VectorScene s({invalid}); });
        rejects([&] { VectorScene s({rectangle, rectangle}); });
        rejects([&] { auto invalid = rectangle; invalid.id = 0; VectorScene s({invalid}); });
        rejects([&] { auto invalid = rectangle; invalid.fill.alpha = -1; VectorScene s({invalid}); });
        rejects([&] { VectorScene s(std::vector<VectorShape>(4097, rectangle)); });
        VectorCanvas canvas; int calls{}, layouts{};
        canvas.set_scene(scene); canvas.arrange({0, 0, 400, 300});
        require(!canvas.hit_test({80, 280}) && canvas.hit_test({80, 40}) == 2, "Canvas hit testing excludes the semantic-list viewport");
        canvas.set_invalidator([&](Invalidation kind) { if (kind == Invalidation::layout) ++layouts; });
        canvas.on_select([&](ShapeId id) { ++calls; require(id == 2, "Stable semantic callback identity"); canvas.set_selected(1); });
        canvas.set_selected(1); require(calls == 0, "Property selection is silent");
        require(canvas.select(2) && calls == 1 && canvas.selected() == 1, "Selection callback can reenter");
        canvas.set_scene(std::make_shared<const VectorScene>(std::vector<VectorShape>{ellipse, rectangle}));
        require(layouts == 0 && canvas.selected() == 1, "Snapshot reorder paints without layout and preserves identity");
        require(!canvas.accessible_items()->multiple_selection(), "Scene semantics advertise single selection");
        canvas.accessible_items()->select({1, 1}, SelectionGesture::toggle);
        require(!canvas.selected(), "Semantic deselection clears the scene selection");
        canvas.on_select({});
        canvas.accessible_items()->select_all();
        require(canvas.selected() && canvas.accessible_items()->selection().storage_size() == 1, "Select all remains single-selection for scene alternatives");
        MapView map; map.arrange({0, 0, 640, 400});
        near(MapView::wrap_longitude(181), -179); near(MapView::wrap_longitude(-181), 179);
        for (auto p : {GeoPoint{0, 0}, GeoPoint{60, 179.5}, GeoPoint{-60, -179.5}, GeoPoint{85, 45}}) {
            const auto q = MapView::unproject(MapView::project(p)); near(q.latitude, p.latitude); near(q.longitude, p.longitude);
        }
        near(MapView::normalize({90, 0}).latitude, 85.0511287798066);
        rejects([&] { map.set_view({std::numeric_limits<double>::quiet_NaN(), 0}, 1); });
        map.set_overlay({{{21, {10, 179}, L"East"}, {22, {10, -179}, L"West"}}, {}});
        map.set_view({10, 179}, 3);
        const auto a = map.screen_point({10, 179}), b = map.screen_point({10, -179});
        require(std::abs(a.x - b.x) < 20, "Dateline markers are geographically adjacent");
        const auto origin = map.location_at({170, 100}); map.zoom_at(1, {170, 100});
        const auto anchored = map.location_at({170, 100}); near(origin.latitude, anchored.latitude); near(origin.longitude, anchored.longitude);
        const auto request = map.request_overlay(); map.pan(10, 10);
        require(request.stop.stop_requested() && !map.complete(request, {}), "Pan cancels obsolete overlay requests");
        MapView other; const auto foreign = other.request_overlay(); auto own = map.request_overlay();
        require(!map.complete(foreign, {}), "Foreign provider tokens are rejected");
        require(map.complete(own, {}, L"Owned provider error") && !map.complete(own, {}), "One completion per owner generation");
        require(map.error() == L"Owned provider error", "Provider errors remain visible in model");
        auto retiring_map = std::make_unique<MapView>();
        retiring_map->on_request([&](MapRequest) { retiring_map.reset(); });
        const auto retired = retiring_map->request_overlay();
        require(retired.stop.stop_requested(), "Provider request callback can dispose its owner");
        auto retiring_canvas = std::make_unique<VectorCanvas>();
        retiring_canvas->set_scene(scene); retiring_canvas->on_select([&](ShapeId) { retiring_canvas.reset(); });
        auto retained_items = retiring_canvas->accessible_items();
        require(retiring_canvas->select(1) && !retiring_canvas, "Scene selection callback can dispose its owner");
        retained_items->select({2, 1});
        map.set_view({}, 100); near(map.zoom(), 20);
        map.set_view({60, 179}, 20);
        const auto precise = map.location_at({123.5f, 99.25f});
        const auto screen = map.screen_point(precise); near(screen.x, 123.5, 0.01); near(screen.y, 99.25, 0.01);
        map.set_view({}, -10); near(map.zoom(), 0);
        MediaPlayback media;
        rejects([&] { media.load_local(L"https://example.com/a.mp4"); });
        rejects([&] { media.load_local(L"\\\\server\\share\\file.wav"); });
        rejects([&] { media.load_local(L"C:\\file.wav:stream"); });
        rejects([&] { media.set_volume(1.1); });
        rejects([&] { media.seek(-1); });
        int operations{}; media.bind([&](MediaOperation operation, double value) { ++operations; if (operation == MediaOperation::volume) near(value, 0.25); });
        media.load_local(L"C:\\owned\\fixture.wav"); media.play(); media.pause(); media.stop(); media.seek(1); media.set_volume(0.25); media.unload();
        require(operations == 7 && media.source().empty(), "Media adapter receives real commands");
        WebContent web;
        web.set_html(L""); require(web.has_source() && web.is_html(), "An explicit empty HTML document remains a load request");
        web.unload(); require(!web.has_source(), "Unload clears the explicit web source");
        rejects([&] { web.navigate(L"https://example.com"); });
        rejects([&] { web.set_allowed_origins({L"http://example.com"}); });
        rejects([&] { web.set_allowed_origins({L"https://user@example.com"}); });
        web.set_allowed_origins({L"https://example.com"});
        require(web.allows(L"https://example.com/path") && !web.allows(L"https://example.com.evil/path") &&
            !web.allows(L"https://example.com@evil/path") && !web.allows(L"file:///C:/owned"), "Exact web origin policy");
        require(web.allows(L"https://EXAMPLE.com:443/path"), "Default HTTPS ports use the same origin");
        rejects([&] { web.set_html(std::wstring(1024 * 1024 + 1, L'x')); });
        web.set_html(L"<h1>Owned</h1>"); require(web.is_html(), "Explicit owned HTML");
        web.navigate(L"https://example.com/path"); require(!web.is_html(), "Explicit allowlisted navigation");
        std::cout << "Scene geometry, stable identity, paint-only snapshots, Mercator projection, cancellation, and host policy passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

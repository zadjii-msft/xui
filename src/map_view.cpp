#include "xui/map_view.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>

namespace xui {
namespace {
constexpr double pole = 85.0511287798066, pi = std::numbers::pi;
void valid(double n) { if (!std::isfinite(n) || std::abs(n) > 1e9) throw std::invalid_argument("Map coordinates must be finite and bounded"); }
}
MapView::MapView(std::wstring name) : VectorCanvas(ControlRole::map_view, std::move(name)) {
    set_help_text(L"Offline Mercator coordinate map. No bundled street basemap. Drag or use arrows to pan. Plus and minus zoom. Select markers in the list.");
    rebuild();
}
double MapView::wrap_longitude(double lon) {
    valid(lon); lon = std::fmod(lon + 180, 360); if (lon < 0) lon += 360; return lon - 180;
}
GeoPoint MapView::normalize(GeoPoint p) {
    valid(p.latitude); return {std::clamp(p.latitude, -pole, pole), wrap_longitude(p.longitude)};
}
WorldPoint MapView::project(GeoPoint point) {
    auto p = normalize(point);
    const double rad = p.latitude * pi / 180;
    return {(p.longitude + 180) / 360, std::clamp((1 - std::asinh(std::tan(rad)) / pi) / 2, 0.0, 1.0)};
}
GeoPoint MapView::unproject(WorldPoint p) {
    valid(p.x); valid(p.y);
    return normalize({std::atan(std::sinh(pi * (1 - 2 * std::clamp<double>(p.y, 0, 1)))) * 180 / pi, p.x * 360.0 - 180});
}
void MapView::set_view(GeoPoint center, double zoom) {
    valid(zoom);
    center = normalize(center); zoom = std::clamp(zoom, 0.0, 20.0);
    if (center.latitude == center_.latitude && center.longitude == center_.longitude && zoom == zoom_) return;
    cancel_request(); center_ = center; zoom_ = zoom; rebuild();
}
Point MapView::screen_point(GeoPoint point) const {
    const auto p = project(point), c = project(center_);
    const double world = 256 * std::exp2(zoom_);
    double dx = p.x - c.x; dx -= std::round(dx);
    auto b = canvas_bounds();
    return {static_cast<float>(b.x + b.width / 2 + dx * world), static_cast<float>(b.y + b.height / 2 + (p.y - c.y) * world)};
}
GeoPoint MapView::location_at(Point point) const {
    valid(point.x); valid(point.y);
    const auto c = project(center_); const auto b = canvas_bounds(); const double world = 256 * std::exp2(zoom_);
    return unproject({c.x + (point.x - b.x - b.width / 2) / world, c.y + (point.y - b.y - b.height / 2) / world});
}
void MapView::pan(double x, double y) {
    valid(x); valid(y);
    const auto b = canvas_bounds();
    set_view(location_at({static_cast<float>(b.x + b.width / 2 - x), static_cast<float>(b.y + b.height / 2 - y)}), zoom_);
}
void MapView::zoom_at(double delta, Point anchor) {
    valid(delta);
    const auto before = location_at(anchor);
    set_view(center_, zoom_ + delta);
    const auto after = screen_point(before);
    pan(anchor.x - after.x, anchor.y - after.y);
}
void MapView::set_overlay(MapOverlay overlay) {
    if (overlay.markers.size() > 256 || overlay.lines.size() > 256) throw std::length_error("Map overlay limit exceeded");
    std::set<ShapeId> ids; std::size_t count{};
    for (auto& m : overlay.markers) {
        if (!m.id || m.id >= (1ull << 63) || !ids.insert(m.id).second || m.name.empty() || m.name.size() > 256)
            throw std::invalid_argument("Map markers require distinct IDs and bounded names");
        m.location = normalize(m.location);
    }
    for (auto& line : overlay.lines) {
        count += line.points.size();
        if (line.points.size() < 2 || count > 2048) throw std::length_error("Map polyline point limit exceeded");
        for (auto c : {line.color.red, line.color.green, line.color.blue, line.color.alpha})
            if (!std::isfinite(c) || c < 0 || c > 1) throw std::invalid_argument("Map colors must be between zero and one");
        for (auto& p : line.points) p = normalize(p);
    }
    cancel_request(); overlay_ = std::move(overlay); error_.clear(); rebuild();
}
StyleStateMask MapView::control_style_state_bits() const {
    return Control::control_style_state_bits() | (selected() ? style_states::selected : 0) |
        (loading_ ? style_states::loading : 0) | (!error_.empty() ? style_states::error : 0);
}
void MapView::cancel_request() {
    stop_.request_stop(); ++generation_;
    if (loading_) { loading_ = false; invalidate_state(); }
}
MapRequest MapView::request_overlay() {
    cancel_request(); stop_ = std::stop_source{};
    loading_ = true; error_.clear(); invalidate_state();
    MapRequest request{generation_, center_, zoom_, stop_.get_token()};
    auto callback = request_callback_; if (callback) callback(request);
    return request;
}
bool MapView::complete(const MapRequest& request, MapOverlay overlay, std::wstring error) {
    if (request.generation != generation_ || request.stop != stop_.get_token() || request.stop.stop_requested()) return false;
    if (error.size() > 4096) throw std::length_error("Map error exceeds 4096 units");
    if (error.empty()) set_overlay(std::move(overlay));
    else { cancel_request(); error_ = std::move(error); invalidate_state(); }
    return true;
}
void MapView::arrange(Rect b) {
    VectorCanvas::arrange(b);
    const auto after = canvas_bounds();
    if (scene_size_.width != after.width || scene_size_.height != after.height) rebuild();
}
void MapView::rebuild() {
    std::vector<VectorShape> shapes;
    ShapeId id = (1ull << 63);
    const auto area = canvas_bounds();
    const Rect clip{0, 0, area.width, area.height};
    scene_size_ = {clip.width, clip.height};
    const auto scene_point = [&](GeoPoint point) {
        const auto screen = screen_point(point);
        return Point{screen.x - area.x, screen.y - area.y};
    };
    const double world = 256 * std::exp2(zoom_);
    for (int lon = -180; lon < 180; lon += 30) {
        const auto p = scene_point({0, static_cast<double>(lon)});
        VectorShape s; s.id = id++; s.points = {{p.x, 0}, {p.x, clip.height}}; s.stroke = {0.3f, 0.4f, 0.5f, 1}; s.clip = clip; shapes.push_back(s);
    }
    for (int lat = -60; lat <= 60; lat += 30) {
        const auto p = scene_point({static_cast<double>(lat), center_.longitude});
        VectorShape s; s.id = id++; s.points = {{0, p.y}, {clip.width, p.y}}; s.stroke = {0.3f, 0.4f, 0.5f, 1}; s.clip = clip; shapes.push_back(s);
    }
    for (const auto& line : overlay_.lines) {
        for (std::size_t i = 1; i < line.points.size(); ++i) {
            auto a = scene_point(line.points[i - 1]), b = scene_point(line.points[i]);
            if (b.x - a.x > world / 2) b.x -= static_cast<float>(world);
            if (a.x - b.x > world / 2) b.x += static_cast<float>(world);
            VectorShape s; s.id = id++; s.points = {a, b}; s.stroke = line.color; s.stroke_width = 2; s.clip = clip; shapes.push_back(std::move(s));
        }
    }
    for (const auto& marker : overlay_.markers) {
        auto p = scene_point(marker.location);
        auto s = VectorShape::ellipse(marker.id, {p.x - 6, p.y - 6, 12, 12});
        s.fill = {1, 0.6f, 0.1f, 1}; s.stroke = {0, 0, 0, 1}; s.clip = clip;
        s.interactive = true; s.name = marker.name; shapes.push_back(std::move(s));
    }
    set_scene(std::make_shared<const VectorScene>(std::move(shapes)));
    if (canvas_bounds().height != clip.height) rebuild();
}
}

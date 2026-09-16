#pragma once
#include "xui/vector_canvas.hpp"
#include <stop_token>

namespace xui {
struct GeoPoint { double latitude{}, longitude{}; };
struct WorldPoint { double x{}, y{}; };
struct MapMarker { ShapeId id{}; GeoPoint location; std::wstring name; };
struct MapPolyline { std::vector<GeoPoint> points; SceneColor color{0.9f, 0.7f, 0.2f, 1}; };
struct MapOverlay { std::vector<MapMarker> markers; std::vector<MapPolyline> lines; };
struct MapRequest {
    std::uint64_t generation{};
    GeoPoint center;
    double zoom{};
    std::stop_token stop;
};
class MapView final : public VectorCanvas {
public:
    explicit MapView(std::wstring name = L"Offline coordinate map");
    ~MapView() override { stop_.request_stop(); }
    static double wrap_longitude(double longitude);
    static GeoPoint normalize(GeoPoint point);
    static WorldPoint project(GeoPoint point);
    static GeoPoint unproject(WorldPoint point);
    void set_view(GeoPoint center, double zoom);
    GeoPoint center() const { return center_; }
    double zoom() const { return zoom_; }
    void pan(double x_dips, double y_dips);
    void zoom_at(double delta, Point anchor);
    Point screen_point(GeoPoint point) const;
    GeoPoint location_at(Point point) const;
    void set_overlay(MapOverlay overlay);
    void on_request(std::function<void(MapRequest)> callback) { request_callback_ = std::move(callback); }
    MapRequest request_overlay();
    bool complete(const MapRequest& request, MapOverlay overlay, std::wstring error = {});
    void cancel_request();
    const std::wstring& error() const { return error_; }
    bool loading() const { return loading_; }
    void arrange(Rect bounds) override;
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::map_view; }
    StyleStateMask control_style_state_bits() const override;
private:
    GeoPoint center_{};
    double zoom_{1};
    MapOverlay overlay_;
    std::stop_source stop_;
    std::uint64_t generation_{};
    std::wstring error_;
    bool loading_{};
    Size scene_size_{};
    std::function<void(MapRequest)> request_callback_;
    void rebuild();
};
}

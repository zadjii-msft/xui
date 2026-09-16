#include "xui/vector_canvas.hpp"
#include "style_hosts_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>

namespace xui {
namespace {
void finite(double value) {
    if (!std::isfinite(value) || std::abs(value) > 1e9) throw std::invalid_argument("Scene coordinates must be finite and at most one billion");
}
void rect_valid(Rect r) {
    finite(r.x); finite(r.y); finite(r.width); finite(r.height);
    if (r.width < 0 || r.height < 0) throw std::invalid_argument("Scene dimensions must be nonnegative");
}
bool contains(Rect r, Point p) { return r.width > 0 && r.height > 0 && p.x >= r.x && p.y >= r.y && p.x <= r.x + r.width && p.y <= r.y + r.height; }
double distance(Point p, Point a, Point b) {
    const double x = b.x - a.x, y = b.y - a.y, length = x * x + y * y;
    const double t = length ? std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / length, 0.0, 1.0) : 0;
    return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
}
class ShapeSource final : public ItemsSource {
public:
    explicit ShapeSource(const std::shared_ptr<const VectorScene>& scene) {
        if (scene) for (const auto& shape : scene->shapes()) if (shape.interactive) entries.emplace_back(shape.id, shape.name);
    }
    std::size_t size() const override { return entries.size(); }
    ItemKey key(std::size_t index) const override { return {entries.at(index).first, 1}; }
    std::optional<std::size_t> find(ItemKey key) const override {
        if (key.version != 1) return {};
        for (std::size_t i = 0; i < entries.size(); ++i) if (entries[i].first == key.id) return i;
        return {};
    }
    ItemContent item(std::size_t index) const override { return {entries.at(index).second}; }
    std::vector<std::pair<ShapeId, std::wstring>> entries;
};
class SceneItems final : public VirtualCollection {
public:
    SceneItems() : VirtualCollection(ControlRole::items_view, L"Interactive scene elements") {}
    bool multiple_selection() const override { return false; }
    void replace(std::shared_ptr<const ItemsSource> value) { set_source(std::move(value)); }
    bool select(ItemKey key, SelectionGesture gesture = SelectionGesture::replace) override {
        if (!enabled() || !source() || !source()->find(key)) return false;
        if (gesture == SelectionGesture::toggle && selection().contains(key)) {
            CollectionSelection empty; empty.set_focus(key); set_selection(std::move(empty)); changed(); return true;
        }
        return VirtualCollection::select(key, SelectionGesture::replace);
    }
    void select_all() override {
        if (!source() || !source()->size()) return;
        select(selection().focused().value_or(source()->key(0)));
    }
    void set_presentation(ItemsPresentation value) override {
        if (value != ItemsPresentation::list) throw std::invalid_argument("Scene semantic alternatives use a single-selection list");
    }
};
}
Point SceneTransform::apply(Point p) const {
    const double x = m11 * p.x + m21 * p.y + dx, y = m12 * p.x + m22 * p.y + dy;
    finite(x); finite(y);
    return {static_cast<float>(x), static_cast<float>(y)};
}
VectorShape VectorShape::rectangle(ShapeId id, Rect r) {
    rect_valid(r);
    VectorShape shape; shape.id = id; shape.closed = true;
    shape.points = {{r.x, r.y}, {r.x + r.width, r.y}, {r.x + r.width, r.y + r.height}, {r.x, r.y + r.height}};
    return shape;
}
VectorShape VectorShape::ellipse(ShapeId id, Rect r) {
    rect_valid(r);
    VectorShape shape; shape.id = id; shape.closed = true;
    for (int i = 0; i < 64; ++i) {
        const double angle = i * std::numbers::pi / 32;
        shape.points.push_back({r.x + r.width * static_cast<float>((1 + std::cos(angle)) / 2),
            r.y + r.height * static_cast<float>((1 + std::sin(angle)) / 2)});
    }
    return shape;
}
VectorScene::VectorScene(std::vector<VectorShape> shapes) : shapes_(std::move(shapes)) {
    if (shapes_.size() > maximum_shapes) throw std::length_error("Scene shape limit exceeded");
    std::set<ShapeId> ids;
    std::size_t points{}, interactive{};
    for (auto& shape : shapes_) {
        if (!shape.id || !ids.insert(shape.id).second) throw std::invalid_argument("Scene IDs must be nonzero and unique");
        points += shape.points.size();
        if (points > maximum_points || shape.points.size() < 2) throw std::length_error("Scene point count is outside its bounds");
        if (shape.interactive && (++interactive > maximum_interactive || shape.name.empty())) throw std::invalid_argument("Interactive shapes require names and at most 256 entries");
        if (shape.name.size() > 256) throw std::length_error("Scene name exceeds 256 units");
        for (auto color : {shape.fill, shape.stroke}) for (auto c : {color.red, color.green, color.blue, color.alpha})
            if (!std::isfinite(c) || c < 0 || c > 1) throw std::invalid_argument("Scene colors must be between zero and one");
        if (!std::isfinite(shape.stroke_width) || shape.stroke_width < 0 || shape.stroke_width > 256)
            throw std::invalid_argument("Scene stroke width must be between zero and 256");
        for (auto v : {shape.transform.m11, shape.transform.m12, shape.transform.m21, shape.transform.m22, shape.transform.dx, shape.transform.dy}) finite(v);
        if (shape.clip) rect_valid(*shape.clip);
        for (auto& p : shape.points) { finite(p.x); finite(p.y); p = shape.transform.apply(p); }
        shape.transform = {};
    }
}
std::optional<ShapeId> VectorScene::hit_test(Point p) const {
    finite(p.x); finite(p.y);
    for (auto it = shapes_.rbegin(); it != shapes_.rend(); ++it) {
        const auto& s = *it;
        if (!s.interactive || (s.clip && !contains(*s.clip, p))) continue;
        bool inside{};
        for (std::size_t i = 0, j = s.points.size() - 1; i < s.points.size(); j = i++) {
            const auto a = s.points[j], b = s.points[i];
            if ((s.closed || i != 0) && s.stroke.alpha > 0 && s.stroke_width > 0 && distance(p, a, b) <= s.stroke_width / 2) return s.id;
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
        }
        if (s.closed && s.fill.alpha > 0 && inside) return s.id;
    }
    return {};
}
VectorCanvas::VectorCanvas(std::wstring name) : VectorCanvas(ControlRole::vector_canvas, std::move(name)) {}
VectorCanvas::~VectorCanvas() { items_->on_selection({}); items_->on_activate({}); }
VectorCanvas::VectorCanvas(ControlRole role, std::wstring name) : Control(role, std::move(name), {480, 360}),
    items_(std::make_shared<SceneItems>()), children_{items_} {
    adopt(items_);
    items_->set_item_size({240, 36});
    items_->on_selection([this] {
        const auto key = items_->selection().focused();
        if (key && items_->selection().contains(*key)) select(key->id);
        else set_selected({});
    });
    items_->on_activate([this](ItemKey key) { select(key.id); });
    set_help_text(L"Select interactive elements in the accompanying list. Shapes use straight segments, canvas-space clips, and fixed-width strokes.");
}
void VectorCanvas::set_scene(std::shared_ptr<const VectorScene> scene) {
    if (scene_ == scene) return;
    auto source = std::make_shared<ShapeSource>(scene);
    const bool layout = (!items_->source() || items_->source()->size() == 0) != source->entries.empty();
    scene_ = std::move(scene);
    static_cast<SceneItems&>(*items_).replace(source);
    if (selected_ && !source->find({*selected_, 1})) selected_.reset();
    set_selected(selected_);
    invalidate_state();
    invalidate(layout ? Invalidation::layout : Invalidation::paint);
}
void VectorCanvas::set_selected(std::optional<ShapeId> id) {
    if (id && (!items_->source() || !items_->source()->find({*id, 1}))) throw std::invalid_argument("Unknown interactive scene ID");
    const bool changed = selected_ != id;
    selected_ = id;
    CollectionSelection selection;
    if (id) { selection.set({*id, 1}, true); selection.set_focus(ItemKey{*id, 1}); }
    items_->set_selection(std::move(selection));
    if (changed) invalidate_state();
}
bool VectorCanvas::select(ShapeId id) {
    if (!enabled() || !items_->source() || !items_->source()->find({id, 1})) return false;
    if (selected_ == id) return true;
    set_selected(id);
    auto callback = selected_callback_;
    if (callback) callback(id);
    return true;
}
Rect VectorCanvas::canvas_bounds() const {
    const auto b = content_bounds();
    const float list = items_->source() && items_->source()->size() ? std::min(108.0f, b.height / 3) : 0;
    return {b.x, b.y, b.width, std::max(0.0f, b.height - list)};
}
Rect VectorCanvas::content_bounds() const {
    const auto b = bounds();
    const auto* root = effective_control_style_values(StylePart::root);
    return host_content_rect({0, 0, b.width, b.height}, root, {},
        root && visual_style() == VisualStyle::winui ? Insets{1, 1, 1, 1} : Insets{});
}
StyleStateMask VectorCanvas::control_style_state_bits() const {
    return Control::control_style_state_bits() | (selected_ ? style_states::selected : 0) |
        ((!scene_ || scene_->shapes().empty()) ? style_states::empty : 0);
}
std::optional<ShapeId> VectorCanvas::hit_test(Point point) const {
    finite(point.x); finite(point.y);
    const auto area = canvas_bounds();
    point.x -= area.x; point.y -= area.y;
    if (point.x < 0 || point.y < 0 || point.x >= area.width || point.y >= area.height || !scene_) return {};
    return scene_->hit_test(point);
}
void VectorCanvas::arrange(Rect b) {
    Control::arrange(b);
    const auto area = canvas_bounds();
    const auto content = content_bounds();
    items_->arrange({b.x + content.x, b.y + area.y + area.height, content.width, content.height - area.height});
}
}

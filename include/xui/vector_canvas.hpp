#pragma once
#include "xui/collections.hpp"
#include <array>

namespace xui {

using ShapeId = std::uint64_t;
struct SceneColor {
    float red{}, green{}, blue{}, alpha{1};
};
struct SceneTransform {
    double m11{1}, m12{}, m21{}, m22{1}, dx{}, dy{};
    Point apply(Point point) const;
};
struct VectorShape {
    ShapeId id{};
    std::vector<Point> points;
    bool closed{};
    SceneColor fill{0, 0, 0, 0}, stroke{0.3f, 0.7f, 1, 1};
    float stroke_width{1};
    SceneTransform transform;
    // Axis-aligned clip in canvas coordinates, after the shape transform.
    std::optional<Rect> clip;
    std::wstring name;
    bool interactive{};
    static VectorShape rectangle(ShapeId id, Rect bounds);
    static VectorShape ellipse(ShapeId id, Rect bounds);
};

class VectorScene final {
public:
    static constexpr std::size_t maximum_shapes = 4096, maximum_points = 65536, maximum_interactive = 256;
    explicit VectorScene(std::vector<VectorShape> shapes);
    // Points are transformed once at construction. Returned storage is immutable.
    const std::vector<VectorShape>& shapes() const { return shapes_; }
    std::optional<ShapeId> hit_test(Point point) const;
private:
    std::vector<VectorShape> shapes_;
};

class VectorCanvas : public Control {
public:
    explicit VectorCanvas(std::wstring name = L"Vector canvas");
    ~VectorCanvas() override;
    void set_scene(std::shared_ptr<const VectorScene> scene);
    const std::shared_ptr<const VectorScene>& scene() const { return scene_; }
    std::optional<ShapeId> hit_test(Point point) const;
    std::optional<ShapeId> selected() const { return selected_; }
    void set_selected(std::optional<ShapeId> id);
    bool select(ShapeId id);
    void on_select(std::function<void(ShapeId)> callback) { selected_callback_ = std::move(callback); }
    // The list is the keyboard and UIA alternative for interactive shapes.
    const std::shared_ptr<VirtualCollection>& accessible_items() const { return items_; }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
    Rect canvas_bounds() const;
    Rect content_bounds() const;
protected:
    VectorCanvas(ControlRole role, std::wstring name);
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::vector_canvas; }
    StyleStateMask control_style_state_bits() const override;
private:
    std::shared_ptr<const VectorScene> scene_;
    std::shared_ptr<VirtualCollection> items_;
    std::array<std::shared_ptr<Element>, 1> children_;
    std::optional<ShapeId> selected_;
    std::function<void(ShapeId)> selected_callback_;
};
}

#pragma once
#include "xui/collections.hpp"

namespace xui::detail {

// Content coordinates use doubles. Conversion to viewport-relative floats happens after scrolling.
struct CollectionBox {
    double x{}, y{}, width{}, height{};
    bool contains(double px, double py) const;
    CollectionBox intersect(CollectionBox other) const;
    Rect in_view(Rect viewport, double offset) const;
};
struct CollectionBand {
    std::size_t first{}, count{};
    CollectionBox row;
    double stride{};
    CollectionBox clip;
};
struct FrozenCollectionRow {
    const CollectionRow row;
    const ItemVisual visual;
    const bool selected{};
};
struct OutgoingCollectionRow {
    std::shared_ptr<const FrozenCollectionRow> frozen;
    CollectionBox bounds, clip;
};

// Internal, immutable, ordered single-column geometry. Bands partition logical indexes, not identity.
// Visible paint intervals cannot overlap. Gaps and clipped-away rows never acquire input identity.
class CollectionPresentation final {
public:
    static constexpr std::size_t maximum_bands = 4096, maximum_outgoing = 512;
    CollectionPresentation(std::shared_ptr<const ItemsSource> source, std::uint64_t version,
        double width, double item_height, double extent, std::vector<CollectionBand> bands,
        std::vector<OutgoingCollectionRow> outgoing = {});
    const std::shared_ptr<const ItemsSource>& source() const { return source_; }
    std::uint64_t version() const { return version_; }
    double width() const { return width_; }
    double item_height() const { return item_height_; }
    double extent() const { return extent_; }
    CollectionBox bounds(std::size_t index) const;
    CollectionBox clip(std::size_t index) const;
    std::vector<std::size_t> visible(double offset, double height) const;
    std::optional<std::size_t> hit(double x, double y) const;
    const std::vector<OutgoingCollectionRow>& outgoing() const { return outgoing_; }
private:
    const CollectionBand& band(std::size_t index) const;
    std::shared_ptr<const ItemsSource> source_;
    std::uint64_t version_{};
    double width_{}, item_height_{}, extent_{};
    std::vector<CollectionBand> bands_;
    struct Interval { double top{}, bottom{}; std::size_t band{}; };
    std::vector<Interval> intervals_;
    std::vector<OutgoingCollectionRow> outgoing_;
};

struct CollectionPresentationAccess {
    static std::shared_ptr<const FrozenCollectionRow> freeze(const VirtualCollection& control, const CollectionRow& row);
    static const std::shared_ptr<const CollectionPresentation>& get(const VirtualCollection& control) {
        return control.collection_presentation_;
    }
    static Rect clip(const VirtualCollection& control, std::size_t index) {
        const auto viewport = control.content_viewport();
        if (const auto& frame = get(control))
            return frame->clip(index).intersect({0, control.offset(), viewport.width, viewport.height})
                .in_view(viewport, control.offset());
        return viewport;
    }
};
}

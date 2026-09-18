#include "collection_presentation_fixture.hpp"
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace collection_fixture;
template<class F> void rejects(F&& callback) {
    bool rejected{};
    try { callback(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid presentation must fail explicitly");
}
void geometry_and_retirement() {
    Probe list;
    list.set_source(std::make_shared<Source>(std::vector<ItemKey>{{11, 7}, {22, 7}, {33, 7}}, true));
    list.arrange({0, 0, 300, 90});
    CollectionSelection selected;
    selected.set({22, 7}, true); selected.set_focus(ItemKey{22, 7});
    list.set_selection(selected);
    list.prepare();
    const auto old = list.source();
    const auto frozen = list.frozen;
    require(frozen->visual.icon == ButtonIcon::folder && frozen->visual.image_path == L"snapshot-22.png" &&
        frozen->row.content.primary == L"Row 22",
        "Outgoing text and visual metadata come from the immutable old row");
    list.present(20);
    const auto frame = CollectionPresentationAccess::get(list);
    require(list.source() == list.next && !list.source()->find({22, 7}) &&
        list.selection().focused() == ItemKey{11, 7}, "Logical membership and missing-focus repair change atomically");
    require(list.item_bounds(0).height == 40 && list.item_bounds(1).y == 60 &&
        list.maximum_offset() == 10, "Explicit geometry keeps gaps out of adjacent row height and scroll extent");
    require(!list.hit_test({50, 50}) && list.hit_test({50, 65}) == 1, "Outgoing pixels are a noninteractive gap");
    const auto rows = list.visible_content();
    require(rows.size() == 2 && rows[1].key == ItemKey{33, 7}, "Visible logical rows exclude draw-only outgoing content");
    require(frame->outgoing().front().frozen == frozen && frozen->selected, "Outgoing styling is frozen without keeping logical membership");
    list.set_source(list.source());
    require(CollectionPresentationAccess::get(list) == frame, "Equivalent logical source assignment preserves presentation");
    rejects([&] { CollectionPresentationAccess::freeze(list, CollectionRow{{22, 7}, {}, {}, 1}); });
    list.present(30);
    require(list.source() == frame->source() && frame->bounds(1).y == 60 &&
        list.item_bounds(1).y == 70, "Retargeted frames do not mutate published geometry or replace the source");
    rejects([&] { list.set_collection_presentation(frame); });
    require(list.item_bounds(1).y == 70, "Rejected stale frame preserves the current presentation");
    list.set_offset(5);
    require(!CollectionPresentationAccess::get(list) && list.retirements == 1 &&
        list.item_bounds(1).y == 40 && list.offset() == 0, "Explicit scroll settles to logical geometry and clamps once");
    list.present(20); list.reveal({33, 7});
    require(!CollectionPresentationAccess::get(list) && list.retirements == 2, "Focus reveal settles presentation");
    list.present(20); list.arrange({0, 0, 320, 90});
    require(!CollectionPresentationAccess::get(list), "Resize retires incompatible geometry");
    list.present(20); list.set_item_size({180, 44});
    require(!CollectionPresentationAccess::get(list), "Metric changes retire geometry");
    list.set_item_size({180, 40});
    list.present(20); list.cancel();
    require(!CollectionPresentationAccess::get(list), "Owner cancellation releases outgoing presentation");
    list.present(20); list.set_source(old);
    require(!CollectionPresentationAccess::get(list) && list.source() == old, "Ordinary source replacement clears presentation");
    list.prepare(); list.set_presentation(ItemsPresentation::tiles);
    rejects([&] { list.present(20); });
    require(!CollectionPresentationAccess::get(list), "The staged single-column contract rejects tile geometry");
}
class Million final : public ItemsSource {
public:
    std::size_t size() const override { return 1000000; }
    ItemKey key(std::size_t i) const override { ++keys; return {i + 1, 19}; }
    std::optional<std::size_t> find(ItemKey value) const override {
        return value.version == 19 && value.id && value.id <= size() ? std::optional<std::size_t>{value.id - 1} : std::nullopt;
    }
    ItemContent item(std::size_t) const override { ++reads; return {L"bounded"}; }
    mutable std::size_t reads{}, keys{};
};
void bounded_enumeration() {
    auto source = std::make_shared<Million>();
    Probe list; list.arrange({0, 0, 300, 180}); list.set_source(source);
    CollectionSelection selection; selection.set_focus(ItemKey{1, 19}); list.set_selection(selection);
    const double width = list.content_viewport().width;
    const auto bands = std::vector<CollectionBand>{
        {0, 1, {0, 0, width, 40}, 40, {0, 0, width, 40}},
        {1, 999998, {0, 40, width, 40}, 40, {0, 40, width, 0}},
        {999999, 1, {0, 80, width, 40}, 40, {0, 80, width, 40}}};
    source->reads = source->keys = 0;
    for (std::uint64_t version = 1; version <= 100; ++version) {
        list.set_collection_presentation(std::make_shared<const CollectionPresentation>(
            source, version, width, 40, 120, bands));
        const auto rows = list.visible_content();
        require(rows.size() == 2 && rows[1].index == 999999, "Hidden logical ranges do not expand visible enumeration");
        require(!list.hit_test({10, 60}) && list.hit_test({10, 90}) == 999999, "Binary geometry lookup preserves a large-index gap");
    }
    require(source->reads == 200 && source->keys == 200, "Per-frame data work scales with visible rows, not source size");
    list.clear_collection_presentation();
    list.select({1000000, 19});
    require(list.visible_items().end == source->size(), "Default reveal and uniform geometry remain unchanged");
    const auto scroll = list.offset();
    list.set_collection_presentation(std::make_shared<const CollectionPresentation>(source, 101, width, 40, 40000000,
        std::vector<CollectionBand>{{0, source->size(), {0, 0, width, 40}, 40, {0, 0, width, 40000000}}}));
    require(list.offset() == scroll && list.visible_content().size() <= 6,
        "Presentation publication preserves the numeric scroll offset with bounded large-index enumeration");
}
void invalid_frames() {
    Probe list; list.arrange({0, 0, 300, 180}); list.prepare();
    const double width = list.content_viewport().width;
    const auto construct = [&](std::vector<CollectionBand> bands, std::vector<OutgoingCollectionRow> exits = {}) {
        return std::make_shared<const CollectionPresentation>(list.next, 1, width, 40, 120, std::move(bands), std::move(exits));
    };
    rejects([&] { construct({{0, 1, {0, 0, width, 40}, 40, {0, 0, width, 40}}}); });
    rejects([&] { construct({{0, 2, {0, 0, width, 40}, 20, {0, 0, width, 80}}}); });
    rejects([&] { construct({{0, 2, {0, 0, width, 40}, 40, {0, 0, width, 80}}},
        {{list.frozen, {0, 20, width, 40}, {0, 20, width, 20}}}); });
    auto frame = list.frame(20);
    rejects([&] { frame->hit(std::numeric_limits<double>::quiet_NaN(), 1); });
    auto stale_row = list.frozen->row;
    stale_row.key = {11, 7};
    auto matching = std::make_shared<const FrozenCollectionRow>(FrozenCollectionRow{stale_row, list.frozen->visual});
    rejects([&] { construct({{0, 2, {0, 0, width, 40}, 40, {0, 0, width, 80}}},
        {{matching, {0, 80, width, 40}, {0, 80, width, 40}}}); });
    stale_row.key = {11, 6};
    matching = std::make_shared<const FrozenCollectionRow>(FrozenCollectionRow{stale_row, list.frozen->visual});
    const auto versioned = construct({{0, 2, {0, 0, width, 40}, 40, {0, 0, width, 80}}},
        {{matching, {0, 80, width, 40}, {0, 80, width, 40}}});
    require(versioned->outgoing().size() == 1, "Retired identity versions cannot alias a current logical row");
}
}
int main() {
    try {
        geometry_and_retirement(); bounded_enumeration(); invalid_frames();
        std::cout << "Collection presentation geometry, gaps, snapshots, bounded work, versions, and retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

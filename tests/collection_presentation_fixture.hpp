#pragma once
#include "../src/collection_presentation.hpp"
#include <stdexcept>

namespace collection_fixture {
using namespace xui;
using namespace xui::detail;
inline void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class Source final : public ItemsSource {
public:
    explicit Source(std::vector<ItemKey> keys, bool images = false) : keys_(std::move(keys)), images_(images) {
        for (std::size_t i = 0; i < keys_.size(); ++i) index_.emplace(keys_[i], i);
    }
    std::size_t size() const override { return keys_.size(); }
    ItemKey key(std::size_t i) const override { return keys_.at(i); }
    std::optional<std::size_t> find(ItemKey value) const override {
        const auto it = index_.find(value);
        return it == index_.end() ? std::nullopt : std::optional{it->second};
    }
    ItemContent item(std::size_t i) const override {
        ++reads;
        ItemContent result;
        result.primary = L"Row " + std::to_wstring(key(i).id);
        result.icon = key(i).id == 22 ? ButtonIcon::folder : ButtonIcon::home;
        return result;
    }
    ItemVisual visual(std::size_t i) const override {
        ++visual_reads;
        return {key(i).id == 22 ? ButtonIcon::folder : ButtonIcon::home,
            images_ ? L"snapshot-" + std::to_wstring(key(i).id) + L".png" : L""};
    }
    mutable std::size_t reads{}, visual_reads{};
private:
    std::vector<ItemKey> keys_;
    std::map<ItemKey, std::size_t> index_;
    bool images_{};
};
class Probe : public VirtualCollection {
public:
    Probe() : VirtualCollection(ControlRole::items_view, L"Presented collection") {
        set_item_size({180, 40});
        set_source(std::make_shared<Source>(std::vector<ItemKey>{{11, 7}, {22, 7}, {33, 7}}));
    }
    using VirtualCollection::set_source;
    using VirtualCollection::set_collection_presentation;
    using VirtualCollection::clear_collection_presentation;
    void prepare() {
        const auto rows = visible_content();
        for (const auto& row : rows) if (row.key == ItemKey{22, 7})
            frozen = CollectionPresentationAccess::freeze(*this, row);
        require(frozen != nullptr, "Capture the old visible row before logical replacement");
        next = std::make_shared<Source>(std::vector<ItemKey>{{11, 7}, {33, 7}});
    }
    std::shared_ptr<const CollectionPresentation> frame(double gap, double live_clip = 40) {
        const double width = content_viewport().width;
        return std::make_shared<const CollectionPresentation>(next, ++version, width, item_size().height, 80 + gap,
            std::vector<CollectionBand>{{0, 1, {0, 0, width, 40}, 40, {0, 0, width, 40}},
                {1, 1, {0, 40 + gap, width, 40}, 40, {0, 40 + gap, width, live_clip}}},
            std::vector<OutgoingCollectionRow>{{frozen, {0, 40, width, 40}, {0, 40, width, gap}}});
    }
    void present(double gap, double live_clip = 40) { set_source(next, {}, frame(gap, live_clip)); }
    std::shared_ptr<const FrozenCollectionRow> frozen;
    std::shared_ptr<const Source> next;
    std::uint64_t version{};
    unsigned retirements{};
protected:
    void collection_presentation_retired() override { ++retirements; }
};
}

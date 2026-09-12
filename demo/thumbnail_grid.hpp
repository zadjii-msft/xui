#pragma once

#include "xui/application.hpp"
#include "xui/image.hpp"
#include <array>
#include <algorithm>

namespace sample {

// A sample-specific layout uses only public controls. It retains ten recycled rows.
// The owning ScrollView has a maximum height of 960 DIPs.
class ThumbnailGrid final : public xui::Stack {
public:
    static constexpr std::size_t columns = 4, row_pool = 10, tile_count = columns * row_pool;
    static constexpr float row_height = 160;
    ThumbnailGrid() : Stack(xui::Axis::vertical) {
        for (std::size_t i = 0; i < tile_count; ++i) {
            auto tile = std::make_shared<xui::Stack>(xui::Axis::vertical);
            images[i] = std::make_shared<xui::Image>();
            images[i]->set_preferred_size({192, 128});
            labels[i] = std::make_shared<xui::Label>(L"");
            labels[i]->set_caption(true);
            labels[i]->set_preferred_size({192, 24});
            tile->add(images[i]);
            tile->add(labels[i]);
            add(tile);
        }
    }
    void set_view(std::shared_ptr<const xui::FilteredView> view) {
        view_ = std::move(view);
        for (auto& image : images) image->unload();
        invalidate(xui::Invalidation::layout);
    }
    void on_offset(std::function<float()> callback) { offset_ = std::move(callback); }
    std::size_t count() const { return view_ ? view_->indices().size() : 0; }
    xui::Size measure(xui::Size available) override {
        return {available.width, std::max(1.0f, static_cast<float>((count() + columns - 1) / columns) * row_height)};
    }
    void arrange(xui::Rect rect) override {
        xui::Element::arrange(rect);
        const float offset = offset_ ? offset_() : 0;
        const auto first = static_cast<std::size_t>(std::max(0.0f, offset / row_height));
        const auto begin = first ? first - 1 : 0;
        const float width = rect.width / columns;
        for (std::size_t slot = 0; slot < tile_count; ++slot) {
            const auto row = begin + (slot / columns + row_pool - begin % row_pool) % row_pool;
            const auto index = row * columns + slot % columns;
            const auto bounds = xui::Rect{rect.x + (slot % columns) * width,
                rect.y + row * row_height, std::max(0.0f, width - 8), index < count() ? row_height : 0};
            if (index < count()) {
                const auto& item = (*view_->source()->items())[view_->indices()[index]];
                images[slot]->set_name(item.name);
                images[slot]->set_source(item.path, {192, 128});
                labels[slot]->set_text(item.name);
            } else {
                images[slot]->unload();
                labels[slot]->set_text(L"");
            }
            child_at(slot)->measure({bounds.width, bounds.height});
            child_at(slot)->arrange(bounds);
        }
    }
    std::array<std::shared_ptr<xui::Image>, tile_count> images;
private:
    std::array<std::shared_ptr<xui::Label>, tile_count> labels;
    std::shared_ptr<const xui::FilteredView> view_;
    std::function<float()> offset_;
};
}

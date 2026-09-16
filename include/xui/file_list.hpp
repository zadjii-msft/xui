#pragma once

#include "xui/controls.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace xui {

enum class Navigation { previous, next, first, last, page_up, page_down };
enum class FileActivation { enter, double_click, command };

// Behavior and viewport state have no dependency on the window or drawing API.
class FileList : public Control {
public:
    explicit FileList(std::wstring name = L"Files") : Control(ControlRole::file_list, std::move(name), {320, 240}) {}
    ~FileList() override { if (dispose_) dispose_(model_.release_view()); }
    // Optional WIC previews and Shell thumbnails/icons. Pending or failed requests keep vector icons.
    void set_thumbnails(bool enabled) {
        if (thumbnails_ == enabled) return;
        thumbnails_ = enabled;
        reload_thumbnails();
    }
    bool thumbnails() const { return thumbnails_; }
    void reload_thumbnails() { ++thumbnail_revision_; invalidate(Invalidation::paint); }
    std::uint64_t thumbnail_revision() const { return thumbnail_revision_; }
    // Runs on the UI thread, once per failed visible request. Missing thumbnails use Shell icons.
    void on_thumbnail_error(std::function<void(ItemId, const std::wstring&)> callback) {
        thumbnail_error_ = std::move(callback);
    }
    // Backend lifetime boundary. Keeps final snapshot destruction off the UI thread.
    void set_disposer(std::function<void(std::shared_ptr<const FilteredView>)> dispose) { dispose_ = std::move(dispose); }
    void on_selection_change(std::function<void()> callback) { selection_change_ = std::move(callback); }
    void on_view_change(std::function<void()> callback) { view_change_ = std::move(callback); }
    void on_activate(std::function<void(const FileItem&, FileActivation)> callback) { activation_ = std::move(callback); }
    bool activate_selected(FileActivation reason = FileActivation::command) {
        if (!enabled() || !model_.selected_index() || !activation_) return false;
        const auto item = *model_.selected_item();
        const auto callback = activation_;
        callback(item, reason);
        return true;
    }
    void set_empty_text(std::wstring title, std::wstring detail) {
        empty_title_ = std::move(title); empty_detail_ = std::move(detail);
        invalidate(Invalidation::paint);
    }
    const std::wstring& empty_title() const { return empty_title_; }
    const std::wstring& empty_detail() const { return empty_detail_; }
    const FileListModel& model() const { return model_; }
    std::optional<size_t> focused_index() const {
        return focused_ ? model_.find_visible(*focused_) : std::nullopt;
    }
    std::optional<ItemId> focused_id() const { return focused_; }
    float row_height() const;
    float scrollbar_width() const;
    Rect content_viewport(float width) const;
    float content_height() const { return content_viewport(0).height; }
    float offset() const {
        return clamp_scroll(model_.visible_indices().size(), row_height(), offset_, content_height());
    }
    float viewport_height() const { return viewport_height_; }
    void set_viewport_height(float height) {
        viewport_height_ = height > 0 ? std::min(height, std::numeric_limits<float>::max()) : 0;
        scroll_to(offset_);
    }
    VisibleRange visible_rows() const {
        return visible_range(model_.visible_indices().size(), row_height(), offset(), content_height());
    }
    void set_items(std::shared_ptr<const std::vector<FileItem>> items) {
        auto previous = model_.view();
        model_.set_items(std::move(items));
        if (dispose_) dispose_(std::move(previous));
        if (focused_ && !model_.view()->source()->find(*focused_)) focused_.reset();
        scroll_to(offset_);
        invalidate(Invalidation::paint);
        notify_view();
    }
    void set_filter(std::wstring filter) {
        auto previous = model_.view();
        model_.set_filter(std::move(filter));
        if (dispose_) dispose_(std::move(previous));
        offset_ = 0;
        invalidate(Invalidation::paint);
        notify_view();
    }
    void set_view(std::shared_ptr<const FilteredView> view) {
        if (!view) throw std::invalid_argument("Filtered view must not be null");
        const bool changed_query = view->query() != model_.view()->query();
        auto previous = model_.view();
        model_.set_view(std::move(view));
        if (dispose_) dispose_(std::move(previous));
        if (focused_ && !model_.view()->source()->find(*focused_)) focused_.reset();
        scroll_to(changed_query ? 0 : offset_);
        invalidate(Invalidation::paint);
        notify_view();
    }
    void scroll_to(float offset) {
        offset_ = clamp_scroll(model_.visible_indices().size(), row_height(), offset, content_height());
        invalidate(Invalidation::paint);
    }
    void reveal(size_t index) {
        scroll_to(reveal_row(index, row_height(), offset(), content_height()));
    }
    void select(size_t index, bool ensure_visible = true) {
        model_.select_index(index);
        focused_ = model_.selected_id();
        if (ensure_visible) reveal(index);
        invalidate(Invalidation::paint);
        notify_selection();
    }
    void clear_selection() {
        model_.clear_selection();
        invalidate(Invalidation::paint);
        notify_selection();
    }
    void focus_item(size_t index) {
        if (index >= model_.visible_indices().size()) throw std::out_of_range("Invalid focus row");
        focused_ = (*model_.items())[model_.visible_indices()[index]].id;
        reveal(index);
        invalidate(Invalidation::paint);
    }
    void focus_list() {
        focused_.reset();
        invalidate(Invalidation::paint);
    }
    void restore_state(std::optional<ItemId> selected, std::optional<ItemId> focused, float offset) {
        model_.selected_ = selected && model_.view()->source()->find(*selected) ? selected : std::nullopt;
        focused_ = focused && model_.view()->source()->find(*focused) ? focused : std::nullopt;
        scroll_to(offset);
        notify_selection();
    }
    void navigate(Navigation navigation) {
        const int page = static_cast<int>(std::clamp(
            static_cast<double>(content_height()) / row_height(), 1.0,
            static_cast<double>(std::numeric_limits<int>::max())));
        if (const auto focused = focused_index()) model_.select_index(*focused);
        switch (navigation) {
        case Navigation::previous: model_.move_selection(-1); break;
        case Navigation::next: model_.move_selection(1); break;
        case Navigation::first: model_.select_first(); break;
        case Navigation::last: model_.select_last(); break;
        case Navigation::page_up: model_.move_selection(-page); break;
        case Navigation::page_down: model_.move_selection(page); break;
        }
        focused_ = model_.selected_id();
        if (const auto selected = model_.selected_index()) reveal(*selected);
        invalidate(Invalidation::paint);
        notify_selection();
    }
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::file_list; }
    StyleStateMask control_style_state_bits() const override {
        return Control::control_style_state_bits() & (style_states::focused | style_states::hovered | style_states::disabled);
    }
private:
    friend class ListPeer;
    void thumbnail_error(ItemId id, const std::wstring& error) {
        if (thumbnail_error_) { auto callback = thumbnail_error_; callback(id, error); }
    }
    void notify_view() { if (view_change_) { auto callback = view_change_; callback(); } }
    void notify_selection() { if (selection_change_) { auto callback = selection_change_; callback(); } }
    std::function<void()> selection_change_, view_change_;
    std::function<void(const FileItem&, FileActivation)> activation_;
    std::function<void(std::shared_ptr<const FilteredView>)> dispose_;
    std::function<void(ItemId, const std::wstring&)> thumbnail_error_;
    std::uint64_t thumbnail_revision_{1};
    bool thumbnails_{};
    std::wstring empty_title_{L"No items"}, empty_detail_;
    FileListModel model_;
    std::optional<ItemId> focused_;
    float offset_{};
    float viewport_height_{};
};

}

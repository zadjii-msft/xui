#include "xui/file_list_model.hpp"
#include <algorithm>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace xui {
namespace {
struct Cancelled {};
void checkpoint(const CancelCheck& cancel) {
    if (cancel && cancel()) throw Cancelled{};
}
}

std::shared_ptr<const FileSnapshot> FileSnapshot::build(
    std::shared_ptr<const std::vector<FileItem>> items, const CancelCheck& cancel) {
    try {
        checkpoint(cancel);
        auto result = std::make_shared<FileSnapshot>();
        result->items_ = items ? std::move(items) : std::make_shared<const std::vector<FileItem>>();
        const auto count = result->items_->size();
        if (count > std::numeric_limits<RowIndex>::max())
            throw std::length_error("File snapshot exceeds the 32-bit row limit");
        result->by_id_.reserve(count);
        result->name_offsets_.reserve(count + 1);
        std::size_t name_units{};
        for (std::size_t index = 0; index < count; ++index) {
            if ((index & 255) == 0) checkpoint(cancel);
            const auto size = (*result->items_)[index].name.size();
            if (size > result->names_.max_size() - name_units)
                throw std::length_error("File names exceed the snapshot storage limit");
            name_units += size;
        }
        result->names_.reserve(name_units);
        std::size_t characters{};
        for (std::size_t index = 0; index < count; ++index) {
            if ((index & 255) == 0) checkpoint(cancel);
            result->by_id_.push_back(static_cast<RowIndex>(index));
            result->name_offsets_.push_back(result->names_.size());
            for (wchar_t character : (*result->items_)[index].name) {
                if ((characters++ & 4095) == 0) checkpoint(cancel);
                result->names_.push_back(static_cast<wchar_t>(std::towlower(character)));
            }
        }
        result->name_offsets_.push_back(result->names_.size());
        std::size_t comparisons{};
        std::sort(result->by_id_.begin(), result->by_id_.end(), [&](RowIndex a, RowIndex b) {
            if ((comparisons++ & 4095) == 0) checkpoint(cancel);
            return (*result->items_)[a].id < (*result->items_)[b].id;
        });
        for (std::size_t i = 1; i < count; ++i) {
            if ((i & 255) == 0) checkpoint(cancel);
            if ((*result->items_)[result->by_id_[i - 1]].id == (*result->items_)[result->by_id_[i]].id)
                throw std::invalid_argument("File item IDs must be unique");
        }
        checkpoint(cancel);
        return result;
    } catch (const Cancelled&) { return {}; }
}

std::optional<RowIndex> FileSnapshot::find(ItemId id) const noexcept {
    const auto found = std::lower_bound(by_id_.begin(), by_id_.end(), id,
        [&](RowIndex index, ItemId value) { return (*items_)[index].id < value; });
    if (found == by_id_.end() || (*items_)[*found].id != id) return {};
    return *found;
}

std::wstring_view FileSnapshot::folded_name(RowIndex index) const noexcept {
    return {names_.data() + name_offsets_[index], name_offsets_[index + 1] - name_offsets_[index]};
}

std::size_t FileSnapshot::index_bytes() const noexcept {
    return by_id_.capacity() * sizeof(RowIndex) + name_offsets_.capacity() * sizeof(std::size_t)
        + names_.capacity() * sizeof(wchar_t);
}

std::shared_ptr<const FilteredView> FilteredView::build(
    std::shared_ptr<const FileSnapshot> source, std::wstring query, const CancelCheck& cancel) {
    if (!source) throw std::invalid_argument("Filter source must not be null");
    try {
        checkpoint(cancel);
        auto result = std::make_shared<FilteredView>();
        result->source_ = std::move(source);
        for (std::size_t i = 0; i < query.size(); ++i) {
            if ((i & 4095) == 0) checkpoint(cancel);
            query[i] = static_cast<wchar_t>(std::towlower(query[i]));
        }
        result->query_ = std::move(query);
        const auto count = result->source_->items()->size();
        // Sparse results allocate only for matches. An empty query has an exact-sized array.
        if (result->query_.empty()) result->indices_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            if ((i & 255) == 0) checkpoint(cancel);
            if (result->query_.empty() ||
                result->source_->folded_name(static_cast<RowIndex>(i)).find(result->query_) != std::wstring_view::npos)
                result->indices_.push_back(static_cast<RowIndex>(i));
        }
        checkpoint(cancel);
        return result;
    } catch (const Cancelled&) { return {}; }
}

std::optional<std::size_t> FilteredView::find(ItemId id) const noexcept {
    const auto underlying = source_->find(id);
    if (!underlying) return {};
    const auto found = std::lower_bound(indices_.begin(), indices_.end(), *underlying);
    if (found == indices_.end() || *found != *underlying) return {};
    return static_cast<std::size_t>(found - indices_.begin());
}

void FileListModel::set_items(std::shared_ptr<const std::vector<FileItem>> items) {
    set_view(FilteredView::build(FileSnapshot::build(std::move(items)), view_->query()));
}

void FileListModel::set_filter(std::wstring filter) {
    set_view(FilteredView::build(view_->source(), std::move(filter)));
}

void FileListModel::set_view(std::shared_ptr<const FilteredView> view) {
    if (!view) throw std::invalid_argument("Filtered view must not be null");
    view_ = std::move(view);
    if (selected_ && !view_->source()->find(*selected_)) selected_.reset();
}

const std::vector<RowIndex>& FileListModel::visible_indices() const { return view_->indices(); }
std::shared_ptr<const std::vector<FileItem>> FileListModel::items() const { return view_->source()->items(); }
std::optional<ItemId> FileListModel::selected_id() const { return selected_; }

std::optional<std::size_t> FileListModel::selected_index() const {
    return selected_ ? view_->find(*selected_) : std::nullopt;
}

const FileItem* FileListModel::selected_item() const {
    if (selected_)
        if (const auto index = view_->source()->find(*selected_)) return &(*items())[*index];
    return nullptr;
}

void FileListModel::select_index(std::size_t index) {
    if (index < visible_indices().size()) selected_ = (*items())[visible_indices()[index]].id;
}

void FileListModel::clear_selection() { selected_.reset(); }

void FileListModel::move_selection(int delta) {
    const auto& visible = visible_indices();
    if (delta == 0 || visible.empty()) return;
    const auto current = selected_index();
    if (!current) {
        select_index(delta > 0 ? 0 : visible.size() - 1);
        return;
    }
    if (delta > 0) {
        const auto distance = static_cast<std::size_t>(delta);
        select_index(*current + (std::min)(distance, visible.size() - 1 - *current));
    } else {
        const auto distance = static_cast<std::size_t>(-static_cast<std::int64_t>(delta));
        select_index(*current - (std::min)(distance, *current));
    }
}

void FileListModel::select_first() { select_index(0); }
void FileListModel::select_last() {
    if (!visible_indices().empty()) select_index(visible_indices().size() - 1);
}

} // namespace xui

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xui {
using ItemId = std::uint64_t;

struct FileItem {
    ItemId id;
    std::wstring name;
    std::wstring path;
    bool directory;
};

using RowIndex = std::uint32_t;
using CancelCheck = std::function<bool()>;

// Builders run off the UI thread. A null result means cancellation, not an empty list.
// Inputs transfer ownership: do not retain a mutable alias to the item collection.
class FileSnapshot {
public:
    static std::shared_ptr<const FileSnapshot> build(
        std::shared_ptr<const std::vector<FileItem>> items, const CancelCheck& cancel = {});
    const std::shared_ptr<const std::vector<FileItem>>& items() const { return items_; }
    std::optional<RowIndex> find(ItemId id) const noexcept;
    std::wstring_view folded_name(RowIndex index) const noexcept;
    std::size_t index_bytes() const noexcept;
private:
    std::shared_ptr<const std::vector<FileItem>> items_;
    std::vector<RowIndex> by_id_;
    std::vector<std::size_t> name_offsets_;
    std::wstring names_;
};

class FilteredView {
public:
    static std::shared_ptr<const FilteredView> build(std::shared_ptr<const FileSnapshot> source,
        std::wstring query, const CancelCheck& cancel = {});
    const std::shared_ptr<const FileSnapshot>& source() const { return source_; }
    const std::vector<RowIndex>& indices() const { return indices_; }
    const std::wstring& query() const { return query_; }
    std::optional<std::size_t> find(ItemId id) const noexcept;
private:
    std::shared_ptr<const FileSnapshot> source_;
    std::wstring query_;
    std::vector<RowIndex> indices_;
};

struct SourceResult {
    std::shared_ptr<const FileSnapshot> source;
    std::wstring error;
};
struct ViewResult {
    std::uint64_t generation{};
    std::shared_ptr<const FilteredView> view;
    std::wstring error;
};

class FileListModel {
public:
    // Item IDs must be unique. Null is treated as an empty immutable collection.
    // These synchronous conveniences are for small collections and pure callers.
    // UI hosts use worker-built set_view instead.
    void set_items(std::shared_ptr<const std::vector<FileItem>> items);
    // Name substring matching uses towlower in the current C locale. This prototype
    // does not implement Unicode case folding or locale-independent collation.
    void set_filter(std::wstring filter);
    void set_view(std::shared_ptr<const FilteredView> view);
    const std::shared_ptr<const FilteredView>& view() const { return view_; }
    std::optional<std::size_t> find_visible(ItemId id) const { return view_->find(id); }
    const std::vector<RowIndex>& visible_indices() const;
    std::shared_ptr<const std::vector<FileItem>> items() const;
    std::optional<ItemId> selected_id() const;
    std::optional<std::size_t> selected_index() const;
    const FileItem* selected_item() const;
    // Index arguments and selected_index refer to positions in visible_indices.
    // Out-of-range selection is ignored. Hidden selections keep their identity.
    void select_index(std::size_t index);
    void clear_selection();
    void move_selection(int delta);
    void select_first();
    void select_last();

private:
    friend class FileList;
    std::shared_ptr<const FilteredView> release_view() { return std::move(view_); }
    std::shared_ptr<const FilteredView> view_{FilteredView::build(FileSnapshot::build(nullptr), {})};
    std::optional<ItemId> selected_;
};

} // namespace xui

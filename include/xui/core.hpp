#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <limits>

namespace xui {

using ItemId = std::uint64_t;

struct Size { float width{}, height{}; };
struct Point { float x{}, y{}; };
struct Rect { float x{}, y{}, width{}, height{}; };
struct Insets { float left{}, top{}, right{}, bottom{}; };

enum class Invalidation { paint, layout };

// Layout invalidation also requires a repaint. Dimensions are finite and nonnegative.
class Element {
public:
    Element();
    virtual ~Element();
    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;
    Element(Element&&) = delete;
    Element& operator=(Element&&) = delete;

    std::uint64_t id() const;
    virtual Size measure(Size available);
    virtual void arrange(Rect bounds);
    Rect bounds() const;
    void set_invalidator(std::function<void(Invalidation)> callback);
    void invalidate(Invalidation kind);
    void set_preferred_size(Size size);
    void set_fixed_size(Size size);
    void set_auto_size(bool value);
    bool auto_size() const { return auto_size_; }
    void set_minimum_size(Size size);
    void set_maximum_size(Size size);

protected:
    Size constrain(Size desired, Size available) const;
    void adopt(const std::shared_ptr<Element>& child);

private:
    struct InvalidationState;
    friend class Stack;
    std::uint64_t id_;
    Size preferred_{};
    Size minimum_{};
    Size maximum_{(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()};
    bool auto_size_{};
    Rect bounds_{};
    std::shared_ptr<InvalidationState> invalidation_;
};

enum class Axis { horizontal, vertical };

class Stack : public Element {
public:
    explicit Stack(Axis axis);
    void set_spacing(float spacing);
    void set_padding(Insets padding);
    bool surface() const { return surface_; }
    void set_surface(bool value) { surface_ = value; invalidate(Invalidation::paint); }
    bool separator_after() const { return separator_after_; }
    void set_separator_after(bool value) { separator_after_ = value; invalidate(Invalidation::paint); }
    // Children have one layout parent. Null, duplicate, and already-parented children
    // are rejected. Nonpositive or nonfinite flex means a fixed-size child.
    void add(std::shared_ptr<Element> child, float flex = 0);
    std::size_t child_count() const { return children_.size(); }
    const std::shared_ptr<Element>& child_at(std::size_t index) const { return children_.at(index).element; }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;

private:
    struct Child {
        std::shared_ptr<Element> element;
        float flex{};
    };
    Axis axis_;
    float spacing_{};
    Insets padding_{};
    bool surface_{};
    bool separator_after_{};
    std::vector<Child> children_;
    std::vector<Size> layout_children(Size available);
};

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

// One worker, one pending request, one result. Refresh cancels source work.
// Query changes cancel only filtering, so typing cannot restart directory I/O.
class ViewWorker {
public:
    using Loader = std::function<SourceResult(const CancelCheck&)>;
    // The notification runs under the mailbox lock. It must not call back into this worker.
    ViewWorker(Loader loader, std::function<void()> ready);
    ~ViewWorker();
    void start();
    std::uint64_t request(std::wstring query, bool refresh = false);
    std::optional<ViewResult> take_result();
    void retire(std::shared_ptr<const FilteredView> view);
    bool busy() const { return busy_.load(); }
    void request_stop();
    void join();
    std::jthread::native_handle_type native_handle() { return thread_.native_handle(); }
    bool joinable() const { return thread_.joinable(); }
private:
    void run(std::stop_token stop);
    Loader loader_;
    std::function<void()> ready_;
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::atomic<std::uint64_t> generation_{}, source_generation_{};
    std::atomic<bool> busy_{};
    std::wstring query_;
    std::optional<ViewResult> result_;
    std::vector<std::shared_ptr<const FilteredView>> retired_;
    bool stopped_{};
    std::jthread thread_;
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

struct VisibleRange { std::size_t begin{}, end{}; };

// Ranges are half-open. Invalid row heights and empty viewports return no rows.
// Negative/NaN dimensions and offsets become zero; positive infinity saturates.
VisibleRange visible_range(std::size_t count, float row_height, float offset,
    float viewport_height, std::size_t overscan = 2);
float clamp_scroll(std::size_t count, float row_height, float offset, float viewport_height);
float reveal_row(std::size_t index, float row_height, float offset, float viewport_height);

} // namespace xui

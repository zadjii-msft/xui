#pragma once
#include "xui/controls.hpp"
#include <map>
#include <set>
#include <stop_token>
#include <compare>

namespace xui {
namespace detail { class CollectionPresentation; struct CollectionPresentationAccess; }

struct ItemKey {
    std::uint64_t id{}, version{};
    auto operator<=>(const ItemKey&) const = default;
};

// Immutable, thread-safe indexes own their identity namespace. Lookup must not enumerate rows.
class CollectionIndex {
public:
    virtual ~CollectionIndex() = default;
    virtual std::size_t size() const = 0;
    virtual ItemKey key(std::size_t index) const = 0;
    virtual std::optional<std::size_t> find(ItemKey key) const = 0;
    // Optional constant-cost proof for filtered or reordered snapshots in the same domain.
    virtual bool contains_all(const CollectionIndex& other) const { return this == &other; }
    virtual bool selectable(std::size_t) const { return true; }
};

enum class SelectAllScope { filtered, full_source };
enum class SelectionGesture { replace, toggle, extend, add_range, focus_only };
enum class SelectionState { none, mixed, all };

// Range and rectangle terms retain immutable indexes, not one identity per selected row.
// A replacement gesture discards old terms. The explicit term limit rejects further additions.
class CollectionSelection {
public:
    static constexpr std::size_t maximum_terms = 4096;
    bool contains(ItemKey key) const;
    bool empty() const { return terms_.empty(); }
    std::size_t storage_size() const { return terms_.size(); }
    std::optional<ItemKey> focused() const { return focus_; }
    std::optional<ItemKey> anchor() const { return anchor_; }
    void set_focus(std::optional<ItemKey> key) { focus_ = key; }
    void clear();
    void set(ItemKey key, bool selected);
    void select(std::shared_ptr<const CollectionIndex> index, ItemKey key, SelectionGesture gesture);
    void range(std::shared_ptr<const CollectionIndex> index, std::size_t first, std::size_t last, bool additive = false);
    void rectangle(std::shared_ptr<const CollectionIndex> index, std::size_t first, std::size_t last,
        std::size_t columns, bool additive = false);
    void select_all(std::shared_ptr<const CollectionIndex> filtered, SelectAllScope scope,
        std::shared_ptr<const CollectionIndex> full = {});
    SelectionState state(const std::shared_ptr<const CollectionIndex>& index,
        const std::shared_ptr<const CollectionIndex>& full_source = {}) const;
    std::optional<std::vector<ItemKey>> selected_keys(const std::shared_ptr<const CollectionIndex>& index, std::size_t limit = 256) const;
    std::uint64_t revision() const { return revision_; }
    bool operator==(const CollectionSelection& other) const;
private:
    struct Term {
        std::shared_ptr<const CollectionIndex> index;
        ItemKey key{};
        std::size_t first{}, last{}, columns{}, left{}, right{};
        bool selected{true};
    };
    void append(Term term, bool additive);
    std::vector<Term> terms_;
    std::optional<ItemKey> focus_, anchor_;
    std::uint64_t revision_{};
};

// Metadata only. Visible rows schedule image work on the shared decode workers.
struct ItemVisual {
    ButtonIcon icon{ButtonIcon::none};
    std::wstring image_path;
};
struct ItemContent {
    std::wstring primary, secondary;
    ButtonIcon icon{ButtonIcon::none};
    std::optional<double> progress;
    std::wstring action;
    bool enabled{true};
    std::optional<bool> checked;
    bool separator{};
    bool submenu{};
    std::wstring image_path;
};
struct ItemGroup {
    ItemKey key;
    std::wstring name;
    std::size_t first{}, count{};
};
struct GridColumn {
    std::wstring name;
    float width{120};
    bool numeric{};
    bool filterable{}, checkable{};
    bool operator==(const GridColumn&) const = default;
};
struct ItemHierarchy {
    std::optional<ItemKey> parent;
    std::size_t depth{};
    bool group{}, expandable{}, expanded{}, pending{};
    std::size_t position{}, count{};
    bool error{};
};
enum class CollectionNavigation { parent, first_child, last_child, next, previous };
class ItemsSource : public CollectionIndex {
public:
    virtual ItemContent item(std::size_t index) const = 0;
    // Nonblocking text access for optional details columns. Column zero is the item name.
    virtual std::wstring cell(std::size_t index, std::size_t column) const {
        return column == 0 ? item(index).primary : std::wstring{};
    }
    virtual ItemVisual visual(std::size_t) const { return {}; }
    // List geometry shared by painting, hit testing, scrolling, and UIA. size() is the end boundary.
    virtual double row_start(std::size_t index, double row_height) const;
    virtual std::size_t row_at(double offset, double row_height) const;
    virtual std::vector<ItemGroup> groups() const { return {}; }
    virtual ItemHierarchy hierarchy(std::size_t) const { return {}; }
    virtual std::optional<std::size_t> navigate(std::optional<std::size_t> row, CollectionNavigation direction) const;
};

enum class ItemsPresentation { list, tiles, grouped, gallery };
struct CollectionGalleryLayout {
    Rect image, primary, secondary;
};
struct CollectionRow {
    ItemKey key;
    ItemContent content;
    Rect bounds;
    std::size_t index{}, depth{};
    std::optional<ItemKey> parent;
    bool group{}, expandable{}, expanded{}, pending{};
    bool navigation{}, compact{}, selected_descendant{}, hovered{}, error{};
    std::vector<std::wstring> cells;
};
// The caller supplies actual peer focus and pointer state, never container hover.
StyleStateMask collection_row_style_state(const CollectionRow& row, bool selected, bool focused,
    bool enabled, bool hovered = false);
class VirtualCollection : public Control {
public:
    virtual bool multiple_selection() const { return true; }
    const std::shared_ptr<const ItemsSource>& source() const { return source_; }
    const CollectionSelection& selection() const { return selection_; }
    void set_selection(CollectionSelection selection);
    void set_select_all_scope(SelectAllScope scope) { scope_ = scope; }
    SelectAllScope select_all_scope() const { return scope_; }
    virtual void select_all();
    virtual bool select(ItemKey key, SelectionGesture gesture = SelectionGesture::replace);
    void select_rectangle(ItemKey first, ItemKey last, bool additive = false);
    virtual void step(int delta, SelectionGesture gesture = SelectionGesture::replace);
    virtual void edge(bool last, SelectionGesture gesture = SelectionGesture::replace);
    void activate_item(ItemKey key, bool inline_action = false);
    void on_selection(std::function<void()> callback) { change_ = std::move(callback); }
    void on_activate(std::function<void(ItemKey)> callback) { activate_ = std::move(callback); }
    void on_action(std::function<void(ItemKey)> callback) { action_ = std::move(callback); }
    virtual void set_presentation(ItemsPresentation value);
    ItemsPresentation presentation() const { return presentation_; }
    bool wraps_items() const { return presentation_ == ItemsPresentation::tiles || presentation_ == ItemsPresentation::gallery; }
    void set_item_size(Size size);
    Size item_size() const;
    float scrollbar_width() const;
    Rect content_viewport() const;
    PartStyleValues row_style_values(StylePart part, const CollectionRow& row, StyleStateMask state) const;
    std::size_t columns() const;
    double offset() const { return std::min(offset_, maximum_offset()); }
    double maximum_offset() const;
    void set_offset(double offset);
    void reveal(ItemKey key);
    Rect item_bounds(std::size_t index) const;
    // Shared DIP geometry for gallery painting and asynchronous image requests.
    CollectionGalleryLayout gallery_layout(const CollectionRow& row, StyleStateMask state = 0) const;
    Rect disclosure_bounds(const CollectionRow& row, bool hovered = false) const;
    bool disclosure_hit(std::size_t index, Point point) const;
    std::optional<std::size_t> hit_test(Point point) const;
    // An envelope only when an internal frame clips logical ranges. Enumerate through visible_content().
    VisibleRange visible_items() const;
    virtual std::vector<CollectionRow> visible_content() const;
    virtual bool disclose(ItemKey key, bool expanded);
    virtual void horizontal(bool right, SelectionGesture gesture);
    Rect thumb() const;
    void arrange(Rect bounds) override;
    void cancel() override;
    static constexpr float bar_width = 12;
protected:
    VirtualCollection(ControlRole role, std::wstring name);
    std::optional<StyleTarget> control_style_target() const override;
    StyleStateMask control_style_state_bits() const override;
    void set_source(std::shared_ptr<const ItemsSource> source, std::shared_ptr<const CollectionIndex> full = {},
        std::shared_ptr<const detail::CollectionPresentation> presentation = {});
    void set_collection_presentation(std::shared_ptr<const detail::CollectionPresentation> presentation);
    void set_collection_presentation_offset(double offset);
    void clear_collection_presentation();
    // An internal animated consumer stops its clock when user navigation or layout retires a frame.
    virtual void collection_presentation_retired() {}
    void changed();
    void repair_focus();
    std::shared_ptr<const ItemsSource> source_;
    std::shared_ptr<const CollectionIndex> full_;
    CollectionSelection selection_;
private:
    friend struct detail::CollectionPresentationAccess;
    std::shared_ptr<const detail::CollectionPresentation> collection_presentation_;
    std::uint64_t collection_presentation_version_{};
    ItemsPresentation presentation_{};
    SelectAllScope scope_{SelectAllScope::filtered};
    Size item_size_{180, 56};
    double offset_{};
    std::function<void()> change_;
    std::function<void(ItemKey)> activate_, action_;
};

class ItemsView final : public VirtualCollection {
public:
    explicit ItemsView(std::wstring name = L"Items") : VirtualCollection(ControlRole::items_view, std::move(name)) {}
    // In this mode, secondary text contains '+'-separated shortcut keys, not a subtitle.
    void set_trailing_shortcut_badges(bool value) {
        if (trailing_shortcut_badges_ == value) return;
        trailing_shortcut_badges_ = value; invalidate(Invalidation::paint);
    }
    bool trailing_shortcut_badges() const { return trailing_shortcut_badges_; }
    void set_items(std::shared_ptr<const ItemsSource> source, std::shared_ptr<const CollectionIndex> full = {});
    void select_all() override;
    bool disclose(ItemKey group, bool expanded) override;
    std::vector<CollectionRow> visible_content() const override;
    void set_presentation(ItemsPresentation value) override;
private:
    bool trailing_shortcut_badges_{};
    void rebuild();
    std::shared_ptr<const ItemsSource> items_;
    std::vector<ItemGroup> groups_;
    std::set<ItemKey> collapsed_;
};

struct TreeRequest {
    ItemKey node;
    std::uint64_t generation{};
    std::stop_token cancellation;
};
struct TreeDetailsLayout {
    std::vector<Rect> columns;
    Rect disclosure, icon, name;
};
// Data access is cached and nonblocking. The request callback starts application-owned I/O.
class TreeSource {
public:
    virtual ~TreeSource() = default;
    virtual std::shared_ptr<const ItemsSource> roots() const = 0;
    virtual bool has_children(ItemKey node) const = 0;
};
class TreeView final : public VirtualCollection {
public:
    explicit TreeView(std::wstring name = L"Tree");
    ~TreeView() override;
    void set_tree(std::shared_ptr<const TreeSource> source);
    // Empty columns restore ordinary tree rows. Metadata columns are headless and read-only.
    void set_columns(std::vector<GridColumn> columns);
    const std::vector<GridColumn>& detail_columns() const { return detail_columns_; }
    TreeDetailsLayout details_layout(const CollectionRow& row, StyleStateMask state = 0) const;
    void on_request(std::function<void(TreeRequest)> callback) { request_ = std::move(callback); }
    // UI-thread delivery only. False means stale, canceled, or no longer owned by this tree.
    bool complete(TreeRequest request, std::shared_ptr<const ItemsSource> children, std::wstring error = {});
    bool disclose(ItemKey node, bool expanded) override;
    void horizontal(bool right, SelectionGesture gesture) override;
    std::vector<CollectionRow> visible_content() const override;
    void cancel() override;
    bool expanded(ItemKey node) const;
    std::size_t retained_branches() const { return branches_.size(); }
    static constexpr std::size_t maximum_branches = 4096, maximum_depth = 128;
private:
    struct Branch {
        std::optional<ItemKey> parent;
        std::shared_ptr<const ItemsSource> children;
        std::stop_source stop;
        std::uint64_t generation{};
        std::wstring error;
        bool open{}, pending{};
    };
    void rebuild();
    bool descendant(ItemKey key, ItemKey ancestor) const;
    std::shared_ptr<const TreeSource> tree_;
    std::vector<GridColumn> detail_columns_;
    std::map<ItemKey, Branch> branches_;
    std::uint64_t generation_{};
    std::function<void(TreeRequest)> request_;
};

}

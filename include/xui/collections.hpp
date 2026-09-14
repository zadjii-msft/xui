#pragma once
#include "xui/controls.hpp"
#include <map>
#include <set>
#include <stop_token>
#include <compare>

namespace xui {

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

struct ItemContent {
    std::wstring primary, secondary;
    ButtonIcon icon{ButtonIcon::none};
    std::optional<double> progress;
    std::wstring action;
    bool enabled{true};
    std::optional<bool> checked;
    bool separator{};
    bool submenu{};
};
struct ItemGroup {
    ItemKey key;
    std::wstring name;
    std::size_t first{}, count{};
};
struct ItemHierarchy {
    std::optional<ItemKey> parent;
    std::size_t depth{};
    bool group{}, expandable{}, expanded{}, pending{};
    std::size_t position{}, count{};
};
enum class CollectionNavigation { parent, first_child, last_child, next, previous };
class ItemsSource : public CollectionIndex {
public:
    virtual ItemContent item(std::size_t index) const = 0;
    // List geometry shared by painting, hit testing, scrolling, and UIA. size() is the end boundary.
    virtual double row_start(std::size_t index, double row_height) const;
    virtual std::size_t row_at(double offset, double row_height) const;
    virtual std::vector<ItemGroup> groups() const { return {}; }
    virtual ItemHierarchy hierarchy(std::size_t) const { return {}; }
    virtual std::optional<std::size_t> navigate(std::optional<std::size_t> row, CollectionNavigation direction) const;
};

enum class ItemsPresentation { list, tiles, grouped };
struct CollectionRow {
    ItemKey key;
    ItemContent content;
    Rect bounds;
    std::size_t index{}, depth{};
    std::optional<ItemKey> parent;
    bool group{}, expandable{}, expanded{}, pending{};
    bool navigation{}, compact{}, selected_descendant{}, hovered{};
};
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
    void set_item_size(Size size);
    Size item_size() const { return item_size_; }
    std::size_t columns() const;
    double offset() const { return offset_; }
    double maximum_offset() const;
    void set_offset(double offset);
    void reveal(ItemKey key);
    Rect item_bounds(std::size_t index) const;
    std::optional<std::size_t> hit_test(Point point) const;
    VisibleRange visible_items() const;
    virtual std::vector<CollectionRow> visible_content() const;
    virtual bool disclose(ItemKey key, bool expanded);
    virtual void horizontal(bool right, SelectionGesture gesture);
    Rect thumb() const;
    void arrange(Rect bounds) override;
    static constexpr float bar_width = 12;
protected:
    VirtualCollection(ControlRole role, std::wstring name);
    void set_source(std::shared_ptr<const ItemsSource> source, std::shared_ptr<const CollectionIndex> full = {});
    void changed();
    void repair_focus();
    std::shared_ptr<const ItemsSource> source_;
    std::shared_ptr<const CollectionIndex> full_;
    CollectionSelection selection_;
private:
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
    void set_items(std::shared_ptr<const ItemsSource> source, std::shared_ptr<const CollectionIndex> full = {});
    void select_all() override;
    bool disclose(ItemKey group, bool expanded) override;
    std::vector<CollectionRow> visible_content() const override;
    void set_presentation(ItemsPresentation value) override;
private:
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
    std::map<ItemKey, Branch> branches_;
    std::uint64_t generation_{};
    std::function<void(TreeRequest)> request_;
};

}

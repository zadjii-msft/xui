#pragma once
#include "controls.hpp"

namespace xui {
struct RetainedPageEntry {
    std::uint64_t id{};
    std::wstring title;
    bool enabled{true};
    bool operator==(const RetainedPageEntry&) const = default;
};

class RetainedPages final : public Control {
public:
    explicit RetainedPages(std::wstring name = L"Pages");
    static void validate_entries(const std::vector<RetainedPageEntry>& entries, std::uint64_t selected);
    const std::vector<RetainedPageEntry>& entries() const { return entries_; }
    std::uint64_t selected() const { return selected_; }
    std::size_t page_count() const { return children_.size(); }
    std::uint64_t page_id(std::size_t index) const { return ids_.at(index); }
    const std::shared_ptr<Element>& page_root(std::size_t index) const;
    std::size_t index_of(const Element& child) const;
    void validate_order(const std::vector<RetainedPageEntry>& entries) const;
    void validate_complete() const;
    void set_entries(std::vector<RetainedPageEntry> entries, std::uint64_t selected, bool complete);
    void insert(std::size_t index, std::uint64_t id, std::shared_ptr<Element> child);
    void remove(const Element& child);
    void move(const Element& child, std::size_t index);
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    bool supports_axis_constraints() const override { return true; }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::content_view; }
private:
    void sync_visibility();
    std::vector<RetainedPageEntry> entries_;
    std::vector<std::uint64_t> ids_;
    std::vector<std::shared_ptr<Element>> children_;
    std::uint64_t selected_{};
};
}

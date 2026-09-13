#pragma once
#include "xui/collections.hpp"
#include "xui/data_grid.hpp"
#include <atomic>
#include <stdexcept>

namespace collections_test {
inline void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class Items final : public xui::ItemsSource {
public:
    std::size_t count;
    std::uint64_t first, stride;
    mutable std::atomic<std::size_t> reads{}, lookups{}, keys{};
    explicit Items(std::size_t count = 1000000, std::uint64_t first = 1, std::uint64_t stride = 1) : count(count), first(first), stride(stride) {}
    std::size_t size() const override { return count; }
    xui::ItemKey key(std::size_t i) const override {
        require(i < count, "Bounded item lookup"); ++keys; return {first + i * stride, 1};
    }
    std::optional<std::size_t> find(xui::ItemKey key) const override {
        ++lookups;
        if (key.version != 1 || key.id < first || (key.id - first) % stride) return {};
        const auto i = (key.id - first) / stride; return i < count ? std::optional<std::size_t>{i} : std::nullopt;
    }
    xui::ItemContent item(std::size_t i) const override {
        ++reads;
        return {L"Record " + std::to_wstring(key(i).id), L"Secondary text", xui::ButtonIcon::theme, 0.4, L"Act"};
    }
    std::vector<xui::ItemGroup> groups() const override {
        return {{{9000000001, 1}, L"First half", 0, count / 2}, {{9000000002, 1}, L"Second half", count / 2, count - count / 2}};
    }
};
class Tree final : public xui::TreeSource {
public:
    std::shared_ptr<Items> top = std::make_shared<Items>(4);
    std::shared_ptr<const xui::ItemsSource> roots() const override { return top; }
    bool has_children(xui::ItemKey key) const override { return key.id < 5 || key.id == 1000001; }
};
class Rows final : public xui::GridSource {
public:
    std::size_t count;
    bool reverse;
    std::uint64_t stride;
    explicit Rows(std::size_t count = 100000, bool reverse = false, std::uint64_t stride = 1) : count(count), reverse(reverse), stride(stride) {}
    std::size_t size() const override { return count; }
    xui::RowKey key(std::size_t i) const override { return {(reverse ? count - i : i + 1) * stride, 1}; }
    std::optional<std::size_t> find(xui::RowKey key) const override {
        if (key.version != 1 || !key.id || key.id % stride || key.id / stride > count) return {};
        return reverse ? count - key.id / stride : key.id / stride - 1;
    }
    std::wstring text(std::size_t i, std::size_t column) const override { return (column ? L"Value " : L"Row ") + std::to_wstring(key(i).id); }
};
}

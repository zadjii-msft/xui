#pragma once
#include "xui/path_input.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xui::explorer {
struct Location {
    std::filesystem::path path;
    std::wstring query, selected_path, focused_path;
    float offset{};
};
class History {
public:
    explicit History(Location location) { entries_.push_back(std::move(location)); }
    Location& current() { return entries_[index_]; }
    const Location& current() const { return entries_[index_]; }
    std::optional<std::size_t> relative(int delta) const {
        if (delta < 0) return index_ ? std::optional(index_ - 1) : std::nullopt;
        return index_ + 1 < entries_.size() ? std::optional(index_ + 1) : std::nullopt;
    }
    const Location& at(std::size_t index) const { return entries_.at(index); }
    std::size_t size() const { return entries_.size(); }
    void commit(Location location, std::optional<std::size_t> index = {}) {
        if (index) {
            entries_.at(*index) = std::move(location);
            index_ = *index;
        } else if (current().path == location.path) {
            current() = std::move(location);
        } else {
            entries_.erase(entries_.begin() + index_ + 1, entries_.end());
            entries_.push_back(std::move(location));
            if (entries_.size() > maximum_entries) entries_.erase(entries_.begin());
            index_ = entries_.size() - 1;
        }
    }
    static constexpr std::size_t maximum_entries = 128;
private:
    std::vector<Location> entries_;
    std::size_t index_{};
};
struct Tab {
    std::uint64_t id;
    History history;
};
struct NavigationRequest {
    std::uint64_t serial{}, tab{};
    Location location;
    std::optional<std::size_t> history_index;
};
class PaneState {
public:
    explicit PaneState(std::filesystem::path initial) { add(std::move(initial)); }
    const std::vector<Tab>& tabs() const { return tabs_; }
    Tab& active() { return tabs_.at(active_); }
    const Tab& active() const { return tabs_.at(active_); }
    const std::optional<NavigationRequest>& pending() const { return pending_; }
    std::uint64_t add(std::filesystem::path path) {
        if (tabs_.size() == maximum_tabs) throw std::length_error("A pane can contain at most 16 tabs");
        cancel();
        const auto id = next_id_++;
        tabs_.push_back({id, History(Location{std::move(path)})});
        active_ = tabs_.size() - 1;
        return id;
    }
    bool select(std::uint64_t id) {
        for (std::size_t i = 0; i < tabs_.size(); ++i) if (tabs_[i].id == id) {
            if (active_ != i) cancel();
            active_ = i;
            return true;
        }
        return false;
    }
    bool close(std::uint64_t id) {
        if (tabs_.size() == 1) return false;
        for (std::size_t i = 0; i < tabs_.size(); ++i) if (tabs_[i].id == id) {
            const bool was_active = i == active_;
            if (was_active) cancel();
            tabs_.erase(tabs_.begin() + i);
            if (i < active_) --active_;
            else if (active_ == tabs_.size()) --active_;
            return true;
        }
        return false;
    }
    NavigationRequest begin(Location location, std::optional<std::size_t> history_index = {}) {
        pending_ = NavigationRequest{++serial_, active().id, std::move(location), history_index};
        return *pending_;
    }
    void set_query(std::wstring query) {
        if (pending_) { pending_->location.query = std::move(query); pending_->location.offset = 0; }
        else { active().history.current().query = std::move(query); active().history.current().offset = 0; }
    }
    bool finish(std::uint64_t serial, bool success) {
        if (!pending_ || pending_->serial != serial || pending_->tab != active().id) return false;
        if (success) active().history.commit(std::move(pending_->location), pending_->history_index);
        pending_.reset();
        return true;
    }
    void cancel() { ++serial_; pending_.reset(); }
    static constexpr std::size_t maximum_tabs = 16;
private:
    std::vector<Tab> tabs_;
    std::size_t active_{};
    std::uint64_t next_id_{1}, serial_{};
    std::optional<NavigationRequest> pending_;
};
inline std::filesystem::path resolve_location(std::wstring input, const std::filesystem::path& base) {
    auto expanded = expand_path_input(input);
    if (!expanded.error.empty()) throw PathInputError(std::move(expanded.error));
    input = std::move(expanded.text);
    if (input.empty()) throw std::invalid_argument("Enter a folder path");
    std::filesystem::path path(input);
    if (path.has_root_name() && !path.has_root_directory()) throw std::invalid_argument("Use an absolute drive path");
    if (!path.is_absolute()) path = base / path;
    auto resolved = std::filesystem::absolute(path).lexically_normal();
    if (resolved.native().size() >= 32767) throw std::length_error("Folder path is too long");
    return resolved;
}
inline std::filesystem::path parent_location(const std::filesystem::path& path) {
    auto parent = path.lexically_normal();
    while (parent.has_relative_path() && parent.filename().empty()) parent = parent.parent_path();
    const auto text = parent.wstring();
    if (text.starts_with(L"\\\\?\\") && text.size() <= 7 && text.size() >= 6 && text[5] == L':') return path;
    const std::size_t unc_start = text.starts_with(L"\\\\?\\UNC\\") ? 8 :
        text.starts_with(L"\\\\") && !text.starts_with(L"\\\\?\\") ? 2 : 0;
    if (unc_start) {
        const auto server = text.find(L'\\', unc_start);
        if (server == std::wstring::npos || text.find(L'\\', server + 1) == std::wstring::npos) return path;
    }
    if (parent.has_relative_path()) parent = parent.parent_path();
    return parent.empty() ? path : parent;
}
}

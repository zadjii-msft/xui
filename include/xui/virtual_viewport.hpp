#pragma once
#include "xui/core.hpp"
#include <unordered_map>

namespace xui {

struct VirtualViewportRect {
    float offset{}, width{}, height{}, extent{};
    bool operator==(const VirtualViewportRect&) const = default;
};
struct VirtualViewportRequest {
    std::uint64_t epoch{}, committed_source_version{}, requested_source_version{};
    VirtualViewportRect committed, requested;
    bool blocked{};
};
struct VirtualItemInfo {
    std::u16string key;
    std::uint32_t index{}, count{};
    std::uint64_t source_version{};
};
struct VirtualViewportItem {
    std::weak_ptr<Stack> root;
    VirtualItemInfo info;
};
enum class VirtualViewportResult { ready = 0, superseded = 1, blocked = 2 };

// UI-thread backend state. A ready update reserves its logical target while
// later intent queues behind it. It does not freeze native accessibility.
class VirtualViewport final {
public:
    VirtualViewport(std::uint32_t item_count, float row_height, std::uint64_t source_version);
    void on_request(std::function<void()> callback) { notify_ = std::move(callback); }
    VirtualViewportRequest request() const;
    void observe(Size available, float scale, bool composing, std::uint64_t focus_identity);
    void set_extent(std::uint32_t item_count, std::uint64_t source_version);
    void request_offset(float offset, bool force = true);
    VirtualViewportResult try_begin(std::uint64_t epoch);
    VirtualViewportResult commit(std::uint64_t epoch);
    void cancel(std::uint64_t epoch);
    void close() noexcept;
    bool closed() const { return closed_; }
    std::uint64_t identity() const { return identity_; }
    bool updating() const { return active_.has_value(); }
    std::uint64_t active_epoch() const { return active_ ? active_->epoch : 0; }
    std::uint64_t committed_epoch() const { return committed_epoch_; }
    const VirtualViewportRect& committed() const { return committed_; }
    float requested_offset() const { return requested_.offset; }
    float requested_maximum_offset() const;
    float layout_extent() const;
    float layout_width() const;
    float row_height() const { return pitch_; }
    std::uint32_t committed_count() const { return committed_count_; }
    std::uint32_t active_count() const { return active_count_; }
    const VirtualViewportRequest& active_request() const;
    void set_item(std::shared_ptr<Stack> root, VirtualItemInfo info);
    void remove_item(std::uint64_t id);
    std::vector<VirtualViewportItem> items() const;
private:
    void require_open() const;
    float validate_extent(std::uint32_t count, float scale) const;
    VirtualViewportRect normalized_request(float extent, float offset, Size available, float scale) const;
    void publish_request(VirtualViewportRect next, bool force);
    void notify();
    float pitch_{}, scale_{1}, extent_{}, offset_intent_{};
    Size available_{};
    std::uint32_t count_{}, committed_count_{}, active_count_{};
    std::uint64_t identity_{}, epoch_{1}, committed_epoch_{}, committed_source_{}, requested_source_{}, focus_identity_{};
    VirtualViewportRect committed_, requested_;
    std::optional<VirtualViewportRequest> active_;
    bool observed_{}, composing_{}, cancelled_{}, closed_{};
    std::function<void()> notify_;
    std::unordered_map<std::uint64_t, VirtualViewportItem> items_;
};

struct ControlInteraction {
    bool has_focus{}, is_composing{};
    bool operator==(const ControlInteraction&) const = default;
};
}

#pragma once

#include "xui/core.hpp"
#include <vector>

namespace xui {

enum class HostWork : unsigned {
    none = 0, paint = 1, accessibility = 2, native = 4, placement = 8, layout = 16, topology = 32,
    state = paint | accessibility | native, geometry = state | placement | layout,
    all = geometry | topology
};
constexpr HostWork operator|(HostWork a, HostWork b) {
    return static_cast<HostWork>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool includes(HostWork work, HostWork bit) {
    return (static_cast<unsigned>(work) & static_cast<unsigned>(bit)) != 0;
}
constexpr HostWork host_work(Invalidation kind) {
    switch (kind) {
    case Invalidation::paint: return HostWork::paint;
    case Invalidation::accessibility: return HostWork::paint | HostWork::accessibility;
    case Invalidation::native: return HostWork::state;
    case Invalidation::state: return HostWork::state;
    case Invalidation::scroll: return HostWork::paint | HostWork::placement | HostWork::accessibility;
    case Invalidation::placement: return HostWork::state | HostWork::placement;
    case Invalidation::layout: return HostWork::geometry;
    case Invalidation::topology: return HostWork::all;
    }
    throw std::invalid_argument("Invalid dirty-work category");
}
struct DirtyRequest {
    std::uint64_t origin{};
    HostWork work{};
};
class DirtyWork {
    std::vector<DirtyRequest> requests_;
public:
    void add(std::uint64_t origin, HostWork work) {
        for (auto& request : requests_)
            if (request.origin == origin) { request.work = request.work | work; return; }
        requests_.push_back({origin, work});
    }
    void clear() { requests_.clear(); }
    bool empty() const { return requests_.empty(); }
    void swap(DirtyWork& other) noexcept { requests_.swap(other.requests_); }
    const std::vector<DirtyRequest>& requests() const { return requests_; }
    HostWork combined() const {
        HostWork work{};
        for (const auto& request : requests_) work = work | request.work;
        return work;
    }
};

}

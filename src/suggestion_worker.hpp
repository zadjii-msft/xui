#pragma once

#include "xui/suggestions.hpp"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <optional>

namespace xui::detail {

inline constexpr UINT suggestions_ready = WM_APP + 0x173;

struct SuggestionDelivery {
    std::mutex mutex;
    std::atomic<std::uint64_t> generation{};
    HWND window{};
    std::optional<SuggestionResult> result;

    void cancel();
    void revoke();
    std::optional<SuggestionResult> take();
};

class SuggestionWorker : public std::enable_shared_from_this<SuggestionWorker> {
public:
    static std::shared_ptr<SuggestionWorker> shared();
    void request(std::shared_ptr<SuggestionSource> source, SuggestionRequest request,
        std::shared_ptr<SuggestionDelivery> delivery);
private:
    struct Work {
        std::shared_ptr<SuggestionSource> source;
        SuggestionRequest request;
        std::shared_ptr<SuggestionDelivery> delivery;
        std::uint64_t generation{};
    };
    static void CALLBACK run(PTP_CALLBACK_INSTANCE, void*) noexcept;
    std::mutex mutex_;
    std::optional<Work> pending_;
    bool running_{};
};
}

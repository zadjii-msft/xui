#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace xui {

struct SuggestionRequest {
    std::wstring text;
    std::wstring context;
    bool explicit_request{};
    static constexpr std::size_t maximum_results = 64;
    static constexpr std::size_t maximum_text = 32767;
};

struct SuggestionResult {
    std::vector<std::wstring> items;
    std::wstring status;
};

// Called on a background worker. Implementations must not access controls.
class SuggestionSource {
public:
    virtual ~SuggestionSource() = default;
    virtual SuggestionResult suggest(const SuggestionRequest& request,
        const std::function<bool()>& cancelled) = 0;
};

// Windows: directories only, resolved relative to the input's context.
std::shared_ptr<SuggestionSource> folder_suggestions();

}

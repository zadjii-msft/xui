#pragma once

#include "xui/core.hpp"
#include <filesystem>
#include <unordered_map>

namespace xui {

class DirectorySource {
public:
    explicit DirectorySource(std::filesystem::path folder) : folder_(std::move(folder)) {}
    SourceResult scan(const CancelCheck& cancel);
private:
    std::filesystem::path folder_;
    ItemId next_id_{1};
    std::unordered_map<std::wstring, ItemId> identities_;
};

}

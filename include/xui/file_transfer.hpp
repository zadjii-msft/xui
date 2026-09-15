#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xui {
enum class FileTransferEffect : std::uint32_t { none = 0, copy = 1, move = 2 };
struct FileClipboardContent {
    std::vector<std::wstring> paths;
    FileTransferEffect effect{FileTransferEffect::copy};
};
inline constexpr std::size_t maximum_transfer_paths = 4096;
}

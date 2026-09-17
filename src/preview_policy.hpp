#pragma once
#include "preview_protocol.hpp"
#include <wrl/client.h>
#include <objidl.h>
#include <vector>

namespace xui::preview {
struct EligibleFile {
    Handle file;
    Handle origin;
    std::vector<Handle> ancestors;
    std::wstring path;
};
EligibleFile open_eligible(const std::wstring& path);
Microsoft::WRL::ComPtr<IStream> readonly_stream(HANDLE file);
bool local_path_syntax(std::wstring_view path);
bool allowed_origin(std::string_view zone);
}

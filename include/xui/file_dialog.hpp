#pragma once
#include <string>
#include <vector>

namespace xui {
struct FileDialogFilter {
    std::wstring name;
    std::wstring pattern;
};
struct FileDialogOptions {
    std::wstring title;
    std::vector<FileDialogFilter> filters;
    std::wstring default_extension;
    std::wstring suggested_name;
    std::wstring initial_directory;
    void validate() const;
};
}

#pragma once
#include "xui/file_transfer.hpp"
#include "xui/data_grid.hpp"
#include <windows.h>
#include <oleidl.h>
#include <wrl/client.h>
#include <functional>
#include <span>

namespace xui::files {
void validate_paths(const std::vector<std::wstring>& paths);
std::vector<std::byte> serialize_paths(const std::vector<std::wstring>& paths);
std::vector<std::wstring> parse_paths(std::span<const std::byte> bytes);
Microsoft::WRL::ComPtr<IDataObject> data_object(const std::vector<std::wstring>& paths, FileTransferEffect preferred);
std::optional<FileClipboardContent> read_object(IDataObject* object);
void set_clipboard(const std::vector<std::wstring>& paths, FileTransferEffect effect);
void set_text(const std::wstring& text);
std::optional<FileClipboardContent> get_clipboard();
bool transfer(HWND owner, const std::vector<std::wstring>& paths, const std::wstring& destination,
    FileTransferEffect effect, const std::function<bool()>& cancelled = {});
std::optional<bool> paste(HWND owner, const std::wstring& destination, const std::function<bool()>& cancelled = {});
FileTransferEffect drag(HWND source, const std::vector<std::wstring>& paths, const std::function<bool()>& cancelled);
// The host owns registration. The object retains only the grid and weak host callbacks.
Microsoft::WRL::ComPtr<IDropTarget> drop_target(HWND window, std::shared_ptr<DataGrid> grid,
    std::function<bool()> available, std::function<void(std::function<void()>)> dispatch);
}

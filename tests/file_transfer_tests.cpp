#include "../src/file_transfer.hpp"
#include "xui/xui.h"
#include "private_desktop.hpp"
#include <shlobj.h>
#include <shldisp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected{};
    try { fn(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid file transfer input was accepted");
}
class Rows final : public xui::GridSource {
public:
    std::size_t size() const override { return 3; }
    xui::RowKey key(std::size_t row) const override { return {row + 1, 1}; }
    std::optional<std::size_t> find(xui::RowKey key) const override {
        return key.version == 1 && key.id >= 1 && key.id <= 3 ? std::optional<std::size_t>{key.id - 1} : std::nullopt;
    }
    std::wstring text(std::size_t, std::size_t) const override { return L"Row"; }
};
void grid_tests() {
    xui::DataGrid grid;
    grid.set_columns({{L"Name", 180}});
    grid.set_source(std::make_shared<Rows>());
    grid.arrange({0, 0, 300, 240});
    grid.on_file_drag([] { return std::vector<std::wstring>{L"C:\\one"}; }, [](auto) {});
    grid.select({1, 1}); grid.select({2, 1}, xui::SelectionGesture::toggle);
    check(grid.begin_file_press({30, 45}, xui::SelectionGesture::replace), "Drag did not arm");
    check(grid.selection().contains({1, 1}) && grid.selection().contains({2, 1}), "Drag press collapsed multiselection");
    check(!grid.file_drag_threshold({33, 48}, {4, 4}), "Drag started below threshold");
    check(grid.file_drag_threshold({34, 45}, {4, 4}), "Drag did not start at threshold");
    grid.end_file_press(false);
    check(grid.selection().contains({2, 1}), "Drag completion collapsed multiselection");
    grid.begin_file_press({30, 45}, xui::SelectionGesture::replace); grid.end_file_press(true);
    check(!grid.selection().contains({2, 1}), "Click did not collapse selection");
    grid.select_all();
    grid.begin_file_press({30, 45}, xui::SelectionGesture::replace); grid.cancel();
    check(!grid.file_drag_threshold({80, 45}, {4, 4}) && grid.selection().contains({3, 1}), "Cancel changed membership");
    grid.begin_file_press({30, 45}, xui::SelectionGesture::toggle);
    check(!grid.selection().contains({1, 1}), "Ctrl press did not toggle membership");
    std::optional<xui::RowKey> key;
    check(!grid.file_drop_hit({30, 20}, key), "Header accepted file drop");
    check(!grid.file_drop_hit({295, 80}, key), "Scrollbar accepted file drop");
    check(!grid.file_drop_hit({30, 235}, key), "Bottom scrollbar accepted file drop");
    check(grid.file_drop_hit({30, 45}, key) && key == xui::RowKey{1, 1}, "Drop row key is incorrect");
    check(grid.file_drop_hit({30, 200}, key) && !key, "Empty body is not a background drop");
    unsigned queries{}, drops{};
    grid.on_file_drop([&](auto row, auto effect) { ++queries; return row ? xui::FileTransferEffect::none : effect; },
        [&](auto, const auto&, auto effect) { ++drops; return effect; });
    check(grid.query_file_drop({30, 20}, xui::FileTransferEffect::copy) == xui::FileTransferEffect::none && queries == 0,
        "Header invoked drop query");
    check(grid.query_file_drop({30, 200}, xui::FileTransferEffect::copy) == xui::FileTransferEffect::copy && drops == 0,
        "Query performed a drop");
    grid.set_enabled(false);
    check(!grid.file_drop_hit({30, 200}, key), "Disabled grid accepted file drop");
}
void serialization_tests() {
    const std::vector<std::wstring> paths{L"C:\\folder\\résumé.txt", L"\\\\server\\share\\文件.txt", L"C:\\folder\\😀.txt"};
    auto bytes = xui::files::serialize_paths(paths);
    check(xui::files::parse_paths(bytes) == paths, "Unicode CF_HDROP round trip failed");
    auto malformed = bytes; malformed.resize(malformed.size() - 2);
    rejects([&] { xui::files::parse_paths(malformed); });
    malformed = bytes;
    DWORD offset = static_cast<DWORD>(bytes.size() + 1);
    std::memcpy(malformed.data(), &offset, sizeof(offset));
    rejects([&] { xui::files::parse_paths(malformed); });
    rejects([] { xui::files::serialize_paths({L"relative.txt"}); });
    rejects([] { xui::files::serialize_paths({}); });
    rejects([] { xui::files::serialize_paths(std::vector<std::wstring>(4097, L"C:\\x")); });
    const char ansi[] = "C:\\ansi.txt\0";
    const DROPFILES header{sizeof(DROPFILES), {}, FALSE, FALSE};
    std::vector<std::byte> narrow(sizeof(header) + sizeof(ansi));
    std::memcpy(narrow.data(), &header, sizeof(header)); std::memcpy(narrow.data() + sizeof(header), ansi, sizeof(ansi));
    check(xui::files::parse_paths(narrow) == std::vector<std::wstring>{L"C:\\ansi.txt"}, "ANSI CF_HDROP failed");
    auto object = xui::files::data_object(paths, xui::FileTransferEffect::move);
    Microsoft::WRL::ComPtr<IDataObjectAsyncCapability> asynchronous;
    check(object.As(&asynchronous) == E_NOINTERFACE, "Drag data object allows premature asynchronous completion");
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM first{}, second{};
    check(SUCCEEDED(object->GetData(&format, &first)) && SUCCEEDED(object->GetData(&format, &second)),
        "Data object cannot provide independent storage");
    check(first.hGlobal != second.hGlobal && !first.pUnkForRelease && !second.pUnkForRelease,
        "Data object shared ownership of clipboard memory");
    const auto* memory = static_cast<const std::byte*>(GlobalLock(first.hGlobal));
    check(memory != nullptr, "Cannot inspect clipboard allocation");
    for (auto padding = bytes.size(); padding < GlobalSize(first.hGlobal); ++padding)
        check(memory[padding] == std::byte{}, "Clipboard allocation exposes uninitialized padding");
    GlobalUnlock(first.hGlobal);
    ReleaseStgMedium(&first); ReleaseStgMedium(&second);
    auto result = xui::files::read_object(object.Get());
    check(result && result->paths == paths && result->effect == xui::FileTransferEffect::move, "Shell data object round trip failed");
}
DWORD effect_data(IDataObject* object, const wchar_t* name) {
    FORMATETC format{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name)), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    check(SUCCEEDED(object->GetData(&format, &medium)), "Missing completion notification");
    auto* data = GlobalLock(medium.hGlobal);
    check(data != nullptr, "Cannot read completion notification");
    DWORD effect{}; std::memcpy(&effect, data, sizeof(effect));
    GlobalUnlock(medium.hGlobal); ReleaseStgMedium(&medium);
    return effect;
}
void target_tests() {
    const HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"XUI file target fixture",
        WS_POPUP, 0, 0, 300, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(hwnd != nullptr, "Could not create drop target fixture");
    struct Close { HWND window; ~Close() { RevokeDragDrop(window); DestroyWindow(window); } } close{hwnd};
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    auto grid = std::make_shared<xui::DataGrid>();
    grid->set_columns({{L"Name", 180}}); grid->set_source(std::make_shared<Rows>()); grid->arrange({0, 0, 300, 240});
    unsigned queries{}, drops{};
    bool complete{}, available = true;
    grid->on_file_drop([&](auto key, auto effect) { ++queries; return key ? xui::FileTransferEffect::none : effect; },
        [&](auto key, const auto& paths, auto effect) {
            check(!key && paths == std::vector<std::wstring>{L"C:\\fixture.txt"}, "Drop did not receive snapshot paths");
            ++drops; return complete ? effect : xui::FileTransferEffect::none;
        });
    auto target = xui::files::drop_target(hwnd, grid, [&] { return available; }, [](auto fn) { fn(); });
    auto object = xui::files::data_object({L"C:\\fixture.txt"}, xui::FileTransferEffect::none);
    const auto location = [&](LONG x, LONG y) {
        const auto dpi = GetDpiForWindow(hwnd);
        POINT p{MulDiv(x, dpi, 96), MulDiv(y, dpi, 96)}; ClientToScreen(hwnd, &p); return POINTL{p.x, p.y};
    };
    DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    check(SUCCEEDED(target->DragEnter(object.Get(), 0, location(30, 200), &effect)) && effect == DROPEFFECT_COPY,
        "External drag did not default to Copy");
    check(drops == 0, "DragEnter performed file work");
    effect = 3; target->DragOver(MK_SHIFT, location(30, 200), &effect);
    check(effect == DROPEFFECT_MOVE && drops == 0, "Shift did not request Move");
    effect = 3; target->DragOver(MK_CONTROL | MK_SHIFT, location(30, 200), &effect);
    check(effect == DROPEFFECT_NONE, "Link gesture was accepted");
    const auto before = queries;
    effect = 3; target->DragOver(0, location(30, 20), &effect);
    check(effect == DROPEFFECT_NONE && queries == before, "Header invoked acceptance query");
    effect = 3; target->Drop(object.Get(), MK_SHIFT, location(30, 200), &effect);
    check(effect == DROPEFFECT_NONE && drops == 1, "Cancelled drop advertised success");
    complete = true;
    effect = 3; target->DragEnter(object.Get(), MK_SHIFT, location(30, 200), &effect);
    target->Drop(object.Get(), MK_SHIFT, location(30, 200), &effect);
    check(effect == DROPEFFECT_NONE && drops == 2, "Optimized move requested source deletion");
    check(effect_data(object.Get(), CFSTR_PERFORMEDDROPEFFECT) == DROPEFFECT_NONE &&
        effect_data(object.Get(), CFSTR_LOGICALPERFORMEDDROPEFFECT) == DROPEFFECT_MOVE, "Optimized move notifications are incorrect");
    effect = 3; target->DragEnter(object.Get(), 0, location(30, 200), &effect);
    available = false;
    effect = 3; target->Drop(object.Get(), 0, location(30, 200), &effect);
    check(effect == 0 && drops == 2, "Closed owner accepted pending drop");
    target.Reset();
}
xui_string span(const std::string& text) { return {text.data(), static_cast<uint32_t>(text.size()), 0}; }
void abi_tests() {
    const std::string title = "File transfer ABI fixture";
    const xui_window_options options{sizeof(options), XUI_ABI_VERSION, span(title), 300, 240, 0, 0};
    xui_handle window{};
    check(xui_window_create(&options, &window) == XUI_OK, "ABI window creation failed");
    struct Close { xui_handle handle; ~Close() { xui_window_destroy(handle); } } close{window};
    const std::string path = "C:\\fixture.txt";
    const auto value = span(path);
    check(xui_window_set_file_clipboard(window, &value, 1, XUI_FILE_COPY | XUI_FILE_MOVE) == XUI_INVALID_ARGUMENT,
        "ABI accepted ambiguous transfer effect");
    check(xui_window_set_file_clipboard(window, &value, 4097, XUI_FILE_COPY) == XUI_INVALID_ARGUMENT,
        "ABI accepted too many paths");
    const auto clipboard_status = xui_window_set_file_clipboard(window, &value, 1, XUI_FILE_MOVE);
    if (clipboard_status != XUI_OK) {
        char message[1024]{}; uint32_t length{}; xui_status status{};
        xui_error_copy(message, sizeof(message), &length, &status);
        throw std::runtime_error("ABI clipboard set failed (" + std::to_string(clipboard_status) + "): " + std::string(message, length));
    }
    struct Receiver { xui_handle window; bool called{}; } receiver{window};
    const auto receive = [](void* context, const xui_string* paths, uint32_t count, uint32_t effect) -> xui_status {
        auto& state = *static_cast<Receiver*>(context);
        state.called = count == 1 && effect == XUI_FILE_MOVE && std::string(paths[0].data, paths[0].length) == "C:\\fixture.txt" &&
            xui_window_destroy(state.window) == XUI_BUSY;
        return state.called ? XUI_OK : XUI_CALLBACK_FAILED;
    };
    check(xui_window_get_file_clipboard(window, receive, &receiver) == XUI_OK && receiver.called, "ABI clipboard receiver failed");
    const std::string text = "text clipboard";
    check(xui_window_set_clipboard_text(window, span(text)) == XUI_OK, "ABI text clipboard failed");
    uint32_t result = 99;
    check(xui_window_paste_files(window, value, &result) == XUI_OK && result == 0, "ABI no-files paste result is incorrect");
    check(xui_window_close(window) == XUI_OK, "ABI close failed");
    check(xui_window_get_file_clipboard(window, receive, &receiver) != XUI_OK, "Closed ABI owner read clipboard");
}
struct Fixture {
    std::filesystem::path root = std::filesystem::current_path() / ("file-transfer-fixture-" + std::to_string(GetCurrentProcessId()));
    Fixture() {
        check(!std::filesystem::exists(root), "Fixture directory already exists");
        std::filesystem::create_directories(root / "source");
        std::filesystem::create_directories(root / "copy");
        std::filesystem::create_directories(root / "move");
    }
    ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
};
void operations_tests(bool clipboard) {
    Fixture fixture;
    const auto& root = fixture.root;
    const auto source = root / "source" / L"résumé.txt";
    std::ofstream(source) << "file transfer fixture";
    std::filesystem::create_directory(root / "source" / "folder");
    std::ofstream(root / "source" / "folder" / "nested.txt") << "nested";
    check(xui::files::transfer(nullptr, {source.wstring(), (root / "source" / "folder").wstring()},
        (root / "copy").wstring(), xui::FileTransferEffect::copy), "Shell copy did not complete");
    check(std::filesystem::exists(source) && std::filesystem::exists(root / "copy" / L"résumé.txt") &&
        std::filesystem::exists(root / "copy" / "folder" / "nested.txt"), "Shell copy lost data");
    check(xui::files::transfer(nullptr, {source.wstring()}, (root / "move").wstring(), xui::FileTransferEffect::move),
        "Shell move did not complete");
    check(!std::filesystem::exists(source) && std::filesystem::exists(root / "move" / L"résumé.txt"), "Shell move result is incorrect");
    // Pre-cancel an actual collision: this fixture must never open unattended conflict UI.
    const auto moved = root / "move" / L"résumé.txt";
    check(!xui::files::transfer(nullptr, {moved.wstring()}, (root / "copy").wstring(), xui::FileTransferEffect::move,
        [] { return true; }), "Cancelled collision reported success");
    check(std::filesystem::exists(moved), "Cancelled transfer deleted its source");
    rejects([&] { xui::files::transfer(nullptr, {(root / "missing.txt").wstring()}, (root / "copy").wstring(), xui::FileTransferEffect::move); });
    if (!clipboard) return;
    xui::files::set_clipboard({moved.wstring()}, xui::FileTransferEffect::copy);
    auto clip = xui::files::get_clipboard();
    check(clip && clip->paths == std::vector<std::wstring>{moved.wstring()} && clip->effect == xui::FileTransferEffect::copy,
        "Persistent clipboard round trip failed");
    std::filesystem::create_directory(root / "paste");
    check(xui::files::paste(nullptr, (root / "paste").wstring()) == true, "Clipboard copy paste failed");
    check(xui::files::get_clipboard().has_value(), "Copy paste consumed clipboard");
    xui::files::set_clipboard({moved.wstring()}, xui::FileTransferEffect::move);
    std::filesystem::create_directory(root / "cut");
    check(xui::files::paste(nullptr, (root / "cut").wstring()) == true, "Clipboard cut paste failed");
    check(!std::filesystem::exists(moved), "Cut paste did not move source");
    check(!xui::files::get_clipboard(), "Cut clipboard was not cleared");
    const auto cut = root / "cut" / L"résumé.txt";
    xui::files::set_clipboard({cut.wstring()}, xui::FileTransferEffect::move);
    std::filesystem::create_directory(root / "replacement");
    bool replaced{};
    check(xui::files::paste(nullptr, (root / "replacement").wstring(), [&] {
        if (!replaced) { replaced = true; xui::files::set_text(L"replacement clipboard"); }
        return false;
    }) == true, "Paste with clipboard replacement failed");
    check(IsClipboardFormatAvailable(CF_UNICODETEXT), "Paste cleared a replacement clipboard");
    check(!xui::files::paste(nullptr, root.wstring()).has_value(), "Text clipboard was treated as files");
}
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    try {
        if (argc > 1 && std::strcmp(argv[1], "--clipboard") == 0) {
            xui::test::isolated_process(L"--clipboard-child", 60000);
            std::cout << "Private clipboard checks passed; interactive clipboard sequence unchanged\n";
            return 0;
        }
        const bool clipboard = argc > 1 && std::strcmp(argv[1], "--clipboard-child") == 0;
        std::unique_ptr<xui::test::PrivateDesktop> desktop;
        if (clipboard) desktop = std::make_unique<xui::test::PrivateDesktop>();
        check(SUCCEEDED(OleInitialize(nullptr)), "Initialize test OLE apartment");
        struct Apartment { ~Apartment() { OleUninitialize(); } } apartment;
        std::cout << "Grid gestures\n"; grid_tests();
        std::cout << "Clipboard serialization\n"; serialization_tests();
        if (!clipboard) { std::cout << "OLE drop target protocol\n"; target_tests(); }
        std::cout << "Shell operations\n"; operations_tests(clipboard);
        if (clipboard) { std::cout << "C ABI clipboard and ownership\n"; abi_tests(); }
        std::cout << "File transfer tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

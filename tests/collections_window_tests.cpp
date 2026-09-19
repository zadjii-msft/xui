#include "collections_fixture.hpp"
#include "xui/application.hpp"
#include "xui/adaptive_layout.hpp"
#include "../src/drawing.hpp"
#include "suggestion_capture.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <psapi.h>
#include <iostream>
#include <cmath>
#include <limits>
#include <thread>

namespace {
using namespace xui;
using namespace collections_test;
using Microsoft::WRL::ComPtr;
constexpr UINT metrics = WM_APP + 60, update = WM_APP + 12;
void success(HRESULT hr, const char* message) { if (FAILED(hr)) throw std::runtime_error(std::string(message) + " HRESULT=" + std::to_string(hr)); }
void flush(HWND hwnd) { SendMessageW(hwnd, update, 0, 0); UpdateWindow(hwnd); }
HWND native(HWND root, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(root, [](HWND child, LPARAM p) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(p); wchar_t name[128]{}; GetWindowTextW(child, name, 128);
        if (std::wstring_view(name) == s.name) { s.result = child; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Owned native peer exists"); return search.result;
}
void palette_appearance(Window& window, HWND hwnd, Control& anchor, ThemeMode theme, UINT dpi) {
    struct Commands final : ItemsSource {
        std::size_t size() const override { return 3; }
        ItemKey key(std::size_t row) const override { return {row + 1, 1}; }
        std::optional<std::size_t> find(ItemKey key) const override {
            return key.version == 1 && key.id && key.id <= size() ? std::optional{std::size_t(key.id - 1)} : std::nullopt;
        }
        ItemContent item(std::size_t row) const override {
            if (row == 0) return {L"New tab", L"Ctrl+T"};
            if (row == 1) {
                ItemContent item{L"Previous tab", L"Ctrl+Shift+Tab"}; item.enabled = false; return item;
            }
            return {L"No shortcut"};
        }
    };
    auto items = std::make_shared<ItemsView>(L"Shortcut rows");
    items->set_items(std::make_shared<Commands>());
    items->set_item_size({180, 56});
    require(!items->trailing_shortcut_badges(), "Ordinary items retain subtitle presentation by default");
    items->set_trailing_shortcut_badges(true);
    auto status = std::make_shared<Label>(L"Status");
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_padding({12, 12, 12, 12}); content->add(items, 1); content->add(status);
    auto popup = std::make_shared<Popup>(content);
    popup->set_preferred_size({440, 280}); popup->set_placement(PopupPlacement::center);
    require(!popup->window_background(), "Ordinary popups keep their raised surface by default");
    window.show_popup(popup, anchor, items.get()); flush(hwnd);
    const auto palette = Palette::system(theme);
    const auto color = [](D2D1_COLOR_F value) {
        return RGB(int(value.r * 255 + .5f), int(value.g * 255 + .5f), int(value.b * 255 + .5f));
    };
    struct Snapshot {
        HDC dc{};
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        ~Snapshot() {
            if (previous) SelectObject(dc, previous);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
    } snapshot;
    RECT host{}; require(GetWindowRect(hwnd, &host) != FALSE, "Read owned palette host bounds");
    POINT client{}; require(ClientToScreen(hwnd, &client) != FALSE, "Read owned palette client origin");
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), host.right - host.left, -(host.bottom - host.top), 1, 32, BI_RGB};
    snapshot.dc = CreateCompatibleDC(nullptr); require(snapshot.dc != nullptr, "Create palette snapshot context");
    void* pixels{};
    snapshot.bitmap = CreateDIBSection(snapshot.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(snapshot.bitmap != nullptr, "Create palette snapshot bitmap");
    snapshot.previous = SelectObject(snapshot.dc, snapshot.bitmap);
    LRESULT captured_paint = -1;
    const auto sample = [&](float x, float y) {
        if (captured_paint != SendMessageW(hwnd, metrics, 0, 0)) {
            // Capture only this test window, even when another application covers it.
            require(PrintWindow(hwnd, snapshot.dc, 2) != FALSE, "Capture owned palette pixels");
            captured_paint = SendMessageW(hwnd, metrics, 0, 0);
        }
        const auto pixel = GetPixel(snapshot.dc, int(x * dpi / 96) + client.x - host.left,
            int(y * dpi / 96) + client.y - host.top);
        require(pixel != CLR_INVALID, "Palette sample stays inside its host"); return pixel;
    };
    const auto frame_color = [&] {
        const auto b = popup->bounds(); return sample(b.x + 6, b.y + b.height / 2);
    };
    const auto result_color = [&] {
        const auto b = items->bounds(); return sample(b.x + b.width - 20, b.y + b.height - 4);
    };
    const auto status_color = [&] {
        const auto b = status->bounds(); return sample(b.x + b.width - 4, b.y + b.height / 2);
    };
    require(frame_color() == color(palette.surface), "Default popup frame retains the raised surface");
    popup->set_window_background(true); flush(hwnd);
    require(frame_color() == color(palette.background) && result_color() == frame_color() && status_color() == frame_color(),
        "Palette frame, results, and status share the window background");
    const auto b = items->bounds();
    Drawing measure; measure.initialize(); Size ctrl{}, t{};
    measure.layout(L"Ctrl", TextStyle::caption, ctrl);
    measure.layout(L"T", TextStyle::caption, t);
    const float ctrl_width = std::max(24.0f, ctrl.width + 12), t_width = std::max(24.0f, t.width + 12);
    const float right = items->item_bounds(0).width - 10;
    const float first = right - ctrl_width - 4 - t_width;
    require(first > items->item_bounds(0).width / 2, "Shortcut keycaps occupy the right side of the row");
    require(sample(b.x + first + 2, b.y + 28) == color(palette.field), "Shortcut keycap has a filled face on the right");
    const auto ink_in = [&](Rect rect, COLORREF background) {
        for (float y = rect.y; y < rect.y + rect.height; ++y)
            for (float x = rect.x; x < rect.x + rect.width; ++x)
                if (sample(x, y) != background) return true;
        return false;
    };
    require(ink_in({b.x + first - 1, b.y + 20, 3, 16}, color(palette.background)),
        "Shortcut keycap has a visible outline at fractional DPI");
    require(ink_in({b.x + 10, b.y + 14, 90, 28}, color(palette.background)), "Command title remains aligned to the left beside trailing keycaps");
    require(!ink_in({b.x + 10, b.y + 44, 90, 9}, color(palette.background)), "Shortcut is not repeated beneath the title");
    require(ink_in({b.x + 10, b.y + 2 * 56 + 14, 110, 28}, color(palette.background)), "Commands without shortcuts retain left title alignment");
    require(!ink_in({b.x + right - 144, b.y + 2 * 56 + 8, 144, 40}, color(palette.background)), "Missing shortcuts leave an empty right keycap column");
    require(ink_in({b.x + 10, b.y + 56 + 14, 110, 28}, color(palette.background)), "Disabled commands retain a readable title");
    const auto second = right - t_width;
    require(sample(b.x + second + 2, b.y + 28) == color(palette.field), "Each shortcut key has a separate filled keycap");
    require(ink_in({b.x + right - 1, b.y + 20, 2, 16}, color(palette.background))
        && ink_in({b.x + right - 1, b.y + 56 + 20, 2, 16}, color(palette.background)),
        "Short and long shortcut groups share the same right edge");
    require(!ink_in({b.x + 130, b.y + 8, first - 142, 40}, color(palette.background)),
        "Shortcut keycaps leave a clear gap after the command title");
    items->select({1, 1}); flush(hwnd);
    require(sample(b.x + first + 2, b.y + 28) == color(palette.field), "Selected command preserves the keycap face");
    require(sample(b.x + first - 8, b.y + 28) == color(palette.selection), "Selected command retains selection around keycaps");
    items->set_trailing_shortcut_badges(false); flush(hwnd);
    require(items->selection().focused() == ItemKey{1, 1} && items->source()->item(0).secondary == L"Ctrl+T",
        "Presentation changes preserve selection and accessible shortcut text");
    items->set_selection({}); flush(hwnd);
    require(ink_in({b.x + 10, b.y + 30, 90, 22}, color(palette.background)), "Disabling keycaps restores ordinary secondary text");
    items->set_trailing_shortcut_badges(true);
    popup->set_preferred_size({180, 280}); flush(hwnd);
    require(frame_color() == color(palette.background) && result_color() == frame_color(),
        "Narrow shortcut rows remain clipped inside the palette");
    popup->set_window_background(false); flush(hwnd);
    require(frame_color() == color(palette.surface) && status_color() == frame_color(), "Background changes update existing popup children");
    window.dismiss_popup(*popup); flush(hwnd);
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> value; success(element->GetCurrentPatternAs(id, IID_PPV_ARGS(&value)), "Read collection UIA pattern"); return value;
}
ComPtr<IUIAutomationElement> find(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition; const auto hr = uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition); VariantClear(&value);
    success(hr, "Create scoped UIA condition"); ComPtr<IUIAutomationElement> result;
    success(root->FindFirst(TreeScope_Subtree, condition.Get(), &result), "Find scoped collection");
    if (!result) {
        ComPtr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        ComPtr<IUIAutomationElement> child;
        walker->GetFirstChildElement(root, &child);
        while (child) {
            BSTR name{}, identity{};
            child->get_CurrentName(&name); child->get_CurrentAutomationId(&identity);
            std::wcerr << L"UIA child: " << (name ? name : L"") << L" [" << (identity ? identity : L"") << L"]\n";
            SysFreeString(name); SysFreeString(identity);
            ComPtr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(child.Get(), &next); child = std::move(next);
        }
        std::string identity;
        for (const auto* character = id; *character; ++character) identity += static_cast<char>(*character);
        throw std::runtime_error("Missing collection UIA identity: " + identity);
    }
    return result;
}
ComPtr<IUIAutomationElement> virtual_item(IUIAutomationItemContainerPattern* container, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationElement> result;
    const auto hr = container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, value, &result); VariantClear(&value);
    success(hr, "Find virtual identity without row scan"); require(result != nullptr, "Virtual item exists"); return result;
}
void automation(HWND hwnd) {
    success(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "UIA apartment");
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    ComPtr<IUIAutomation> uia; success(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)), "UIA client");
    ComPtr<IUIAutomationElement> root; success(uia->ElementFromHandle(hwnd, &root), "Owned host provider");
    auto items = find(uia.Get(), root.Get(), L"items");
    auto container = pattern<IUIAutomationItemContainerPattern>(items.Get(), UIA_ItemContainerPatternId);
    auto last = virtual_item(container.Get(), L"1000000:1");
    BOOL offscreen{}; success(last->get_CurrentIsOffscreen(&offscreen), "Virtual item offscreen state"); require(offscreen, "Unrealized last item has no visible bounds");
    success(pattern<IUIAutomationVirtualizedItemPattern>(last.Get(), UIA_VirtualizedItemPatternId)->Realize(), "Realize last virtual item");
    auto selected = pattern<IUIAutomationSelectionItemPattern>(last.Get(), UIA_SelectionItemPatternId);
    success(selected->Select(), "Select last stable identity on UI thread");
    auto previous = virtual_item(container.Get(), L"999999:1");
    success(pattern<IUIAutomationSelectionItemPattern>(previous.Get(), UIA_SelectionItemPatternId)->AddToSelection(), "Add virtual item without replacing selection");
    BOOL value{}; selected->get_CurrentIsSelected(&value); require(value, "SelectionItem Add retains previous item");
    auto selection = pattern<IUIAutomationSelectionPattern>(items.Get(), UIA_SelectionPatternId);
    selection->get_CurrentCanSelectMultiple(&value); require(value, "Collection exposes multiple selection");
    ComPtr<IUIAutomationElementArray> selected_items; success(selection->GetCurrentSelection(&selected_items), "Enumerate bounded selected identities");
    int count{}; selected_items->get_Length(&count); require(count == 2, "Selected array contains two stable items");
    auto inline_action = find(uia.Get(), last.Get(), L"1000000:1:action");
    success(pattern<IUIAutomationInvokePattern>(inline_action.Get(), UIA_InvokePatternId)->Invoke(), "Invoke distinct inline action");
    success(pattern<IUIAutomationSelectionItemPattern>(previous.Get(), UIA_SelectionItemPatternId)->RemoveFromSelection(), "Remove only one selected item");
    auto tree = find(uia.Get(), root.Get(), L"tree");
    CONTROLTYPEID role{}; tree->get_CurrentControlType(&role); require(role == UIA_TreeControlTypeId, "Tree exposes a hierarchy, not a table");
    auto tree_container = pattern<IUIAutomationItemContainerPattern>(tree.Get(), UIA_ItemContainerPatternId);
    auto branch = virtual_item(tree_container.Get(), L"1:1");
    auto disclosure = pattern<IUIAutomationExpandCollapsePattern>(branch.Get(), UIA_ExpandCollapsePatternId);
    success(disclosure->Expand(), "UIA starts lazy child query");
    auto child = virtual_item(tree_container.Get(), L"1000001:1");
    child->get_CurrentControlType(&role); require(role == UIA_TreeItemControlTypeId, "Virtual child has tree-item semantics");
    ComPtr<IUIAutomationTreeWalker> walker; success(uia->get_ControlViewWalker(&walker), "Hierarchy walker");
    ComPtr<IUIAutomationElement> parent; success(walker->GetParentElement(child.Get(), &parent), "Child parent navigation");
    BSTR id{}; parent->get_CurrentAutomationId(&id); const bool correct = id && std::wstring_view(id) == L"1:1"; SysFreeString(id);
    require(correct, "Tree child provider navigates to its real parent");
    success(disclosure->Collapse(), "UIA collapses branch");
    require(FAILED(child->get_CurrentName(&id)), "Collapsed virtual provider rejects stale visible access");
    auto grid = find(uia.Get(), root.Get(), L"table");
    auto table = pattern<IUIAutomationGridPattern>(grid.Get(), UIA_GridPatternId);
    table->get_CurrentRowCount(&count); require(count == 100000, "DataGrid source compatibility preserves virtual count");
    auto check = find(uia.Get(), grid.Get(), L"table-5-0-0-0");
    auto toggle = pattern<IUIAutomationTogglePattern>(check.Get(), UIA_TogglePatternId);
    success(toggle->Toggle(), "Header select-all action");
    ToggleState state{}; toggle->get_CurrentToggleState(&state); require(state == ToggleState_On, "Header exposes all state");
    ComPtr<IUIAutomationElement> cell; success(table->GetItem(10, 0, &cell), "Virtual check cell");
    success(pattern<IUIAutomationTogglePattern>(cell.Get(), UIA_TogglePatternId)->Toggle(), "Checkbox cell toggles independently of sort");
    success(toggle->get_CurrentToggleState(&state), "Read mixed header state");
    require(state == ToggleState_Indeterminate, "Header exposes mixed state");
    auto filter = find(uia.Get(), grid.Get(), L"table-4-0-0-0");
    auto filter_value = pattern<IUIAutomationValuePattern>(filter.Get(), UIA_ValuePatternId);
    auto query = SysAllocString(L"even"); const auto filter_result = filter_value->SetValue(query); SysFreeString(query);
    success(filter_result, "Header filter Value marshals external query");
    table->get_CurrentRowCount(&count); require(count == 50000, "Accepted header filter replaces source");
    query = SysAllocString(L""); const auto clear_result = filter_value->SetValue(query); SysFreeString(query);
    success(clear_result, "Clear filter"); table->get_CurrentRowCount(&count); require(count == 100000, "Filter clear restores full source");
    Sleep(150); const auto paints = SendMessageW(hwnd, metrics, 0, 0); Sleep(200);
    require(SendMessageW(hwnd, metrics, 0, 0) == paints, "Collections have zero idle paints");
    require(SendMessageW(hwnd, metrics, 14, 0) == 8, "Closed popup peers retire after input dispatch");
    PostMessageW(hwnd, WM_KEYDOWN, VK_F11, 0);
    const auto deadline = GetTickCount64() + 3000;
    while (IsWindow(hwnd) && GetTickCount64() < deadline) Sleep(10);
    require(!IsWindow(hwnd) && FAILED(selected->Select()), "Retained UIA child rejects actions after owner teardown");
}
void miller_appearance(Window& window, HWND hwnd, MillerColumns& columns, TextInput& edit) {
    struct Snapshot {
        HDC dc{};
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        ~Snapshot() {
            if (previous) SelectObject(dc, previous);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
    };
    const UINT original_dpi = GetDpiForWindow(hwnd);
    RECT original{}; GetWindowRect(hwnd, &original);
    const auto first = columns.column_list(0);
    columns.set_active_column(0); flush(hwnd);
    require(window.focus(*first), "Focus the Miller list to identify its native peer");
    const auto first_hwnd = GetFocus();
    require(first_hwnd != nullptr && first->focused(), "Identify the list peer, not its same-named header");
    require(window.focus(edit), "Restore editor focus before hover checks");
    for (auto style : {VisualStyle::classic, VisualStyle::winui})
        for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
            for (UINT dpi : {96u, 120u, 144u, 192u}) {
                std::cout << "Miller pixels: style " << static_cast<int>(style) << ", theme " <<
                    static_cast<int>(theme) << ", DPI " << dpi << std::endl;
                window.set_visual_style(style); window.set_theme(theme);
                RECT rectangle = original;
                rectangle.right = rectangle.left + MulDiv(700, dpi, 96);
                rectangle.bottom = rectangle.top + MulDiv(480, dpi, 96);
                SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rectangle));
                columns.set_active_column(0); columns.set_horizontal_offset(0); first->set_offset(0);
                window.focus(edit); flush(hwnd);
                const auto selected = first->selection().focused();
                const auto palette = Palette::system(theme, style);
                const auto color = [](D2D1_COLOR_F value) {
                    return RGB(int(value.r * 255 + .5f), int(value.g * 255 + .5f), int(value.b * 255 + .5f));
                };
                RECT outer{}; GetWindowRect(hwnd, &outer);
                POINT client{}; ClientToScreen(hwnd, &client);
                Snapshot capture;
                capture.dc = CreateCompatibleDC(nullptr);
                require(capture.dc != nullptr, "Create owned Miller capture context");
                BITMAPINFO info{};
                info.bmiHeader = {sizeof(BITMAPINFOHEADER), outer.right - outer.left, -(outer.bottom - outer.top), 1, 32, BI_RGB};
                void* pixels{};
                capture.bitmap = CreateDIBSection(capture.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
                require(capture.bitmap != nullptr, "Create owned Miller capture bitmap");
                capture.previous = SelectObject(capture.dc, capture.bitmap);
                const auto paint = [&] {
                    flush(hwnd);
                    require(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW),
                        "Paint Miller transitions");
                    require(PrintWindow(hwnd, capture.dc, 2), "Capture only the owned Miller window");
                };
                const auto sample = [&](float x, float y) {
                    const auto result = GetPixel(capture.dc, int(x * dpi / 96) + client.x - outer.left,
                        int(y * dpi / 96) + client.y - outer.top);
                    require(result != CLR_INVALID, "Miller sample stays inside the owned window");
                    return result;
                };
                const auto at = [&](float x, float y) {
                    return MAKELPARAM(static_cast<int>(std::lround(x * dpi / 96)), static_cast<int>(std::lround(y * dpi / 96)));
                };
                const auto row_color = [&](float y) {
                    const auto b = first->bounds(); return sample(b.x + 100, b.y + y);
                };
                SendMessageW(first_hwnd, WM_MOUSELEAVE, 0, 0); paint();
                require(row_color(45) == color(palette.background), "Unselected Miller rows start without hover");
                SendMessageW(first_hwnd, WM_MOUSEMOVE, 0, at(20, 45)); paint();
                if (first->hovered_row() != 1 || row_color(45) != color(palette.hover)) {
                    const auto hovered = first->hovered_row();
                    std::cerr << "Miller hover: row=" << (hovered ? std::to_string(*hovered) : "none") <<
                        ", offset=" << first->offset() << ", capture=" << GetCapture() <<
                        ", actual=" << std::hex << row_color(45) << ", expected=" << color(palette.hover) <<
                        std::dec << '\n';
                }
                require(first->hovered_row() == 1 && row_color(45) == color(palette.hover),
                    "Native pointer movement paints Miller row hover in every theme and DPI");
                SendMessageW(first_hwnd, WM_MOUSEMOVE, 0, at(20, 85)); paint();
                require(row_color(45) == color(palette.background) && row_color(85) == color(palette.hover),
                    "Hover moves between rows without leaving the previous highlight");
                require(first->selection().focused() == selected && edit.focused() && columns.active_column() == 0,
                    "Hover leaves selection, editor focus and active column unchanged");
                SetCapture(first_hwnd);
                SendMessageW(first_hwnd, WM_MOUSEMOVE, MK_LBUTTON, at(20, 85)); paint();
                require(!first->hovered_row() && row_color(85) == color(palette.background),
                    "Captured pointer gestures suppress the row hover");
                SendMessageW(first_hwnd, WM_CANCELMODE, 0, 0);
                require(GetCapture() != first_hwnd, "Cancellation releases the synthetic capture");
                SendMessageW(first_hwnd, WM_MOUSEMOVE, 0, at(20, 45));
                SendMessageW(first_hwnd, WM_MOUSEMOVE, 0, at(first->bounds().width - 2, 45)); paint();
                require(!first->hovered_row() && row_color(45) == color(palette.background),
                    "Vertical scrollbar input does not highlight a row");
                SendMessageW(first_hwnd, WM_MOUSEMOVE, 0, at(20, 45));
                SendMessageW(first_hwnd, WM_MOUSELEAVE, 0, 0); paint();
                require(!first->hovered_row() && row_color(45) == color(palette.background), "Pointer leave clears visible hover");
                for (double offset : {0.0, 43.25, columns.maximum_horizontal()}) {
                    columns.set_horizontal_offset(offset); paint();
                    bool found{};
                    for (std::size_t i = 0; i + 1 < columns.columns().size(); ++i) {
                        const auto separator = columns.separator_bounds(i);
                        if (separator.width <= 0) continue;
                        const float scale = dpi / 96.0f;
                        const float left = std::round(separator.x * scale);
                        const float right = std::round((separator.x + separator.width) * scale);
                        const float x = columns.bounds().x + (left + right) / (2 * scale);
                        if (sample(x, columns.bounds().y + separator.y + 8) != color(palette.border) ||
                            sample(x, columns.bounds().y + separator.y + 60) != color(palette.border))
                            std::cerr << "Miller separator: offset=" << offset << ", column=" << i <<
                                ", bounds=" << separator.x << "," << separator.y << "," << separator.width << "," << separator.height <<
                                ", origin=" << columns.bounds().x << "," << columns.bounds().y <<
                                ", header=" << std::hex << sample(x, columns.bounds().y + separator.y + 8) <<
                                ", list=" << sample(x, columns.bounds().y + separator.y + 60) <<
                                ", expected=" << color(palette.border) << std::dec << '\n';
                        require(sample(x, columns.bounds().y + separator.y + 8) == color(palette.border) &&
                            sample(x, columns.bounds().y + separator.y + 60) == color(palette.border),
                            "A pixel-aligned separator spans the header and list after fractional horizontal scrolling");
                        found = true;
                    }
                    require(found, "The fixture has at least one visible adjacent-column separator");
                }
            }
    window.set_visual_style(VisualStyle::classic); window.set_theme(ThemeMode::dark);
    SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(original_dpi, original_dpi), reinterpret_cast<LPARAM>(&original));
    columns.set_active_column(0); columns.set_horizontal_offset(0); flush(hwnd);
}
void miller_window() {
    Window window({L"XUI Miller host contracts", {700, 480}, ThemeMode::dark});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto columns = std::make_shared<MillerColumns>(L"Folders");
    auto edit = std::make_shared<TextInput>(L"Find in folders");
    auto source = std::make_shared<Items>(1000000);
    std::vector<MillerColumn> path;
    for (size_t i = 0; i < 8; ++i)
        path.push_back({L"Level " + std::to_wstring(i), source, ItemKey{1, 1}});
    columns->set_column_width(220);
    columns->set_columns(path);
    columns->column_list(0)->set_automation_id(L"miller-parent");
    root->add(columns, 1); root->add(edit);
    window.set_content(root);
    unsigned selections{}, activations{};
    columns->on_selection([&](size_t column, ItemKey key) {
        require(column == 0 && key == ItemKey{2, 1}, "Selection preserves column and key");
        ++selections;
    });
    columns->on_activate([&](size_t column, ItemKey key) {
        require(column == 0 && key == ItemKey{2, 1}, "Activation preserves column and key");
        ++activations;
    });
    bool completed{};
    std::atomic<bool> native_done{};
    Control* pointer_key_target{};
    unsigned pointer_keys{};
    window.on_key([&](const KeyEvent& event) {
        if (event.key == Key::f10) {
            require(event.target == pointer_key_target, "Column pointer focus scopes keyboard input to its native list");
            ++pointer_keys; return true;
        }
        if (event.key == Key::f11) {
            require(pointer_keys == 2, "Header and whitespace focus preserve scoped keyboard dispatch");
            columns->set_columns({path.front()});
            require(columns->active_column() == 0 && !columns->column_list(7)->visible() &&
                columns->horizontal_offset() == 0 && columns->horizontal_track().width == 0,
                "Path replacement hides stale peers and removes the horizontal scrollbar");
            require(selections == 1 && activations == 1, "All scrolling leaves selection and activation unchanged");
            completed = true; window.close(); return true;
        }
        if (event.key != Key::f12) return false;
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI Miller host contracts");
        require(hwnd != nullptr, "Find owned Miller host");
        window.focus(*edit);
        columns->set_columns({path.front()}); flush(hwnd);
        columns->set_columns(path); flush(hwnd);
        const auto appended = columns->column_list(7)->bounds();
        require(columns->active_column() == 0 && edit->focused() &&
            appended.x >= columns->bounds().x &&
            appended.x + appended.width <= columns->bounds().x + columns->bounds().width &&
            columns->horizontal_offset() == columns->maximum_horizontal(),
            "Appending a deep path reveals the final column after native layout without stealing editor focus");
        columns->set_active_column(0); flush(hwnd);
        auto first = columns->column_list(0), second = columns->column_list(1);
        const auto point_at = [&](float x, float y) {
            const float scale = GetDpiForWindow(hwnd) / 96.0f;
            return MAKELPARAM(static_cast<int>(std::lround(x * scale)), static_cast<int>(std::lround(y * scale)));
        };
        auto filtered = path;
        for (auto& column : filtered) column.source = std::make_shared<Items>(2);
        columns->set_columns(filtered); flush(hwnd);
        unsigned column_focuses{};
        second->on_focus([&] { ++column_focuses; });
        window.focus(*edit);
        const auto edit_hwnd = GetFocus();
        edit->set_text(L"Find query");
        flush(hwnd);
        SendMessageW(edit_hwnd, EM_SETSEL, 2, 6);
        filtered[0].source = std::make_shared<Items>(1);
        columns->set_columns(filtered);
        columns->set_active_column(1);
        columns->set_column_width(221); columns->set_column_width(220);
        columns->set_horizontal_offset(0); flush(hwnd);
        DWORD selection_start{}, selection_end{};
        SendMessageW(edit_hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start), reinterpret_cast<LPARAM>(&selection_end));
        require(GetFocus() == edit_hwnd && edit->focused() && edit->text() == L"Find query" &&
            selection_start == 2 && selection_end == 6 && !column_focuses && !selections && !activations &&
            first->selection().contains({1, 1}) && second->selection().contains({1, 1}) && columns->columns().size() == path.size(),
            "Filtering and property updates preserve native editor focus, text range, rows and descendants");
        columns->set_active_column(0); flush(hwnd);
        const auto second_header = native(hwnd, L"Level 1");
        SendMessageW(second_header, WM_LBUTTONDOWN, MK_LBUTTON, point_at(20, 12));
        SendMessageW(second_header, WM_LBUTTONUP, 0, point_at(20, 12)); flush(hwnd);
        const auto second_hwnd = GetFocus();
        require(second->focused() && second_hwnd != second_header && columns->active_column() == 1 &&
            column_focuses == 1 && second->selection().contains({1, 1}) && !selections && !activations &&
            columns->columns().size() == path.size(),
            "Header pointer input focuses the named native list without selecting or trimming descendants");
        pointer_key_target = second.get();
        PostMessageW(second_hwnd, WM_KEYDOWN, VK_F10, 0);
        window.focus(*edit);
        columns->set_active_column(0); flush(hwnd);
        SendMessageW(second_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point_at(20, 180));
        SendMessageW(second_hwnd, WM_LBUTTONUP, 0, point_at(20, 180)); flush(hwnd);
        require(GetFocus() == second_hwnd && second->focused() && columns->active_column() == 1 &&
            column_focuses == 2 && second->selection().contains({1, 1}) && !selections && !activations &&
            columns->columns().size() == path.size(),
            "Empty body pointer input focuses its list without changing row selection or descendants");
        PostMessageW(second_hwnd, WM_KEYDOWN, VK_F10, 0);
        window.focus(*edit);
        filtered[1] = {L"Empty column", {}, {}};
        columns->set_columns(filtered); flush(hwnd);
        require(GetFocus() == edit_hwnd && column_focuses == 2, "An empty filter result does not steal Find focus");
        SendMessageW(second_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point_at(20, 12));
        SendMessageW(second_hwnd, WM_LBUTTONUP, 0, point_at(20, 12)); flush(hwnd);
        require(GetFocus() == second_hwnd && second->selection().empty() && column_focuses == 3 &&
            !selections && !activations && columns->columns().size() == path.size(),
            "A column with no rows still accepts focus without row events");
        window.focus(*edit);
        columns->set_enabled(false); flush(hwnd);
        SendMessageW(second_header, WM_LBUTTONDOWN, MK_LBUTTON, point_at(20, 12));
        SendMessageW(second_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point_at(20, 12));
        require(GetFocus() == edit_hwnd && column_focuses == 3, "Disabled headers and empty lists reject pointer focus");
        columns->set_enabled(true);
        second->on_focus({});
        columns->set_columns(path); columns->set_active_column(0); flush(hwnd);
        window.focus(*edit);
        first->step(1);
        require(edit->focused() && !first->focused() && selections == 1 && !activations,
            "Programmatic selection preserves native editor focus and does not activate");
        window.focus(*first); flush(hwnd);
        auto first_hwnd = GetFocus();
        require(first->focused() && first_hwnd, "First Miller list receives native focus");
        SendMessageW(first_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point_at(20, 52));
        SendMessageW(first_hwnd, WM_LBUTTONUP, 0, point_at(20, 52));
        require(first->selection().contains({2, 1}) && selections == 1 && !activations,
            "Clicking an existing selected row retains ordinary row selection without activation");
        const auto vertical_viewport = first->content_viewport();
        const auto vertical_track = point_at(vertical_viewport.x + vertical_viewport.width + 2,
            vertical_viewport.y + vertical_viewport.height - 1);
        SendMessageW(first_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, vertical_track);
        SendMessageW(first_hwnd, WM_LBUTTONUP, 0, vertical_track);
        require(first->offset() > 0 && first->selection().contains({2, 1}) && selections == 1 && !activations,
            "Vertical scrollbar clicks retain native paging without changing row selection");
        first->set_offset(0); flush(hwnd);
        SendMessageW(first_hwnd, WM_KEYDOWN, VK_RIGHT, 0); flush(hwnd);
        require(columns->active_column() == 1 && second->focused(),
            "C++ host wires Right to native focus in the next column");
        SendMessageW(GetFocus(), WM_KEYDOWN, VK_LEFT, 0); flush(hwnd);
        require(columns->active_column() == 0 && first->focused(), "Left restores native focus to the parent column");
        SendMessageW(GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
        require(activations == 1 && selections == 1, "Enter activates without duplicate selection");
        first->on_context_menu([] { return std::vector<MenuItem>{}; });
        POINT point{20, 12}; ClientToScreen(first_hwnd, &point);
        SendMessageW(first_hwnd, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(first_hwnd), MAKELPARAM(point.x, point.y));
        require(first->selection().focused() == ItemKey{1, 1} && selections == 1,
            "Context click selects its row without starting a child query");
        columns->set_columns({path.front()}); flush(hwnd);
        columns->set_columns(path); flush(hwnd);
        require(first->focused() && GetFocus() == first_hwnd && columns->active_column() == 0 &&
            columns->horizontal_offset() == columns->maximum_horizontal(),
            "A new column stays revealed while its clipped ancestor retains native keyboard focus");
        columns->set_active_column(7); flush(hwnd);
        require(columns->column_list(7)->bounds().width > 0 &&
            columns->column_list(7)->bounds().x >= columns->bounds().x &&
            columns->column_list(7)->bounds().x + columns->column_list(7)->bounds().width <=
                columns->bounds().x + columns->bounds().width,
            "Deep active column is fully inside the horizontal viewport");
        require(source->reads < 3000, "Native paint requests bounded visible content");
        auto last = columns->column_list(7);
        window.focus(*last); flush(hwnd);
        auto last_hwnd = GetFocus(), columns_hwnd = native(hwnd, L"Folders");
        const double end = columns->maximum_horizontal();
        last->set_offset(80);
        SendMessageW(last_hwnd, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-30)), 0); flush(hwnd);
        require(columns->horizontal_offset() == end - 24 && columns->active_column() == 7 && last->offset() == 80,
            "Fractional horizontal wheel input moves the viewport without changing the active column or vertical offset");
        SendMessageW(last_hwnd, WM_MOUSEWHEEL, MAKEWPARAM(MK_SHIFT, 60), 0); flush(hwnd);
        require(columns->horizontal_offset() == end - 72 && last->offset() == 80,
            "Shift plus wheel scrolls horizontally and retains fractional input");
        SendMessageW(last_hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), 0); flush(hwnd);
        require(columns->horizontal_offset() == end - 72 && last->offset() > 80,
            "Unmodified wheel input still scrolls only the pointed column vertically");
        SendMessageW(native(hwnd, L"Level 7"), WM_MOUSEHWHEEL, MAKEWPARAM(0, 30), 0); flush(hwnd);
        require(columns->horizontal_offset() == end - 48, "Header wheel input routes to the Miller viewport");
        SendMessageW(last_hwnd, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-12000)), 0); flush(hwnd);
        require(columns->horizontal_offset() == 0 && columns->active_column() == 7,
            "Wheel scrolling can hide the focused column without snapping back or changing the active column");
        window.focus(*edit);
        columns->set_horizontal_offset(0); flush(hwnd);
        const auto track = columns->horizontal_track(), thumb = columns->horizontal_thumb();
        const auto start = point_at(thumb.width / 2, track.y + track.height / 2);
        const auto finish = point_at(track.width - 1, track.y + track.height / 2);
        SendMessageW(columns_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, finish); flush(hwnd);
        require(columns->horizontal_offset() > 0 && columns->active_column() == 7 && edit->focused(),
            "A track click pages without changing selection, active column or native editor focus");
        columns->set_horizontal_offset(0); flush(hwnd);
        SendMessageW(columns_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, start);
        require(GetCapture() == columns_hwnd, "Horizontal thumb drag captures the pointer");
        SendMessageW(columns_hwnd, WM_MOUSEMOVE, MK_LBUTTON, finish); flush(hwnd);
        require(columns->horizontal_offset() == end, "Dragging the thumb reaches the final column");
        SendMessageW(columns_hwnd, WM_LBUTTONUP, 0, finish);
        require(GetCapture() != columns_hwnd, "Thumb release clears pointer capture");
        columns->set_horizontal_offset(0); flush(hwnd);
        SendMessageW(columns_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, start);
        SendMessageW(columns_hwnd, WM_CANCELMODE, 0, 0);
        SendMessageW(columns_hwnd, WM_MOUSEMOVE, 0, finish); flush(hwnd);
        require(GetCapture() != columns_hwnd && columns->horizontal_offset() == 0, "Cancelled thumb drags cannot continue");
        columns->set_enabled(false); flush(hwnd);
        SendMessageW(columns_hwnd, WM_MOUSEHWHEEL, MAKEWPARAM(0, 120), 0);
        SendMessageW(columns_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, finish); flush(hwnd);
        require(columns->horizontal_offset() == 0 && GetCapture() != columns_hwnd, "Disabled columns reject wheel and track input");
        columns->set_enabled(true); flush(hwnd);
        miller_appearance(window, hwnd, *columns, *edit);
        require(selections == 1 && activations == 1, "Hover and separator changes dispatch no selection or activation");
        window.focus(*first);
        columns->set_horizontal_offset(columns->maximum_horizontal()); flush(hwnd);
        require(first->focused(), "Manual scrolling retains focus for the offscreen accessibility checks");
        native_done = true;
        return true;
    });
    std::exception_ptr automation_error;
    std::atomic<bool> driver_exited{};
    std::jthread driver([&](std::stop_token stop) {
        struct Finished { std::atomic<bool>& value; ~Finished() { value = true; } } finished{driver_exited};
        const auto deadline = GetTickCount64() + 60000;
        while (!stop.stop_requested() && GetTickCount64() < deadline) {
            if (const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI Miller host contracts"); hwnd && IsWindowVisible(hwnd)) {
                const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                try {
                    success(initialized, "Initialize Miller UIA client");
                    PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
                    while (!native_done && IsWindow(hwnd) && GetTickCount64() < deadline) Sleep(10);
                    require(native_done, "Native Miller scrolling contracts complete before UIA actions");
                    ComPtr<IUIAutomation> automation;
                    success(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)), "Create Miller UIA client");
                    ComPtr<IUIAutomationElement> element;
                    success(automation->ElementFromHandle(native(hwnd, L"Folders"), &element), "Read Miller automation element");
                    auto parent = find(automation.Get(), element.Get(), L"miller-parent");
                    BOOL offscreen{}, focused{};
                    success(parent->get_CurrentIsOffscreen(&offscreen), "Read clipped parent visibility");
                    success(parent->get_CurrentHasKeyboardFocus(&focused), "Read clipped parent focus");
                    require(offscreen && focused, "A clipped parent remains focused but reports offscreen through UIA");
                    auto selection = pattern<IUIAutomationSelectionPattern>(parent.Get(), UIA_SelectionPatternId);
                    ComPtr<IUIAutomationElementArray> selected;
                    success(selection->GetCurrentSelection(&selected), "Read clipped parent selection");
                    ComPtr<IUIAutomationElement> row;
                    success(selected->GetElement(0, &row), "Read clipped selected row");
                    success(row->get_CurrentIsOffscreen(&offscreen), "Read clipped row visibility");
                    require(offscreen, "Rows in a clipped parent also report offscreen through UIA");
                    ComPtr<IUIAutomationScrollPattern> scroll;
                    success(element->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scroll)), "Miller exposes horizontal ScrollPattern");
                    BOOL horizontal{}, vertical{}; double percent{}, size{};
                    success(scroll->get_CurrentHorizontallyScrollable(&horizontal), "Read horizontal availability");
                    success(scroll->get_CurrentVerticallyScrollable(&vertical), "Read vertical availability");
                    success(scroll->get_CurrentHorizontalViewSize(&size), "Read horizontal view size");
                    require(horizontal && !vertical && size > 0 && size < 100, "Miller root exposes its horizontal axis only");
                    success(scroll->SetScrollPercent(25, UIA_ScrollPatternNoScroll), "Scroll Miller through UIA");
                    success(scroll->get_CurrentHorizontalScrollPercent(&percent), "Read horizontal percent");
                    require(std::abs(percent - 25) < 0.01, "UIA horizontal percentage matches the requested position");
                    success(scroll->Scroll(ScrollAmount_SmallIncrement, ScrollAmount_NoAmount), "Increment horizontal UIA scrolling");
                    success(scroll->get_CurrentHorizontalScrollPercent(&percent), "Read incremented horizontal percent");
                    require(percent > 25, "UIA scrolling advances the viewport");
                    success(scroll->get_CurrentVerticalScrollPercent(&percent), "Read absent vertical percent");
                    require(percent == UIA_ScrollPatternNoScroll, "Miller root does not report column-specific vertical scrolling");
                    require(scroll->SetScrollPercent(std::numeric_limits<double>::quiet_NaN(), UIA_ScrollPatternNoScroll) == E_INVALIDARG,
                        "UIA rejects non-finite horizontal percentages");
                    scroll.Reset(); element.Reset(); automation.Reset();
                    PostMessageW(hwnd, WM_KEYDOWN, VK_F11, 0);
                } catch (...) {
                    automation_error = std::current_exception();
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
                if (SUCCEEDED(initialized)) CoUninitialize();
                return;
            }
            Sleep(10);
        }
        window.post([&] { window.close(); });
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    while (!driver_exited) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    driver.request_stop(); driver.join();
    if (automation_error) std::rethrow_exception(automation_error);
    require(result == 0 && completed, "Miller native host contracts pass");
    require(Drawing::live_targets() == 0, "Miller window releases its drawing target");
    std::cout << "Miller native focus, context selection, viewport and lifecycle contracts passed\n";
}

void single_click_activation(Window& window, HWND hwnd, ItemsView& items, UINT dpi) {
    const auto peer = native(hwnd, L"Items");
    const auto source = items.source();
    int activations = 0;
    items.on_activate([&](ItemKey) { ++activations; });
    const auto click = [&](WPARAM modifiers = 0) {
        const auto row = items.item_bounds(0);
        const auto point = MAKELPARAM(static_cast<int>((row.x + 12) * dpi / 96),
            static_cast<int>((row.y + 12) * dpi / 96));
        SendMessageW(peer, WM_LBUTTONDOWN, MK_LBUTTON | modifiers, point);
        SendMessageW(peer, WM_LBUTTONUP, modifiers, point);
    };
    require(!items.single_click_activation(), "Ordinary items require explicit activation by default");
    click();
    require(activations == 0, "Default single clicks only select");
    SendMessageW(peer, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(12, 12));
    require(activations == 1, "Default double clicks still activate");
    items.set_single_click_activation(true);
    activations = 0;
    items.select(source->key(1));
    window.focus(items);
    SendMessageW(peer, WM_KEYDOWN, VK_HOME, 0);
    SendMessageW(peer, WM_KEYDOWN, VK_DOWN, 0);
    require(activations == 0, "Opt-in activation does not turn selection or arrow keys into activation");
    click(); click();
    require(activations == 2, "Single clicks activate both a new and an already-selected row");
    SendMessageW(peer, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(12, 12));
    require(activations == 2, "The double-click message does not repeat single-click activation");
    SendMessageW(peer, WM_KEYDOWN, VK_RETURN, 0);
    require(activations == 3, "Enter still activates");
    click(MK_CONTROL); click(MK_SHIFT);
    require(activations == 3, "Modified clicks retain selection gestures without activation");
    items.set_enabled(false); click(); items.set_enabled(true);
    require(activations == 3, "Disabled items reject single clicks");
    items.on_selection([&] {
        items.on_selection({});
        items.set_items(std::make_shared<Items>(3));
    });
    click();
    require(activations == 3, "A selection callback cannot activate a row from a replaced snapshot");
    items.on_selection([&] {
        items.on_selection({});
        items.set_visible(false);
    });
    click();
    require(activations == 3, "A selection callback that hides the control cancels activation");
    items.set_visible(true);
    items.on_activate({});
    items.set_single_click_activation(false);
    items.set_items(source, source);
    items.set_offset(0);
    flush(hwnd);
}
void run(ThemeMode theme, UINT dpi, bool palette_only = false) {
    Window window({L"XUI collection contracts", {920, 760}, theme});
    auto root = std::make_shared<Stack>(Axis::vertical); root->set_spacing(6); root->set_padding({10, 10, 10, 10});
    auto source = std::make_shared<Items>();
    auto items = std::make_shared<ItemsView>(L"Items"); items->set_automation_id(L"items"); items->set_items(source, source);
    items->set_help_text(L"Collection help"); items->set_tooltip_delay(100);
    auto tree = std::make_shared<TreeView>(L"Tree"); tree->set_automation_id(L"tree"); tree->set_tree(std::make_shared<Tree>());
    auto detail = std::make_shared<Stack>(Axis::vertical); detail->add(tree, 1);
    auto overlay_edit = std::make_shared<TextInput>(L"Overlay native field"); overlay_edit->set_caption_visible(false);
    overlay_edit->set_preferred_size({600, 44}); overlay_edit->set_text(L"Native field below the adaptive overlay"); detail->add(overlay_edit);
    auto panes = std::make_shared<AdaptiveLayout>(items, detail); panes->set_preferred_size({850, 260}); panes->set_navigation_extent(420);
    root->add(panes);
    auto grid = std::make_shared<DataGrid>(L"Table"); grid->set_automation_id(L"table");
    grid->set_columns({{L"Name", 280, false, true, true}, {L"Value", 160, true}});
    grid->set_source(std::make_shared<Rows>()); grid->set_preferred_size({850, 200}); root->add(grid);
    auto anchor = std::make_shared<Button>(L"Popup anchor"); root->add(anchor);
    auto edit = std::make_shared<TextInput>(L"Native field"); edit->set_text(L"Atomic native composition"); root->add(edit);
    auto wrap = std::make_shared<Wrap>(); wrap->set_preferred_size({850, 40});
    wrap->add(std::make_shared<Button>(L"Wrapped one")); wrap->add(std::make_shared<Button>(L"Wrapped two")); root->add(wrap);
    int actions{}, sorts{};
    items->on_action([&](ItemKey key) { require(key.id == 1000000, "Inline action retains stable ID"); ++actions; });
    tree->on_request([tree = tree.get()](TreeRequest request) {
        tree->complete(request, std::make_shared<Items>(1000000, request.node.id * 1000000 + 1));
    });
    grid->on_filter([grid = grid.get()](GridFilterRequest request) {
        const bool even = request.filters[0] == L"even"; grid->complete_filter(request, std::make_shared<Rows>(even ? 50000 : 100000, false, even ? 2 : 1));
    });
    grid->on_sort([&](auto, auto) { ++sorts; });
    window.set_content(root);
    const auto check_palette = [&](HWND hwnd) {
        // PrintWindow also prints native EDIT children; isolate the palette geometry capture.
        overlay_edit->set_visible(false); edit->set_visible(false); flush(hwnd);
        palette_appearance(window, hwnd, *anchor, theme, dpi);
        overlay_edit->set_visible(true); edit->set_visible(true); flush(hwnd);
    };
    std::atomic<bool> native_done{}, driver_done{}, driver_exited{}; std::wstring driver_error;
    std::optional<GridFilterRequest> closing_filter;
    std::optional<TreeRequest> closing_tree;
    window.on_key([&](const KeyEvent& event) {
        if (event.key == Key::f11) {
            require(actions == 1 && !sorts, "UIA inline/filter/check actions never invoke unrelated actions");
            grid->on_filter([&](GridFilterRequest request) { closing_filter = request; });
            tree->on_request([&](TreeRequest request) { closing_tree = request; });
            grid->filter(0, L"pending"); tree->disclose({2, 1}, true);
            window.close(); return true;
        }
        if (event.key != Key::f12) return false;
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI collection contracts"); require(hwnd != nullptr, "Own collection host");
        RECT rect{}; GetWindowRect(hwnd, &rect); const auto actual = GetDpiForWindow(hwnd);
        rect.right = rect.left + MulDiv(rect.right - rect.left, dpi, actual); rect.bottom = rect.top + MulDiv(rect.bottom - rect.top, dpi, actual);
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect)); flush(hwnd);
        if (palette_only) {
            check_palette(hwnd);
            native_done = true; driver_done = true; window.close(); return true;
        }
        const auto items_hwnd = native(hwnd, L"Items"), tree_hwnd = native(hwnd, L"Tree"), grid_hwnd = native(hwnd, L"Table");
        single_click_activation(window, hwnd, *items, dpi);
        require(window.focus(*items), "Owned items receive native focus");
        SendMessageW(items_hwnd, WM_KEYDOWN, VK_HOME, 0); SendMessageW(items_hwnd, WM_KEYDOWN, VK_DOWN, 0);
        require(items->selection().focused() == ItemKey{2, 1}, "Native arrows select item IDs");
        BYTE keyboard[256]{}; GetKeyboardState(keyboard); const auto restore = [&] { SetKeyboardState(keyboard); };
        BYTE shift[256]{}; std::copy(std::begin(keyboard), std::end(keyboard), std::begin(shift)); shift[VK_SHIFT] = 0x80; SetKeyboardState(shift);
        SendMessageW(items_hwnd, WM_KEYDOWN, VK_DOWN, 0); restore();
        require(items->selection().contains({2, 1}) && items->selection().contains({3, 1}), "Native Shift+arrow selects range");
        for (const auto presentation : {ItemsPresentation::tiles, ItemsPresentation::gallery}) {
        items->set_item_size(presentation == ItemsPresentation::gallery ? Size{96, 128} : Size{180, 56});
        items->set_presentation(presentation); items->set_offset(0); flush(hwnd);
        const auto cols = items->columns(); require(cols >= 2, "Wide items have tile columns");
        const auto start = items->item_bounds(0), finish = items->item_bounds(cols + 1);
        SendMessageW(items_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<int>((start.x + 35) * dpi / 96), static_cast<int>((start.y + 10) * dpi / 96)));
        SendMessageW(items_hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(static_cast<int>((finish.x + 35) * dpi / 96), static_cast<int>((finish.y + 10) * dpi / 96)));
        SendMessageW(items_hwnd, WM_LBUTTONUP, 0, 0);
        require(items->selection().contains(source->key(cols + 1)) && items->selection().storage_size() == 1, "Native rectangle uses one selection term");
        SendMessageW(items_hwnd, WM_KEYDOWN, VK_HOME, 0); SendMessageW(items_hwnd, WM_KEYDOWN, VK_DOWN, 0);
        require(items->selection().focused() == source->key(cols), "Native tile and gallery arrows use the shared column count");
        }
        items->set_item_size({180, 56});
        items->set_presentation(ItemsPresentation::list);
        require(window.focus(*tree), "Tree receives native focus"); SendMessageW(tree_hwnd, WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(tree_hwnd, WM_KEYDOWN, VK_RIGHT, 0); SendMessageW(tree_hwnd, WM_KEYDOWN, VK_RIGHT, 0);
        require(tree->selection().focused() == ItemKey{1000001, 1}, "Native Right expands and enters child");
        SendMessageW(tree_hwnd, WM_KEYDOWN, VK_LEFT, 0); SendMessageW(tree_hwnd, WM_KEYDOWN, VK_LEFT, 0);
        require(!tree->expanded({1, 1}), "Native Left returns to and collapses parent");
        window.focus(*grid); SendMessageW(grid_hwnd, WM_KEYDOWN, VK_F6, 0);
        SendMessageW(grid_hwnd, WM_KEYDOWN, VK_F4, 0); SendMessageW(grid_hwnd, WM_KEYDOWN, VK_F4, 0);
        SendMessageW(grid_hwnd, WM_KEYDOWN, VK_SPACE, 0);
        require(grid->check_state() == SelectionState::all && sorts == 0, "Header check keyboard target does not sort");
        grid->toggle_check(); grid->focus_header(false);
        grid->reorder_column(0, 1); grid->set_column_width(1, 300); require(grid->source_column(1) == 0, "Native table retains logical identity");
        grid->reorder_column(1, 0);
        const auto focus_id = items->selection().focused(); window.focus(*items);
        panes->set_breakpoint(10000); flush(hwnd);
        panes->set_compact_navigation(CompactNavigation::overlay); flush(hwnd);
        require(panes->overlay_active() && GetFocus() == items_hwnd, "Inline-to-overlay keeps the native navigation peer focused");
        const auto underlying_edit = native(hwnd, L"Native field below the adaptive overlay");
        RECT clipped{}; require(GetWindowRgnBox(underlying_edit, &clipped) != ERROR, "Adaptive overlay clips the underlying native EDIT");
        panes->set_breakpoint(640); flush(hwnd);
        require(GetWindowRgnBox(underlying_edit, &clipped) == ERROR, "Inline transition restores the native EDIT region");
        require(items->selection().focused() == focus_id && GetFocus() == items_hwnd, "Compact/inline transition keeps selected and native focused control");
        items->set_enabled(false); const auto before = items->selection(); SendMessageW(items_hwnd, WM_KEYDOWN, VK_END, 0);
        require(before == items->selection(), "Disabled native input is rejected"); items->set_enabled(true);
        window.focus(*items); SendMessageW(items_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10)); SendMessageW(hwnd, WM_TIMER, 41, 0); flush(hwnd);
        require(SendMessageW(hwnd, metrics, 25, 0) && GetFocus() == items_hwnd, "Collection tooltip shares no-focus timing");
        SendMessageW(items_hwnd, WM_MOUSELEAVE, 0, 0); flush(hwnd);
        const auto peer_count = SendMessageW(hwnd, metrics, 14, 0);
        const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        auto popup_items = std::make_shared<ItemsView>(L"Popup items"); popup_items->set_items(source); popup_items->set_preferred_size({300, 150});
        auto popup = std::make_shared<Popup>(popup_items); popup->set_preferred_size({320, 180});
        for (int i = 0; i < 30; ++i) {
            window.show_popup(popup, *anchor, popup_items.get()); flush(hwnd);
            require(Drawing::live_targets() == 1, "Popup collection shares the root render target");
            window.dismiss_popup(*popup); flush(hwnd);
            items->set_offset((i % 2) ? items->maximum_offset() : 0); flush(hwnd);
        }
        require(SendMessageW(hwnd, metrics, 14, 0) <= peer_count + 2 &&
            GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi + 2 && GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= user + 3,
            "Repeated virtual scroll and popup cycles keep peers and GDI bounded");
        check_palette(hwnd);
        auto lazy = std::make_shared<TreeView>(L"Popup lazy tree"); lazy->set_tree(std::make_shared<Tree>());
        std::optional<TreeRequest> pending;
        lazy->on_request([&](TreeRequest request) { pending = request; });
        auto lazy_popup = std::make_shared<Popup>(lazy); lazy_popup->set_preferred_size({320, 150});
        window.show_popup(lazy_popup, *anchor, lazy.get()); lazy->disclose({1, 1}, true);
        require(pending.has_value(), "Popup tree starts application-owned work");
        window.dismiss_popup(*lazy_popup); flush(hwnd);
        require(pending->cancellation.stop_requested() && !lazy->complete(*pending, source), "Popup dismissal cancels lazy child work");
        lazy->on_request({});
        items->set_offset(0); tree->set_offset(0); window.focus(*anchor); flush(hwnd);
        SendMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0);
        for (int attempt = 0; attempt < 3 && Drawing::live_targets() != 1; ++attempt) { InvalidateRect(hwnd, nullptr, FALSE); flush(hwnd); }
        require(Drawing::live_targets() == 1, "Collections recover a lost root target");
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        const auto path = std::filesystem::path(executable).parent_path().parent_path() / L"collection-captures";
        suggestion_capture::bitmap(hwnd, nullptr, path / (L"collections-" + std::to_wstring(static_cast<int>(theme)) + L"-" + std::to_wstring(dpi) + L".bmp"));
        std::cout << "Collections theme=" << static_cast<int>(theme) << " DPI=" << dpi << " peers=" << peer_count <<
            " GDI=" << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << " USER=" << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) << " root targets=1\n";
        native_done = true; return true;
    });
    std::jthread driver([&] {
        struct Exit { std::atomic<bool>& value; ~Exit() { value = true; } } completion{driver_exited};
        HWND hwnd{}; const auto deadline = GetTickCount64() + 20000;
        while (GetTickCount64() < deadline) { hwnd = FindWindowW(L"Xui.Window.1", L"XUI collection contracts"); if (hwnd && IsWindowVisible(hwnd)) break; Sleep(10); }
        if (!hwnd) { driver_error = L"Native host did not start"; return; }
        PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
        while (!native_done && IsWindow(hwnd) && GetTickCount64() < deadline) Sleep(10);
        if (!native_done) { driver_error = L"Native phase failed"; PostMessageW(hwnd, WM_CLOSE, 0, 0); return; }
        if (palette_only) return;
        wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
        std::wstring command = L"\"" + std::wstring(exe) + L"\" --automation " + std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd));
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            const auto wait = WaitForSingleObject(process.hProcess, 30000); DWORD exit{1};
            if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exit); else TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread); CloseHandle(process.hProcess); driver_done = exit == 0;
            if (!driver_done) driver_error = L"Isolated UIA client failed";
        } else driver_error = L"UIA client did not start";
        PostMessageW(hwnd, WM_KEYDOWN, VK_F11, 0);
    });
    const auto result = Application::run(window);
    while (!driver_exited) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    driver.join();
    if (result || !driver_done) std::wcerr << window.error() << L" / " << driver_error << L'\n';
    require(result == 0 && driver_done, palette_only ? "Palette appearance matrix passes" : "Native and UIA collection matrix passes");
    if (palette_only) return;
    require(closing_filter && closing_filter->cancellation.stop_requested() && closing_tree && closing_tree->cancellation.stop_requested(),
        "Actual owner closure cancels filter and child requests");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--automation") { automation(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::stoull(argv[2])))); return 0; }
        success(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Keep the server apartment alive until retained UIA clients exit");
        struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
        if (argc == 2 && std::string_view(argv[1]) == "--miller-only") { miller_window(); return 0; }
        if (argc == 2 && std::string_view(argv[1]) == "--palette-only") {
            for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
                for (UINT dpi : {96u, 144u, 192u}) run(theme, dpi, true);
            require(Drawing::live_targets() == 0, "All palette targets retire");
            std::cout << "Palette appearance passed across three themes and three DPI scales\n"; return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--case") { run(static_cast<ThemeMode>(std::stoi(argv[2])), static_cast<UINT>(std::stoul(argv[3]))); return 0; }
        for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast}) for (UINT dpi : {96u, 144u, 192u}) run(theme, dpi);
        require(Drawing::live_targets() == 0, "All collection targets retire");
        std::cout << "Collection native/UIA matrix, resources, popup, tooltip, and DPI contracts passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

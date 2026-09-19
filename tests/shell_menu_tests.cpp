#include "../src/shell_commands_internal.hpp"
#include "../src/context_menu.hpp"
#include "xui/xui.h"
#include "xui/xui_shell_actions.h"
#include <commctrl.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

namespace {
using namespace xui;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& action) {
    bool failed{};
    try { action(); } catch (const std::exception&) { failed = true; }
    check(failed, "Invalid or reentrant Shell menu must fail explicitly");
}
struct Context final : IContextMenu3 {
    std::atomic<ULONG> refs{1};
    HMENU child{}, parent{};
    UINT first{}, invoked_offset{};
    std::atomic<unsigned> queries{}, invokes{}, verb_queries{};
    unsigned initialized{}, measured{}, drawn{}, chars{};
    DWORD query_delay{}, verb_delay{};
    bool fail{}, fail_invoke{};
    HBITMAP leaf_bitmap{};
    bool owner_drawn_leaf{}, blank_leaf{}, disabled_leaf{};
    std::wstring canonical_verb;
    std::wstring leaf_label{L"Inspect fixture"};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (id != IID_IUnknown && id != IID_IContextMenu && id != IID_IContextMenu2 && id != IID_IContextMenu3) return E_NOINTERFACE;
        *output = static_cast<IContextMenu3*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT, UINT first_id, UINT last, UINT flags) override {
        parent = menu;
        ++queries; first = first_id;
        if (query_delay) Sleep(query_delay);
        if (fail) return E_FAIL;
        if (first_id != 1 || last != 0x7fff || flags != CMF_NORMAL) return E_INVALIDARG;
        if (!AppendMenuW(menu, MF_STRING | (disabled_leaf ? MF_GRAYED : 0), first,
            blank_leaf ? L"" : leaf_bitmap ? L"&Open" : leaf_label.c_str())) return E_FAIL;
        if (leaf_bitmap) {
            MENUITEMINFOW item{sizeof(item)};
            item.fMask = MIIM_BITMAP | MIIM_FTYPE;
            item.hbmpItem = leaf_bitmap; item.fType = owner_drawn_leaf ? MFT_OWNERDRAW : MFT_STRING;
            if (!SetMenuItemInfoW(menu, first, FALSE, &item)) return E_FAIL;
        }
        child = CreatePopupMenu();
        if (!child || !AppendMenuW(child, MF_STRING | MF_GRAYED, first + 1, L"Deferred")) return E_FAIL;
        if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(child), L"Dynamic Shell submenu")) return E_FAIL;
        if (!AppendMenuW(menu, MF_STRING | MF_GRAYED, first + 2, L"Disabled Shell verb")) return E_FAIL;
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 3);
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(CMINVOKECOMMANDINFO* info) override {
        if (!IS_INTRESOURCE(info->lpVerb)) return E_INVALIDARG;
        ++invokes; invoked_offset = LOWORD(info->lpVerb); return fail_invoke ? E_FAIL : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR id, UINT flags, UINT*, LPSTR text, UINT capacity) override {
        ++verb_queries; if (verb_delay) Sleep(verb_delay);
        if (id == 0 && flags == GCS_VERBW && !canonical_verb.empty() && capacity > canonical_verb.size()) {
            wcscpy_s(reinterpret_cast<wchar_t*>(text), capacity, canonical_verb.c_str()); return S_OK;
        }
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE HandleMenuMsg(UINT message, WPARAM w, LPARAM l) override {
        LRESULT result{}; return HandleMenuMsg2(message, w, l, &result);
    }
    HRESULT STDMETHODCALLTYPE HandleMenuMsg2(UINT message, WPARAM w, LPARAM, LRESULT* result) override {
        *result = 0;
        if (message == WM_INITMENUPOPUP && reinterpret_cast<HMENU>(w) == child) {
            ++initialized;
            if (!DeleteMenu(child, 0, MF_BYPOSITION)) return E_FAIL;
            MENUITEMINFOW item{sizeof(item)};
            item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA;
            item.fType = MFT_OWNERDRAW; item.wID = first + 1; item.dwItemData = 0x1234;
            return InsertMenuItemW(child, 0, TRUE, &item) ? S_OK : E_FAIL;
        }
        if (message == WM_DRAWITEM) ++drawn;
        if (message == WM_MEASUREITEM) ++measured;
        if (message == WM_MENUCHAR) { ++chars; *result = 123; }
        return S_OK;
    }
};
std::function<UINT(HMENU, HWND)> inspect;
std::function<UINT(HMENU, HWND)> inspect_styled;
UINT track_styled(HMENU menu, HWND owner, Point) {
    const auto result = inspect_styled(menu, owner); SetLastError(ERROR_SUCCESS); return result;
}
UINT track(HMENU menu, HWND owner, Point) {
    const auto result = inspect(menu, owner); SetLastError(ERROR_SUCCESS); return result;
}
MENUITEMINFOW entry(HMENU menu, UINT index) {
    MENUITEMINFOW result{sizeof(result)}; result.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU | MIIM_DATA;
    check(GetMenuItemInfoW(menu, index, TRUE, &result) != FALSE, "Read actual merged HMENU");
    return result;
}
struct Provider final : ShellCommandProvider {
    std::vector<ShellCommandInfo> items;
    std::vector<ItemKey>* invoked{};
    bool fail{};
    std::vector<ShellCommandInfo> discover(std::stop_token) override {
        if (fail) throw std::runtime_error("Fixture discovery failed");
        return items;
    }
    void invoke(ItemKey key) override { invoked->push_back(key); }
};
void bitmap_leaf_tests(HWND owner) {
    {
        Context context;
        auto provider = ShellMenuTestAccess::provider(owner, &context);
        const auto discovered = provider->discover({});
        EnableMenuItem(context.parent, context.first, MF_BYCOMMAND | MF_GRAYED);
        rejects([&] { provider->invoke(discovered.front().key); });
        check(context.invokes == 0, "Availability is checked again on the original native menu before invocation");
    }
    struct Bitmap {
        HBITMAP value{CreateBitmap(1, 1, 1, 32, nullptr)};
        ~Bitmap() { if (value) DeleteObject(value); }
    } bitmap;
    check(bitmap.value != nullptr, "Create a real bitmap for Shell menu metadata");
    for (unsigned mode = 0; mode < 4; ++mode) {
        Context context;
        context.leaf_bitmap = bitmap.value;
        context.owner_drawn_leaf = mode == 1; context.blank_leaf = mode == 2; context.disabled_leaf = mode == 3;
        auto provider = ShellMenuTestAccess::provider(owner, &context);
        const auto discovered = provider->discover({});
        check(discovered.front().has_native_icon && discovered.front().native_only == (mode == 1 || mode == 2),
            "Native bitmap metadata is independent of custom invocation safety");
        unsigned fallbacks{};
        CustomShellMenu model(provider, {}, [] { return true; }, [&] { ++fallbacks; });
        const auto& first = model.commands()->records().front();
        if (mode == 0 || mode == 3) {
            check(first.label == L"Open", "A labeled bitmap-backed Open verb remains in the XUI menu");
            check(model.commands()->invoke(first.id) == (mode == 0), "Disabled bitmap-backed leaves remain disabled");
            check(context.invokes == (mode == 0 ? 1u : 0u) && !context.invoked_offset && !fallbacks,
                "Safe bitmap-backed Open invokes its original IContextMenu offset, not a fallback or reconstructed verb");
        } else {
            check(first.label.find(L"(Windows menu...)") != std::wstring::npos, "Unsafe bitmap entries show a visible native route");
            ShellCommandSession session(provider); session.discover();
            check(!session.invoke(discovered.front().key), "Owner-drawn and unlabeled leaves cannot bypass the native fallback");
            model.commands()->invoke(first.id);
            check(fallbacks == 1 && !context.invokes, "Owner-drawn and unlabeled leaves use fallback without direct invocation");
        }
        check(context.queries == 1, "Custom bitmap commands preserve the original Shell context");
    }
}
void custom_model_tests(HWND owner) {
    std::vector<ItemKey> invoked;
    auto provider = std::make_shared<Provider>(); provider->invoked = &invoked;
    ShellCommandInfo leaf{{900, 73}, L"&Inspect && keep"}; leaf.checked = true; leaf.shortcut_hints = {L"Ctrl+I"};
    ShellCommandInfo disabled{{901, 73}, L"Disabled"}; disabled.enabled = false;
    ShellCommandInfo separator{{902, 73}, L""}; separator.separator = true;
    ShellCommandInfo group{{903, 73}, L"Static group"};
    group.children.push_back({{904, 73}, L"Static child"});
    ShellCommandInfo native{{905, 73}, L"Dynamic group"}; native.native_only = true;
    native.children.push_back({{906, 73}, L"Do not expose"});
    ShellCommandInfo bitmap{{907, 73}, L"Bitmap command"}; bitmap.has_native_icon = true;
    provider->items = {leaf, disabled, separator, group, native, bitmap};
    unsigned apps{}, fallbacks{};
    bool current = true;
    const std::vector<MenuItem> app{{L"&New tab\tCtrl+T", [&] { ++apps; }, true, true}};
    auto model = std::make_shared<CustomShellMenu>(provider, app, [&] { return current; }, [&] { ++fallbacks; });
    auto commands = model->commands();
    const auto items = model->menu_items();
    const auto styled_find = [&](std::wstring_view label) -> const MenuItem& {
        for (const auto& item : items) if (item.text == label) return item;
        throw std::runtime_error("Expected styled context-menu label");
    };
    check(styled_find(L"Inspect && keep\tCtrl+I").checked && styled_find(L"New tab\tCtrl+T").checked &&
        !styled_find(L"Disabled").enabled, "Gallery menu adapter preserves literal ampersands, hints, checks, and disabled items");
    check(styled_find(L"Static group (Windows menu...)").action && styled_find(L"Show Windows menu...").action,
        "Groups retain an explicit Windows fallback in the flat gallery menu");
    check(std::count_if(items.begin(), items.end(), [](const auto& item) { return item.separator; }) == 3,
        "Gallery menu adapter retains Shell and application separators");
    const auto find = [](const CommandSet& set, std::wstring_view label) -> const CommandRecord& {
        for (const auto& record : set.records()) if (record.label == label) return record;
        throw std::runtime_error("Expected custom menu label");
    };
    check(find(*commands, L"Inspect & keep").checked == true &&
        find(*commands, L"Inspect & keep").shortcut_hints == std::vector<std::wstring>{L"Ctrl+I"} &&
        find(*commands, L"New tab").shortcut_hints == std::vector<std::wstring>{L"Ctrl+T"},
        "XUI preserves labels, checks, and shortcut hints");
    check(!commands->invoke(find(*commands, L"Disabled").id) &&
        find(*commands, L"Static child").parent == find(*commands, L"Static group").id,
        "Disabled leaves and ordinary submenu hierarchy survive mapping");
    check(find(*commands, L"Dynamic group (Windows menu...)").kind == CommandKind::action &&
        find(*commands, L"Bitmap command").kind == CommandKind::action &&
        std::none_of(commands->records().begin(), commands->records().end(), [](const auto& item) { return item.label == L"Do not expose"; }),
        "Native-only groups use Windows while labeled bitmap leaves remain direct custom actions");
    bool rejected_thread{};
    std::thread foreign([&] { try { model->current(); } catch (const std::logic_error&) { rejected_thread = true; } });
    foreign.join();
    check(rejected_thread, "Custom Shell discovery and actions belong to the UI thread");
    auto menu = std::make_shared<CommandMenu>();
    menu->set_commands(commands, find(*commands, L"Static group").id);
    menu->on_accept([&] { model->dismiss(PopupDismissReason::commit); model.reset(); });
    check(menu->execute(find(*commands, L"Static child").id) && invoked == std::vector<ItemKey>{{904, 73}} && !fallbacks,
        "Dismiss-before-action preserves the exact original Shell identity after model destruction");
    commands->invoke(find(*commands, L"New tab").id);
    check(!apps, "A custom menu snapshot invokes only once");

    for (unsigned mode = 0; mode < 4; ++mode) {
        auto owned = std::make_shared<Provider>(); owned->items = {leaf}; owned->invoked = &invoked;
        std::weak_ptr<Provider> lifetime = owned;
        current = true;
        model = std::make_shared<CustomShellMenu>(owned, app, [&] { return current; },
            [owned, &fallbacks] { ++fallbacks; });
        owned.reset(); commands = model->commands();
        if (mode == 0) model->dismiss(PopupDismissReason::outside);
        if (mode == 1) { current = false; check(!model->current(), "Stale selection cancels the custom menu"); }
        if (mode == 2) model.reset();
        if (mode == 3) {
            model->dismiss(PopupDismissReason::commit);
            commands->invoke(find(*commands, L"New tab").id);
            check(apps == 1, "A current application action runs once");
        }
        check(lifetime.expired(), "Cancellation or app dispatch releases the provider even if accessibility retains commands");
        commands->invoke(find(*commands, L"Show Windows menu...").id);
        check(!fallbacks, "Cancelled and consumed snapshots cannot open the native fallback");
    }
    auto failing = std::make_shared<Provider>(); failing->fail = true;
    model = std::make_shared<CustomShellMenu>(failing, app, [] { return true; }, [&] { ++fallbacks; });
    check(!model->discovery_error().empty() &&
        find(*model->commands(), L"Shell discovery failed - use the Windows menu").kind == CommandKind::section,
        "Discovery failure produces a diagnostic and visible fallback");
    model->commands()->invoke(find(*model->commands(), L"Show Windows menu...").id);
    check(fallbacks == 1, "Discovery failure still permits the explicit native fallback");
    std::vector<MenuItem> maximum_apps(4092);
    for (auto& item : maximum_apps) item.text = L"App command";
    {
        CustomShellMenu bounded(provider, maximum_apps, [] { return true; }, [] {});
        check(bounded.commands()->records().size() == CommandSet::maximum_commands &&
            find(*bounded.commands(), L"Additional commands are in the Windows menu").kind == CommandKind::section,
            "The exact command limit preserves an explicit overflow notice and native fallback");
    }
    maximum_apps.push_back({L"One too many"});
    rejects([&] { CustomShellMenu excess(provider, maximum_apps, [] { return true; }, [] {}); });

    Context context;
    auto original = ShellMenuTestAccess::provider(owner, &context);
    std::weak_ptr<ShellCommandProvider> lifetime = original;
    unsigned chosen{};
    const std::vector<MenuItem> merged{{L"Original app snapshot", [&] {
        check(context.refs == 1 && !GetPropW(owner, active_menu_property), "Fallback releases native ownership before app dispatch");
        ++chosen;
    }}};
    bool dismissed{};
    model = std::make_shared<CustomShellMenu>(original, merged, [] { return true; },
        [provider = original, owner, merged]() mutable {
            track_shell_commands(std::move(provider), {}, merged, [] { return true; });
        });
    original.reset();
    commands = model->commands(); menu->set_commands(commands);
    menu->on_accept([&] { dismissed = true; model->dismiss(PopupDismissReason::commit); model.reset(); });
    inspect = [&](HMENU native_menu, HWND hwnd) {
        check(dismissed && context.queries == 1 && GetMenuItemCount(native_menu) == 5,
            "Fallback closes XUI first and reuses the original IContextMenu with original app commands");
        SendMessageW(hwnd, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(context.child), 0);
        DRAWITEMSTRUCT draw{}; MEASUREITEMSTRUCT measure{};
        SendMessageW(hwnd, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
        SendMessageW(hwnd, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure));
        check(SendMessageW(hwnd, WM_MENUCHAR, 0, reinterpret_cast<LPARAM>(native_menu)) == 123,
            "The retained fallback still forwards extension menu messages");
        return 0x8000u;
    };
    menu->execute(find(*commands, L"Dynamic Shell submenu (Windows menu...)").id);
    check(chosen == 1 && !context.invokes && context.initialized && context.drawn && context.measured && lifetime.expired(),
        "An incompatible group uses the original native menu without invoking a real Shell verb");
    inspect = {};
}
void mock_tests(HWND owner) {
    Context context;
    HMENU tracked{};
    unsigned actions{};
    const std::vector<MenuItem> apps{
        {L"&New tab\tCtrl+Enter", [&] {
            check(!IsMenu(tracked) && !GetPropW(owner, active_menu_property) && context.refs == 1,
                "Native menu and extension references close before an app action");
            ++actions;
        }, true, true},
        {L"", {}, true, false, true},
        {L"Disabled app", [&] { throw std::runtime_error("Disabled app invoked"); }, false}
    };
    inspect = [&](HMENU menu, HWND hwnd) {
        tracked = menu;
        check(GetMenuItemCount(menu) == 7 && GetPropW(hwnd, active_menu_property), "Real Shell and app entries share one tracked menu");
        check(entry(menu, 0).wID == 1 && entry(menu, 1).hSubMenu == context.child, "Original Shell IDs and submenu handles remain intact");
        check((entry(menu, 3).fType & MFT_SEPARATOR) && entry(menu, 4).wID == 0x8000, "App IDs do not overlap Shell IDs");
        check((entry(menu, 4).fState & MFS_CHECKED) && (entry(menu, 5).fType & MFT_SEPARATOR) &&
            (entry(menu, 6).fState & MFS_DISABLED), "App checks, separators, and disabled state survive merging");
        wchar_t label[64]{};
        GetMenuStringW(menu, 4, label, 64, MF_BYPOSITION);
        check(std::wstring(label) == L"&New tab\tCtrl+Enter", "App mnemonic and shortcut hint survive merging");
        SendMessageW(hwnd, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(context.child), 0);
        check((entry(context.child, 0).fType & MFT_OWNERDRAW) && entry(context.child, 0).dwItemData == 0x1234,
            "Shell dynamically populates its own owner-drawn submenu");
        DRAWITEMSTRUCT draw{}; MEASUREITEMSTRUCT measure{};
        SendMessageW(hwnd, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
        SendMessageW(hwnd, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure));
        check(SendMessageW(hwnd, WM_MENUCHAR, L'x', reinterpret_cast<LPARAM>(menu)) == 123,
            "IContextMenu3 receives native menu character messages");
        rejects([&] { ShellMenuTestAccess::run(hwnd, &context, {}); });
        return 0x8000u;
    };
    check(!ShellMenuTestAccess::run(owner, &context, apps), "App action is not a Shell verb");
    check(actions == 1 && !context.invokes && context.refs == 1 && context.initialized && context.measured && context.drawn && context.chars,
        "App dispatch and Shell message forwarding preserve lifetimes");
    inspect = [&](HMENU, HWND hwnd) {
        SendMessageW(hwnd, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(context.child), 0); return 2u;
    };
    check(ShellMenuTestAccess::run(owner, &context, apps).has_value(), "Mock dynamic verb can be selected");
    check(context.invokes == 1 && context.invoked_offset == 1, "Only the selected fake Shell offset is invoked");
    inspect = [&](HMENU, HWND hwnd) {
        SendMessageW(hwnd, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(context.child), 0);
        check(DeleteMenu(context.child, 0, MF_BYPOSITION) != FALSE, "Simulate dynamic-menu cleanup after selection");
        return 2u;
    };
    check(ShellMenuTestAccess::run(owner, &context, apps).has_value() && context.invokes == 2,
        "Dynamic Shell selection remains valid after its submenu entry is removed");
    for (const UINT id : {3u, 0x8001u, 0x8002u}) {
        inspect = [id](HMENU, HWND) { return id; };
        check(!ShellMenuTestAccess::run(owner, &context, apps), "Disabled commands and separators never invoke");
    }
    bool current = true;
    for (const UINT id : {1u, 0x8000u}) {
        current = true;
        inspect = [&](HMENU, HWND) { current = false; return id; };
        check(!ShellMenuTestAccess::run(owner, &context, apps, [&] { return current; }), "Stale snapshots cancel both Shell and app actions");
    }
    check(actions == 1 && context.invokes == 2, "Stale menus invoke nothing");
    const auto queries = context.queries.load();
    ShellMenuTestAccess::run(owner, &context, apps, [] { return false; });
    check(context.queries == queries, "An already stale snapshot does not discover Shell verbs");
    context.fail = true;
    rejects([&] { ShellMenuTestAccess::run(owner, &context, apps); });
    check(context.refs == 1 && !GetPropW(owner, active_menu_property), "Discovery failure releases COM and menu ownership");
    context.fail = false;
    rejects([&] { ShellMenuTestAccess::run(owner, &context, std::vector<MenuItem>(4097)); });
    check(context.refs == 1 && !GetPropW(owner, active_menu_property), "Oversized app menu is bounded and releases ownership");
}
void real_discovery(HWND owner, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory / L"folder");
    std::ofstream(directory / L"file.txt") << "Original Shell menu fixture.";
    unsigned discoveries{};
    inspect = [&](HMENU menu, HWND) {
        const auto count = GetMenuItemCount(menu);
        check(count > 2 && entry(menu, count - 1).wID == 0x8000, "Real Windows Shell verbs and an app command share one HMENU");
        ++discoveries;
        return 0u;
    };
    const std::vector<MenuItem> app{{L"Fixture command", [] { throw std::runtime_error("Real discovery must not invoke"); }}};
    for (const auto& path : {directory / L"file.txt", directory / L"folder"}) {
        for (int attempt = 0; attempt != 2; ++attempt) {
            const auto start = std::chrono::steady_clock::now();
            auto provider = shell_command_provider(owner, {path.wstring()});
            const auto created = std::chrono::steady_clock::now();
            const auto commands = provider->discover({});
            const auto done = std::chrono::steady_clock::now();
            std::cout << "Shell " << path.filename().string() << (attempt ? " repeat" : " first-selection")
                << " construct_ms=" << std::chrono::duration<double, std::milli>(created - start).count()
                << " discover_ms=" << std::chrono::duration<double, std::milli>(done - created).count()
                << " rows=" << commands.size() << '\n';
        }
    }
    for (const auto& path : {directory / L"folder", directory / L"file.txt"})
        check(!track_shell_commands(owner, {path.wstring()}, {}, app, [] { return true; }), "Real Shell construction cancels without invocation");
    check(discoveries == 2 && !GetPropW(owner, active_menu_property), "Both file and folder discovery release menus");
    rejects([&] { track_shell_commands(owner, {std::wstring(L"bad\0path", 8)}, {}, app, {}); });
}
xui_string text(const char* value) { return {value, static_cast<uint32_t>(std::strlen(value)), 0}; }
void ok(xui_status result) { check(result == XUI_OK, "C ABI merged-menu operation succeeds"); }
struct Binding {
    xui_handle window{}, grid{}, replacement{};
    HWND peer{};
    std::string path;
    unsigned refs{1}, requests{}, actions{}, tracked{}, styled{}, mode{};
};
LRESULT CALLBACK binding_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR) {
    if (message == WM_APP + 53) {
        auto& binding = *reinterpret_cast<Binding*>(lparam);
        return xui_source_attach(binding.grid, binding.replacement);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, binding_proc, id);
    return DefSubclassProc(hwnd, message, wparam, lparam);
}
void XUI_CALL retain(void* value) { ++static_cast<Binding*>(value)->refs; }
void XUI_CALL release(void* value) { --static_cast<Binding*>(value)->refs; }
xui_status XUI_CALL query(void*, uint32_t operation, uint64_t first, uint64_t second, xui_source_row* row) {
    if (operation == 0) { row->id = first + 1; row->version = 9; }
    if (operation == 1) { std::memcpy(row->primary, "row", 3); row->primary_length = 3; }
    if (operation == 2) row->index = first && first <= 2 && second == 9 ? first - 1 : UINT64_MAX;
    return XUI_OK;
}
xui_status XUI_CALL requested(void* value, const xui_event* event) {
    auto& binding = *static_cast<Binding*>(value);
    if (event->kind == XUI_REQUEST) {
        ++binding.requests;
        xui_command_record command{sizeof(command), 0, 77, 0, text("App fixture\tCtrl+T"), {}, {}, 0, 0};
        const auto result = xui_context_menu_items(binding.grid, &command, 1);
        if (result) return result;
        const auto path = text(binding.path.c_str());
        const auto paths = xui_context_menu_shell_paths(binding.grid, &path, 1);
        if (paths) return paths;
        if (xui_context_menu_presentation(binding.grid, 2) != XUI_INVALID_ARGUMENT) return XUI_INVALID_ARGUMENT;
        return xui_context_menu_presentation(binding.grid, binding.mode >= 4 ? 1 : 0);
    }
    if (event->kind == XUI_ACTION) {
        if (event->value != 77) return XUI_INVALID_ARGUMENT;
        ++binding.actions;
    }
    return XUI_OK;
}
xui_status XUI_CALL posted(void* value, uint32_t execute) {
    if (!execute) return XUI_OK;
    auto& binding = *static_cast<Binding*>(value);
    const auto focused = xui_focus(binding.grid, 0);
    if (focused) return focused;
    const auto hwnd = GetFocus();
    binding.peer = hwnd;
    SetWindowSubclass(hwnd, binding_proc, 53, 0);
    const auto dpi = GetDpiForWindow(hwnd);
    const auto position = MAKELPARAM(MulDiv(60, dpi, 96), MulDiv(54, dpi, 96));
    SendMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, position);
    SendMessageW(hwnd, WM_RBUTTONUP, 0, position);
    return xui_window_close(binding.window);
}
void binding_menus(const std::filesystem::path& directory) {
    ContextMenuTestAccess::track = track_styled;
    const auto path = (directory / L"folder").u8string();
    for (unsigned mode = 0; mode < 10; ++mode) {
        std::cout << "Binding mode=" << mode << '\n';
        Binding binding; binding.path.assign(path.begin(), path.end()); binding.mode = mode;
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Merged menu C ABI fixture"), 500, 320};
        ok(xui_window_create(&options, &binding.window));
        xui_feature_options feature{sizeof(feature), XUI_FEATURE_VERSION, text("Files")};
        ok(xui_feature_create(binding.window, XUI_DATA_GRID, &feature, &binding.grid));
        const xui_column column{sizeof(column), 0, text("Name"), 300};
        ok(xui_grid_columns(binding.grid, &column, 1));
        xui_source_options source_options{sizeof(source_options), XUI_FEATURE_VERSION, 2, &binding, query, retain, release};
        xui_handle source{};
        ok(xui_source_create(binding.window, &source_options, &source));
        ok(xui_source_create(binding.window, &source_options, &binding.replacement));
        ok(xui_source_attach(binding.grid, source));
        xui_handle stack{};
        ok(xui_stack_create(binding.window, 1, &stack));
        ok(xui_stack_add(stack, binding.grid, 1)); ok(xui_window_content(binding.window, stack));
        ok(xui_context_menu_bind(binding.grid, requested, &binding));
        check(xui_context_menu_presentation(binding.grid, 1) == XUI_BUSY, "Presentation staging requires an active menu request");
        inspect = [&](HMENU menu, HWND) {
            ++binding.tracked;
            const auto count = GetMenuItemCount(menu);
            check(count > 2 && entry(menu, count - 1).wID == 0x8000, "C ABI stages real Shell verbs alongside app commands");
            if (mode == 1 || mode == 9)
                ok(static_cast<xui_status>(SendMessageW(binding.peer, WM_APP + 53, 0, reinterpret_cast<LPARAM>(&binding))));
            if (mode == 2) ok(xui_context_menu_bind(binding.grid, requested, &binding));
            return mode == 3 ? 0u : 0x8000u;
        };
        inspect_styled = [&](HMENU menu, HWND hwnd) {
            ++binding.styled;
            const auto count = GetMenuItemCount(menu);
            check(count > 2 && GetPropW(GetAncestor(hwnd, GA_ROOT), active_menu_property),
                "File commands use the gallery's tracked native context menu");
            UINT app_id{}, fallback_id{};
            for (int i = 0; i < count; ++i) {
                const auto item = entry(menu, i);
                check(item.fType & MFT_OWNERDRAW, "Shell and app rows use the gallery's owner-drawn renderer");
                wchar_t label[2048]{};
                GetMenuStringW(menu, i, label, 2048, MF_BYPOSITION);
                if (std::wstring_view(label) == L"App fixture\tCtrl+T") app_id = item.wID;
                if (std::wstring_view(label) == L"Show Windows menu...") fallback_id = item.wID;
            }
            check(app_id && fallback_id, "Styled menu preserves the shortcut column and native fallback");
            MEASUREITEMSTRUCT measure{}; measure.CtlType = ODT_MENU; measure.itemID = app_id;
            SendMessageW(hwnd, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure));
            check(measure.itemWidth > 0 && measure.itemHeight >= UINT(MulDiv(32, GetDpiForWindow(hwnd), 96)),
                "File menu uses gallery row measurement, not CommandSurface rows");
            if (mode == 6) ok(xui_source_attach(binding.grid, binding.replacement));
            if (mode == 7) ok(xui_context_menu_bind(binding.grid, requested, &binding));
            return mode == 8 ? 0u : mode == 4 || mode == 9 ? fallback_id : app_id;
        };
        ok(xui_window_post(binding.window, posted, &binding));
        ok(xui_window_run(binding.window));
        check(binding.requests == 1 && binding.tracked == (mode <= 4 || mode == 9 ? 1u : 0u) &&
            binding.styled == (mode >= 4 ? 1u : 0u) &&
            binding.actions == (mode == 0 || mode == 4 || mode == 5 ? 1u : 0u),
            "Raw right-click supports native merging, custom actions, native fallback, and stale snapshot cancellation");
        ok(xui_window_destroy(binding.window));
        check(binding.refs == 1, "Merged C ABI menus release immutable-source ownership");
        inspect = {};
        inspect_styled = {};
    }
    ContextMenuTestAccess::track = nullptr;
}
std::shared_ptr<Context> slow_context;
std::atomic<unsigned> worker_creates{};
std::atomic<DWORD> worker_thread{};
std::atomic<bool> require_first_frame{}, first_frame_drawn{};
std::shared_ptr<ShellCommandProvider> slow_provider(HWND owner, const std::vector<std::wstring>& paths) {
    check(!require_first_frame || first_frame_drawn, "Shell creation starts only after the first gallery frame is painted");
    check(paths.size() == 1 && (paths.front() == L"fake-selection" || paths.front() == L"other-selection"),
        "Worker receives the exact immutable selection");
    worker_thread = GetCurrentThreadId();
    check(GetWindowThreadProcessId(owner, nullptr) == GetCurrentThreadId(), "Shell HWND and COM objects share the worker STA");
    APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
    check(SUCCEEDED(CoGetApartmentType(&type, &qualifier)) && type == APTTYPE_STA, "Shell worker is an initialized STA");
    ++worker_creates;
    slow_context->leaf_label = paths.front() == L"fake-selection" ? L"Inspect fixture" : L"Other fixture";
    return ShellMenuTestAccess::provider(owner, slow_context.get());
}
std::function<void()> latency_drive;
std::exception_ptr latency_failure;
double first_paint_ms{};
std::chrono::steady_clock::time_point latency_start;
std::atomic<bool> native_visible{};
std::atomic<bool> cancel_native{};
std::atomic<HWND> native_owner{};
bool cancel_via_api{};
ULONGLONG native_started{};
void CALLBACK native_latency_timer(HWND hwnd, UINT, UINT_PTR, DWORD) noexcept {
    if (native_owner != hwnd) return;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM) -> BOOL {
        wchar_t name[32]{};
        if (IsWindowVisible(hwnd) && GetClassNameW(hwnd, name, 32) && std::wstring_view(name) == L"#32768")
            native_visible = true;
        return TRUE;
    }, 0);
    if ((!cancel_native && native_visible) || GetTickCount64() - native_started > 2000) EndMenu();
}
LRESULT CALLBACK cancel_native_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    if (message == WM_TIMER && w == 93 && native_visible) {
        if (cancel_via_api) cancel_control_menu(hwnd);
        else *reinterpret_cast<bool*>(data) = false;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, cancel_native_proc, id);
    return DefSubclassProc(hwnd, message, w, l);
}
void painted(HMENU, HWND) {
    first_frame_drawn = true;
    if (!first_paint_ms)
        first_paint_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - latency_start).count();
}
void CALLBACK latency_timer(HWND, UINT, UINT_PTR, DWORD) noexcept {
    if (!latency_drive) return;
    try { latency_drive(); }
    catch (...) { latency_failure = std::current_exception(); EndMenu(); }
}
template<class F> void pump_until(F done, DWORD limit = 8000) {
    const auto start = GetTickCount64();
    while (!done()) {
        check(GetTickCount64() - start < limit, "Shell test completion is bounded");
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(2);
    }
}
void latency_tests(HWND owner) {
    {
        Context delayed; delayed.query_delay = 2000; delayed.verb_delay = 5;
        const auto start = std::chrono::steady_clock::now();
        auto provider = ShellMenuTestAccess::provider(owner, &delayed);
        const auto constructed = std::chrono::steady_clock::now();
        provider->discover({});
        std::cout << "Slow-extension baseline construct_ms="
            << std::chrono::duration<double, std::milli>(constructed - start).count()
            << " discover_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - constructed).count()
            << " verb_queries=" << delayed.verb_queries.load() << '\n';
    }
    ShellMenuTestAccess::create = slow_provider;
    ContextMenuTestAccess::track = track_styled;
    ContextMenuTestAccess::paint = painted;
    ShowWindow(owner, SW_SHOWNOACTIVATE);
    const DWORD ui_thread = GetCurrentThreadId();
    // App during cold/warm discovery, cancellation, stale selection, populated
    // command, native fallback, and visible discovery failure.
    for (unsigned mode = 0; mode != 10; ++mode) {
        require_first_frame = true; first_frame_drawn = false;
        slow_context = std::make_shared<Context>();
        slow_context->query_delay = mode >= 6 ? 150 : 2000;
        slow_context->verb_delay = 30;
        slow_context->fail = mode == 6;
        slow_context->fail_invoke = mode == 7;
        native_visible = false; cancel_native = mode >= 8; cancel_via_api = mode == 9;
        bool current = true;
        unsigned app_actions{}, tracks{};
        UINT initial_app{}, initial_fallback{};
        UINT selected{};
        bool loaded{}, saw_failure{};
        first_paint_ms = 0; latency_failure = {};
        Button button(L"Latency fixture");
        button.on_context_menu_content([&] {
            return ContextMenuContent{{{L"Harmless app\tCtrl+H", [&] {
                check(GetCurrentThreadId() == ui_thread, "Application actions remain on the UI thread");
                check(!GetPropW(owner, active_menu_property), "Gallery and fallback close before application dispatch");
                ++app_actions;
            }}}, {L"fake-selection"}, [&] {
                check(GetCurrentThreadId() == ui_thread, "Snapshot validity is evaluated only on the UI thread");
                return current;
            }, ShellMenuPresentation::xui};
        });
        inspect = [&](HMENU native, HWND hwnd) {
            check(GetCurrentThreadId() == worker_thread && hwnd != owner && slow_context->queries == 1,
                "Fallback retains the original handler and uses its owning STA");
            check(entry(native, 0).wID == 1 && entry(native, GetMenuItemCount(native) - 1).wID == 0x8000,
                "Fallback preserves original Shell IDs and app identity");
            native_started = GetTickCount64();
            native_owner = hwnd;
            check(SetTimer(hwnd, 92, 16, native_latency_timer) != 0, "Start actual native fallback checks");
            TrackPopupMenuEx(native, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NOANIMATION, 30, 40, hwnd, nullptr);
            KillTimer(hwnd, 92);
            native_owner = nullptr;
            check(native_visible, "The Shell STA shows an actual native fallback popup without foreground workarounds");
            return mode >= 8 ? 0u : 0x8000u;
        };
        inspect_styled = [&](HMENU menu, HWND hwnd) {
            ++tracks; selected = 0;
            UINT app_id{}, shell_id{}, fallback_id{};
            bool loading{};
            for (int i = 0; i < GetMenuItemCount(menu); ++i) {
                check(entry(menu, i).fType & MFT_OWNERDRAW, "Loading and ready frames are actual gallery owner-drawn HMENUs");
                wchar_t label[1024]{};
                GetMenuStringW(menu, i, label, 1024, MF_BYPOSITION);
                const std::wstring_view text(label);
                if (text == L"Harmless app\tCtrl+H") app_id = entry(menu, i).wID;
                if (text == L"Inspect fixture") shell_id = entry(menu, i).wID;
                if (text == L"Show Windows menu...") fallback_id = entry(menu, i).wID;
                loading |= text == L"Loading Windows commands...";
                saw_failure |= text == L"Shell discovery failed - use the Windows menu";
            }
            check(app_id && fallback_id, "App commands and explicit Windows fallback are available on every frame");
            if (tracks == 1) { initial_app = app_id; initial_fallback = fallback_id; }
            else check(app_id == initial_app && fallback_id == initial_fallback,
                "Population keeps existing app commands and the Windows fallback in their original positions");
            loaded |= shell_id != 0;
            if (mode == 4 && shell_id)
                check(std::chrono::steady_clock::now() - latency_start >= std::chrono::milliseconds(2300),
                    "Ready discovery does not replace a highlighted command during pointer or keyboard interaction");
            latency_drive = [&, app_id, shell_id, fallback_id, loading] {
                check(std::chrono::steady_clock::now() - latency_start < std::chrono::seconds(8), "Slow menu test deadline");
                if (!first_paint_ms || !slow_context->queries) return;
                if (mode == 4 && loading && std::chrono::steady_clock::now() - latency_start < std::chrono::milliseconds(2300)) {
                    HiliteMenuItem(hwnd, menu, app_id, MF_BYCOMMAND | MF_HILITE);
                    return;
                }
                if ((mode == 4 || mode == 6 || mode == 7) && loading) {
                    for (int i = 0; i < GetMenuItemCount(menu); ++i)
                        HiliteMenuItem(hwnd, menu, i, MF_BYPOSITION | MF_UNHILITE);
                    return;
                }
                if (mode == 3) current = false;
                selected = mode == 2 ? 0 : mode == 4 || mode == 7 ? shell_id : mode == 5 || mode >= 8 ? fallback_id : app_id;
                EndMenu();
            };
            check(SetTimer(hwnd, 91, 16, latency_timer) != 0, "Start first-frame driver");
            TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NOANIMATION, 30, 40, hwnd, nullptr);
            KillTimer(hwnd, 91); latency_drive = {};
            if (latency_failure) std::rethrow_exception(latency_failure);
            return selected;
        };
        if (mode >= 8) {
            SetWindowSubclass(owner, cancel_native_proc, 93, reinterpret_cast<DWORD_PTR>(&current));
            SetTimer(owner, 93, 16, nullptr);
        }
        latency_start = std::chrono::steady_clock::now();
        const auto show = [&] {
            show_control_menu(button, owner, MAKELPARAM(30, 40), Palette::system(), GetDpiForWindow(owner));
        };
        if (mode == 7) rejects(show);
        else show();
        if (mode >= 8) { KillTimer(owner, 93); RemoveWindowSubclass(owner, cancel_native_proc, 93); }
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - latency_start).count();
        std::cout << "Gallery mode=" << mode << " first_paint_ms=" << first_paint_ms
            << " interaction_ms=" << elapsed << " frames=" << tracks << '\n';
        check(first_paint_ms > 0 && first_paint_ms < 250, "First owner-drawn native menu frame must paint in under 250 ms with a 2-second extension");
        if (mode < 4) check(elapsed < 500, "App selection, Escape, and stale cancellation do not wait for blocked discovery");
        check(app_actions == (mode == 0 || mode == 1 || mode == 5 || mode == 6 ? 1u : 0u),
            "Only the current harmless application action runs");
        if (mode == 4 || mode == 7) check(loaded && tracks == 2 && slow_context->invokes == 1 && !slow_context->invoked_offset,
            "Completed metadata replaces loading in the same modal interaction and invokes only the exact fake identity");
        if (mode == 6) check(saw_failure, "Failed asynchronous discovery produces a visible disabled notice");
        pump_until([] { return slow_context->refs == 1; });
        check(slow_context->verb_queries == 0, "Gallery discovery skips verb strings and native-only child traversal");
        check(mode == 4 || mode == 7 || slow_context->invokes == 0, "No other fake or real Shell verb runs");
    }
    // A blocked extension cannot create a thread or an unbounded pending job per click.
    require_first_frame = false;
    slow_context = std::make_shared<Context>(); slow_context->query_delay = 2000;
    const auto creates = worker_creates.load();
    auto active = AsyncShellMenu::start(owner, {L"fake-selection"});
    pump_until([] { return slow_context->queries != 0; });
    std::vector<std::shared_ptr<AsyncShellMenu>> pending;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) pending.push_back(AsyncShellMenu::start(owner, {L"fake-selection"}));
    active->cancel();
    for (const auto& request : pending) request->cancel();
    check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200),
        "One hundred pending replacements and cancellation requests remain bounded");
    pump_until([&] { return active->finished() && pending.back()->finished(); });
    check(worker_creates == creates + 1 && slow_context->refs == 1,
        "A blocked worker retains one active provider and one replaceable pending selection, not 100 providers");
    slow_context->query_delay = 0;
    auto first = AsyncShellMenu::start(owner, {L"fake-selection"});
    pump_until([&] { return first->ready(); });
    const auto first_commands = first->commands();
    check(first_commands.size() == 3 && first_commands[1].native_only && first_commands[1].children.empty(),
        "Gallery metadata skips the full native-only subtree");
    first->cancel(); pump_until([&] { return first->finished(); });
    rejects([&] { first->invoke(first_commands[0].key); });
    rejects([&] { first->windows_menu({}, {}); });
    slow_context->disabled_leaf = true;
    auto other = AsyncShellMenu::start(owner, {L"other-selection"});
    pump_until([&] { return other->ready(); });
    const auto other_commands = other->commands();
    check(first_commands[0].label == L"Inspect fixture" && other_commands[0].label == L"Other fixture" &&
        !other_commands[0].enabled && first_commands[0].key != other_commands[0].key,
        "A different selection gets fresh metadata, disabled state, and original Shell identities, never cached verbs");
    other->cancel(); pump_until([&] { return other->finished(); });
    const auto thread = OpenThread(SYNCHRONIZE, FALSE, worker_thread);
    check(thread != nullptr, "Observe the worker lifetime without retaining its code");
    try { pump_until([&] { return WaitForSingleObject(thread, 0) == WAIT_OBJECT_0; }, 13000); }
    catch (...) { CloseHandle(thread); throw; }
    CloseHandle(thread);
    std::cout << "Shell worker released handlers and exited after idle timeout; 100 cancelled pending requests created no extra providers.\n";
    ContextMenuTestAccess::paint = nullptr; ContextMenuTestAccess::track = nullptr;
    ShellMenuTestAccess::create = nullptr;
    inspect = {}; inspect_styled = {}; slow_context.reset();
}
void snapshot_tests() {
    require_first_frame = false;
    ShellMenuTestAccess::create = slow_provider;
    slow_context = std::make_unique<Context>();
    slow_context->canonical_verb = L"fixture.inspect";
    struct Results {
        bool valid{true};
        std::vector<ItemKey> keys;
        std::vector<std::string> verbs;
    } results;
    const auto current = +[](void* data) -> uint32_t { return static_cast<Results*>(data)->valid ? 1u : 0u; };
    const auto receive = +[](void* data, uint64_t id, uint64_t version, xui_string, xui_string verb, uint32_t) -> xui_status {
        auto& result = *static_cast<Results*>(data);
        result.keys.push_back({id, version}); result.verbs.emplace_back(verb.data, verb.length); return XUI_OK;
    };
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, {"Snapshot fixture", 16, 0}, 300, 200};
    xui_handle window{}, session{};
    check(xui_window_create(&options, &window) == XUI_OK, "Create snapshot binding owner");
    const xui_string path{"fake-selection", 14, 0};
    check(xui_shell_actions_create(window, &path, 1, current, &results, &session) == XUI_OK, "Create asynchronous Shell snapshot");
    uint32_t ready{};
    pump_until([&] {
        results.keys.clear(); results.verbs.clear();
        check(xui_shell_actions_read(session, receive, &results, &ready) == XUI_OK, "Read copied Shell metadata");
        return ready == 1;
    });
    check(results.keys.size() == 2 && results.verbs.front() == "fixture.inspect",
        "Snapshot exposes actual canonical verbs and leaves, never a dynamic submenu");
    check(xui_shell_actions_read(session, nullptr, nullptr, &ready) == XUI_OK && ready == 1,
        "Status polling does not require repeated metadata delivery");
    const auto key = results.keys.front();
    results.valid = false;
    check(xui_shell_actions_invoke(session, key.id, key.version) == XUI_INVALID_ARGUMENT && slow_context->invokes == 0,
        "Stale selections cannot invoke a searched command");
    results.valid = true;
    check(xui_shell_actions_invoke(session, key.id, key.version) == XUI_OK, "Invoke original snapshot identity");
    pump_until([&] {
        check(xui_shell_actions_read(session, receive, &results, &ready) == XUI_OK, "Read Shell invocation completion");
        return ready == 2;
    });
    check(slow_context->invokes == 1 && slow_context->invoked_offset == 0, "Snapshot invokes actual original COM command");
    check(xui_shell_actions_destroy(session) == XUI_OK, "Destroy snapshot without joining");
    pump_until([&] { return slow_context->refs == 1; });
    slow_context->query_delay = 200;
    check(xui_shell_actions_create(window, &path, 1, current, &results, &session) == XUI_OK, "Start cancellable snapshot");
    pump_until([&] { return slow_context->queries > 1; });
    const auto start = GetTickCount64();
    check(xui_shell_actions_destroy(session) == XUI_OK && GetTickCount64() - start < 100,
        "Snapshot cancellation does not wait for Shell discovery");
    check(xui_shell_actions_read(session, receive, &results, &ready) == XUI_INVALID_HANDLE, "Destroyed snapshot handles are invalid");
    pump_until([&] { return slow_context->refs == 1; });
    slow_context->query_delay = 0;
    std::atomic<unsigned> fallback_tracks{};
    inspect = [&](HMENU, HWND owner) {
        ++fallback_tracks;
        SendMessageW(owner, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(slow_context->child), 0);
        return 2u;
    };
    check(xui_shell_actions_create(window, &path, 1, current, &results, &session) == XUI_OK, "Create full Windows fallback snapshot");
    check(xui_shell_actions_windows(session) == XUI_OK, "Request Windows fallback before discovery finishes");
    pump_until([&] {
        check(xui_shell_actions_read(session, nullptr, nullptr, &ready) == XUI_OK, "Read native fallback completion");
        return ready == 2;
    });
    check(fallback_tracks == 1 && slow_context->initialized == 1 && slow_context->invokes == 2 &&
        slow_context->invoked_offset == 1, "Fallback preserves native dynamic submenu messages and original COM invocation");
    check(xui_shell_actions_destroy(session) == XUI_OK, "Destroy completed native fallback");
    check(xui_window_destroy(window) == XUI_OK, "Retire snapshot owner");
    pump_until([&] { return slow_context->refs == 1; });
    inspect = {};
    ShellMenuTestAccess::create = nullptr;
    slow_context.reset();
}
}
int wmain(int argc, wchar_t** argv) {
    HWND owner{};
    try {
        std::cout << std::unitbuf;
        check(argc == 2 || (argc == 3 && std::wstring_view(argv[2]) == L"--latency-only"), "Pass a project-local fixture directory and optional --latency-only");
        check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Initialize Shell STA");
        owner = CreateWindowExW(0, L"STATIC", L"Shell menu fixture", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(owner != nullptr, "Create hidden fixture owner");
        ShellMenuTestAccess::track = track;
        const auto directory = std::filesystem::absolute(argv[1]);
        if (argc == 2) {
            std::cout << "Mock menus\n"; mock_tests(owner);
            std::cout << "Bitmap menus\n"; bitmap_leaf_tests(owner);
            std::cout << "Custom models\n"; custom_model_tests(owner);
            std::cout << "Real discovery\n"; real_discovery(owner, directory);
            std::cout << "Binding menus\n"; binding_menus(directory);
        }
        std::cout << "Latency menus\n";
        latency_tests(owner);
        std::cout << "Searchable snapshot bindings\n";
        snapshot_tests();
        Context context;
        inspect = [](HMENU, HWND hwnd) { DestroyWindow(hwnd); return 1u; };
        check(!ShellMenuTestAccess::run(owner, &context, {}), "Closing the owner cancels pending Shell invocation");
        owner = nullptr;
        check(context.refs == 1 && !context.invokes, "Closed-owner menu releases extension references");
        ShellMenuTestAccess::track = nullptr; inspect = {};
        CoUninitialize();
        std::cout << "Merged real Shell menus, dynamic forwarding, app IDs, stale guards, and failure lifetimes passed. No real verbs invoked.\n";
        return 0;
    } catch (const std::exception& error) {
        ShellMenuTestAccess::track = nullptr;
        ContextMenuTestAccess::track = nullptr;
        if (owner) DestroyWindow(owner);
        std::cerr << error.what() << '\n'; return 1;
    }
}

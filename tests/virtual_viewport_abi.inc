#include "xui/xui_virtual_viewport.h"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <future>

namespace virtual_viewport_abi {
using Microsoft::WRL::ComPtr;
struct Fixture {
    struct Row { uint32_t index{}; xui_handle root{}, input{}, button{}; };
    xui_handle window{}, host{}, scope{}, root{}, rows_root{}, scroll{}, lease{};
    xui_virtual_viewport_request latest{sizeof(latest), XUI_VIRTUAL_VIEWPORT_VERSION};
    std::vector<Row> rows;
    HWND frame{}, native_content{}, first_edit{};
    unsigned notifications{}, interactions{}, commits{}, phase{}, dpi_index{};
    bool scheduled{}, automatic{true}, built{}, retirement_signalled{};
    unsigned invalid_coverage{};
    std::promise<void> retirement;
    std::thread automation_thread;
    std::vector<UINT> dpis{96, 192, 384, 768};
    static xui_status XUI_CALL notified(void* context, const xui_virtual_viewport_request* request) {
        auto& self = *static_cast<Fixture*>(context);
        expect(request && request->size == sizeof(*request) && request->version == XUI_VIRTUAL_VIEWPORT_VERSION &&
            !request->reserved && request->epoch && request->requested_source_version && request->blocked <= 1);
        ++self.notifications; self.latest = *request;
        uint32_t result{};
        expect(xui_virtual_viewport_try_begin_update(self.lease, request->epoch, &result) == XUI_BUSY);
        expect(xui_virtual_viewport_flush_committed(self.lease, request->epoch) == XUI_BUSY);
        if (self.automatic) self.schedule();
        return XUI_OK;
    }
    static xui_status XUI_CALL interaction(void* context, const xui_control_interaction* value) {
        auto& self = *static_cast<Fixture*>(context);
        expect(value && value->size == sizeof(*value) && value->version == XUI_CONTROL_INTERACTION_VERSION &&
            value->has_focus <= 1 && value->is_composing <= 1);
        ++self.interactions;
        return XUI_OK;
    }
    static xui_status XUI_CALL processing(void* context, uint32_t execute) {
        if (!execute) return XUI_OK;
        auto& self = *static_cast<Fixture*>(context); self.scheduled = false;
        if (self.automatic) self.process();
        return XUI_OK;
    }
    void schedule() {
        if (scheduled) return;
        scheduled = true; ok(xui_window_post(window, processing, this));
    }
    xui_virtual_viewport_request read_request() {
        xui_virtual_viewport_request value{sizeof(value), XUI_VIRTUAL_VIEWPORT_VERSION};
        ok(xui_virtual_viewport_get_request(lease, &value)); return value;
    }
    xui_handle stack() {
        xui_handle value{}; ok(xui_stack_create(window, 1, &value)); return value;
    }
    void height(xui_handle value, float extent) {
        xui_axis_constraints constraints{sizeof(constraints), XUI_AXIS_CONSTRAINTS_VERSION,
            {}, {XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, extent, 0, 0}};
        ok(xui_element_set_axis_constraints(value, &constraints));
    }
    void build() {
        ok(xui_content_begin_append(scope));
        for (uint32_t i = 0; i < 16; ++i) {
            Row row; row.index = i < 8 ? i : 9992 + i - 8; row.root = stack();
            height(row.root, 128);
            const auto name = "Virtual editor " + std::to_string(row.index);
            const auto contents = "row-value-" + std::to_string(row.index);
            ok(xui_create(window, XUI_TEXT_INPUT, text(name.c_str()), 0, &row.input));
            ok(xui_create(window, XUI_BUTTON, text("Row action"), 0, &row.button));
            xui_property property{sizeof(property), XUI_TEXT, row.input, text(contents.c_str())};
            ok(xui_update(window, &property, 1));
            ok(xui_stack_add(row.root, row.input, 0)); ok(xui_stack_add(row.root, row.button, 0));
            rows.push_back(row);
        }
        const auto gap = stack(); height(gap, (9992 - 8) * 128.0f);
        ok(xui_content_end_append(scope, 1));
        for (uint32_t i = 0; i < 8; ++i) ok(xui_content_stack_insert(rows_root, i, rows[i].root, 0));
        ok(xui_content_stack_insert(rows_root, 8, gap, 0));
        for (uint32_t i = 8; i < 16; ++i) ok(xui_content_stack_insert(rows_root, i + 1, rows[i].root, 0));
        built = true;
    }
    void metadata(uint64_t source) {
        for (const auto& row : rows) {
            if (invalid_coverage == 1 && row.index == 1) continue;
            const auto key = "key-" + std::to_string(row.index);
            std::vector<uint16_t> units(key.begin(), key.end());
            xui_virtual_item_info info{sizeof(info), XUI_VIRTUAL_ITEM_VERSION, row.index, 10000,
                source, units.data(), static_cast<uint32_t>(units.size()), 0};
            if (row.index == 0) {
                auto invalid = info; invalid.count = 9999;
                expect(xui_virtual_viewport_set_item(lease, row.root, &invalid) == XUI_INVALID_ARGUMENT);
                invalid = info; invalid.key_length = 4097;
                expect(xui_virtual_viewport_set_item(lease, row.root, &invalid) == XUI_INVALID_ARGUMENT);
            }
            ok(xui_virtual_viewport_set_item(lease, row.root, &info));
        }
    }
    HWND editor(uint32_t index) {
        const auto wanted = L"row-value-" + std::to_wstring(index);
        struct Search { const std::wstring& name; HWND value{}; } search{wanted};
        EnumChildWindows(frame, [](HWND hwnd, LPARAM context) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(context);
            wchar_t name[128]{}, type[64]{};
            GetWindowTextW(hwnd, name, 128); GetClassNameW(hwnd, type, 64);
            if (_wcsicmp(type, L"EDIT") == 0 && search.name == name) { search.value = hwnd; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        expect(search.value != nullptr); return search.value;
    }
    bool onscreen(HWND hwnd) {
        RECT rect{}; GetWindowRect(hwnd, &rect);
        for (auto parent = GetParent(hwnd); parent; parent = GetParent(parent)) {
            RECT clip{}; GetClientRect(parent, &clip);
            MapWindowPoints(parent, nullptr, reinterpret_cast<POINT*>(&clip), 2);
            if (!IntersectRect(&rect, &rect, &clip)) return false;
        }
        return rect.bottom > rect.top && rect.right > rect.left;
    }
    void check_native(bool at_end) {
        frame = FindWindowW(L"Xui.Window.1", L"Virtual viewport ABI");
        expect(frame != nullptr);
        if (!native_content) EnumChildWindows(frame, [](HWND hwnd, LPARAM context) -> BOOL {
            wchar_t type[64]{}; GetClassNameW(hwnd, type, 64);
            if (std::wstring_view(type) == L"Xui.ScrollContent.1") {
                *reinterpret_cast<HWND*>(context) = hwnd; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&native_content));
        expect(native_content != nullptr);
        RECT actual{}; GetClientRect(native_content, &actual);
        expect(actual.right > 0 && actual.bottom > 0 && actual.right <= 32767 && actual.bottom <= 32767);
        const auto request = read_request();
        expect(request.committed.extent == 1280000 && request.committed.height > 0 && actual.bottom < 10000);
        const auto requested_dpi = dpi_index ? dpis[dpi_index - 1] : GetDpiForWindow(frame);
        expect(actual.right == std::lround(request.committed.width * requested_dpi / 96.0f) &&
            actual.bottom == std::lround(request.committed.height * requested_dpi / 96.0f));
        expect(onscreen(editor(at_end ? 9999 : 0)));
        unsigned count{};
        EnumChildWindows(frame, [](HWND, LPARAM data) -> BOOL { ++*reinterpret_cast<unsigned*>(data); return TRUE; },
            reinterpret_cast<LPARAM>(&count));
        expect(count <= 64);
        std::cout << "Virtual geometry dpi_request=" << requested_dpi << " dpi_os=" << GetDpiForWindow(frame)
            << " logical_extent=" << request.committed.extent << " native_content=" << actual.right << 'x' << actual.bottom
            << " mounted_rows=" << rows.size() << " child_hwnds=" << count << " end=" << at_end << '\n';
        if (first_edit) {
            expect(editor(0) == first_edit && IsWindowVisible(first_edit) && GetFocus() == first_edit);
            uint64_t start{}, end{}; ok(xui_text_input_selection_get(rows.front().input, &start, &end));
            expect(start == 2 && end == 6);
        }
    }
    void process() {
        const auto request = latest;
        uint32_t result{};
        ok(xui_virtual_viewport_try_begin_update(lease, request.epoch, &result));
        if (result == XUI_VIRTUAL_SUPERSEDED) return;
        if (result == XUI_VIRTUAL_BLOCKED) {
            expect(phase == 3 && request.blocked);
            const auto current = read_request();
            expect(current.committed.offset > 1000000 && current.requested.offset == 0);
            expect(GetFocus() == first_edit && !onscreen(first_edit));
            ok(xui_virtual_viewport_cancel(lease, request.epoch));
            phase = 4;
            SendMessageW(first_edit, WM_IME_ENDCOMPOSITION, 0, 0);
            return;
        }
        expect(result == XUI_VIRTUAL_READY);
        expect(xui_virtual_viewport_flush_committed(lease, request.epoch) == XUI_INVALID_ARGUMENT);
        if (!built) {
            build();
            const auto window = FindWindowW(L"Xui.Window.1", L"Virtual viewport ABI");
            expect(window != nullptr);
            unsigned editors{};
            EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
                wchar_t type[32]{}; GetClassNameW(child, type, 32);
                if (_wcsicmp(type, L"EDIT") == 0) ++*reinterpret_cast<unsigned*>(value);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&editors));
            expect(editors == 0);
        }
        metadata(request.requested_source_version);
        if (invalid_coverage) {
            if (invalid_coverage == 2) height(rows[1].root, 64);
            expect(xui_virtual_viewport_try_commit(lease, request.epoch, &result) == XUI_NATIVE_ERROR);
            automatic = false; phase = 11;
            ok(xui_content_release(scope));
            uint32_t count{}; ok(xui_content_handle_count(window, &count)); expect(count == 3);
            return;
        }
        if (phase == 5) {
            ok(xui_virtual_viewport_set_extent(lease, 10000, 3));
            RECT outer{}; GetWindowRect(frame, &outer);
            SetWindowPos(frame, nullptr, outer.left, outer.top, outer.right - outer.left, outer.bottom - outer.top - 30,
                SWP_NOZORDER | SWP_NOACTIVATE);
            SendMessageW(frame, WM_APP + 12, 0, 0);
        }
        ok(xui_virtual_viewport_try_commit(lease, request.epoch, &result));
        expect(result == XUI_VIRTUAL_COMMITTED);
        const auto notified_before_flush = notifications;
        ok(xui_virtual_viewport_flush_committed(lease, request.epoch));
        expect(notifications == notified_before_flush);
        expect(xui_virtual_viewport_flush_committed(lease, 0) == XUI_INVALID_ARGUMENT);
        expect(xui_virtual_viewport_flush_committed(lease, request.epoch + 1) == XUI_INVALID_ARGUMENT);
        const auto current = read_request();
        expect(current.committed_source_version == request.requested_source_version &&
            std::memcmp(&current.committed, &request.requested, sizeof(current.committed)) == 0);
        ++commits;
        if (phase == 0) {
            check_native(false);
            const auto before = notifications;
            phase = 1;
            ok(xui_virtual_viewport_request_offset(lease, current.committed.offset));
            const auto equal = read_request();
            expect(equal.epoch > current.epoch && notifications == before &&
                std::memcmp(&equal.requested, &current.committed, sizeof(equal.requested)) == 0);
            return;
        } else if (phase == 1) {
            check_native(false);
            const auto wheel_epoch = current.epoch;
            SendMessageW(native_content, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
            expect(read_request().requested.offset == 0 && read_request().epoch == wheel_epoch && IsWindow(frame));
            ok(xui_content_begin_append(scope));
            const auto outside = stack(); height(outside, 128);
            xui_handle retired_input{};
            ok(xui_create(window, XUI_TEXT_INPUT, text("Offscreen retirement"), 0, &retired_input));
            xui_property property{sizeof(property), XUI_TEXT, retired_input, text("offscreen-retired")};
            ok(xui_update(window, &property, 1));
            ok(xui_stack_add(outside, retired_input, 0)); ok(xui_content_end_append(scope, 1));
            ok(xui_content_stack_insert(rows_root, 17, outside, 0));
            HWND native_retired{};
            EnumChildWindows(frame, [](HWND child, LPARAM value) -> BOOL {
                wchar_t name[64]{}; GetWindowTextW(child, name, 64);
                if (std::wstring_view(name) == L"offscreen-retired") {
                    *reinterpret_cast<HWND*>(value) = child; return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&native_retired));
            expect(native_retired && IsWindow(native_retired));
            ok(xui_content_stack_remove(rows_root, outside));
            expect(!IsWindow(native_retired));
            ok(xui_content_release_element(scope, retired_input));
            ok(xui_content_release_element(scope, outside));
            ok(xui_virtual_viewport_flush_committed(lease, request.epoch));
            first_edit = editor(0); ok(xui_focus(rows.front().input, 0));
            ok(xui_text_input_selection_set(rows.front().input, 2, 6));
            ok(xui_control_interaction_subscribe(rows.front().input, interaction, this));
            phase = 2;
            ok(xui_virtual_viewport_request_offset(lease, (std::numeric_limits<float>::max)()));
        } else if (phase == 2) {
            check_native(true); expect(!onscreen(first_edit));
            phase = 3;
            SendMessageW(first_edit, WM_IME_STARTCOMPOSITION, 0, 0);
            xui_control_interaction interaction_state{sizeof(interaction_state), XUI_CONTROL_INTERACTION_VERSION};
            ok(xui_control_interaction_get(rows.front().input, &interaction_state));
            expect(interaction_state.has_focus == 1 && interaction_state.is_composing == 1);
            ok(xui_virtual_viewport_request_offset(lease, 0));
            RECT outer{}; GetWindowRect(frame, &outer);
            SetWindowPos(frame, nullptr, outer.left, outer.top, outer.right - outer.left, outer.bottom - outer.top + 100,
                SWP_NOZORDER | SWP_NOACTIVATE);
            SendMessageW(frame, WM_APP + 12, 0, 0);
            RECT held{}; GetClientRect(native_content, &held);
            expect(held.bottom == std::lround(current.committed.height * GetDpiForWindow(frame) / 96.0f));
        } else if (phase == 4) {
            check_native(false);
            phase = 5; ok(xui_virtual_viewport_set_extent(lease, 10000, 2));
        } else if (phase == 5) {
            expect(current.committed_source_version == 2 && current.requested_source_version == 3);
            expect(current.committed.height > current.requested.height);
            RECT parent{}, child{};
            GetClientRect(GetParent(native_content), &parent); GetClientRect(native_content, &child);
            expect(child.bottom <= parent.bottom);
            phase = 6;
        } else if (phase == 6) {
            expect(current.committed_source_version == 3);
            automatic = false; phase = 7; start_automation();
        } else if (phase == 8) {
            check_native(true);
            drive_dpi();
        } else if (phase == 9) {
            expect(current.committed_source_version == 5);
            const auto old_lease = lease;
            ok(xui_virtual_viewport_request_offset(lease, 100));
            ok(xui_content_release(scope));
            uint32_t count{}; ok(xui_content_handle_count(window, &count)); expect(count == 3);
            xui_virtual_viewport_request stale{sizeof(stale), XUI_VIRTUAL_VIEWPORT_VERSION};
            expect(xui_virtual_viewport_get_request(old_lease, &stale) == XUI_INVALID_HANDLE);
            automatic = false; phase = 10;
            retirement_signalled = true; retirement.set_value();
        }
    }
    static xui_status XUI_CALL drive(void* context, uint32_t execute) {
        if (execute) static_cast<Fixture*>(context)->drive_dpi();
        return XUI_OK;
    }
    void drive_dpi() {
        if (dpi_index < dpis.size()) {
            const auto dpi = dpis[dpi_index++];
            RECT outer{}; GetWindowRect(frame, &outer);
            SendMessageW(frame, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&outer));
            SendMessageW(frame, WM_APP + 12, 0, 0);
            automatic = true; phase = 8;
            ok(xui_virtual_viewport_request_offset(lease, (std::numeric_limits<float>::max)()));
            return;
        }
        const auto original = lease;
        ok(xui_control_interaction_subscribe(rows.front().input, nullptr, nullptr));
        ok(xui_virtual_viewport_request_offset(lease, 0));
        ok(xui_virtual_viewport_release(lease));
        RECT closed{}; GetClientRect(native_content, &closed);
        expect(closed.right == 0 && closed.bottom == 0);
        xui_virtual_viewport_request stale{sizeof(stale), XUI_VIRTUAL_VIEWPORT_VERSION};
        expect(xui_virtual_viewport_get_request(original, &stale) == XUI_INVALID_HANDLE);
        xui_virtual_viewport_options options{sizeof(options), XUI_VIRTUAL_VIEWPORT_VERSION, 10000, 128, 5};
        phase = 9; automatic = true;
        ok(xui_virtual_viewport_begin(scroll, &options, notified, this, &lease));
        expect(lease != original);
    }
    static xui_status XUI_CALL finish(void* context, uint32_t execute) {
        return execute ? xui_window_close(static_cast<Fixture*>(context)->window) : XUI_OK;
    }
    void start_automation() {
        auto retired = retirement.get_future();
        automation_thread = std::thread([this, retired = std::move(retired)]() mutable {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            expect(SUCCEEDED(initialized));
            {
                ComPtr<IUIAutomation> automation;
                expect(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))));
                ComPtr<IUIAutomationElement> editor_element, row, list;
                expect(SUCCEEDED(automation->ElementFromHandle(first_edit, &editor_element)));
                ComPtr<IUIAutomationTreeWalker> walker;
                expect(SUCCEEDED(automation->get_ControlViewWalker(&walker)));
                expect(SUCCEEDED(walker->GetParentElement(editor_element.Get(), &row)) && row);
                CONTROLTYPEID type{}; expect(SUCCEEDED(row->get_CurrentControlType(&type)) && type == UIA_ListItemControlTypeId);
                VARIANT position{}, count{};
                expect(SUCCEEDED(row->GetCurrentPropertyValue(UIA_PositionInSetPropertyId, &position)));
                expect(SUCCEEDED(row->GetCurrentPropertyValue(UIA_SizeOfSetPropertyId, &count)));
                expect(position.vt == VT_I4 && position.lVal == 1 && count.vt == VT_I4 && count.lVal == 10000);
                VariantClear(&position); VariantClear(&count);
                expect(SUCCEEDED(walker->GetParentElement(row.Get(), &list)) && list);
                expect(SUCCEEDED(list->get_CurrentControlType(&type)) && type == UIA_ListControlTypeId);
                ComPtr<IUIAutomationValuePattern> value;
                expect(SUCCEEDED(editor_element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value);
                BSTR contents{}; expect(SUCCEEDED(value->get_CurrentValue(&contents)));
                expect(std::wstring_view(contents) == L"row-value-0"); SysFreeString(contents);
                ComPtr<IUIAutomationElement> last;
                expect(SUCCEEDED(walker->GetLastChildElement(list.Get(), &last)) && last);
                expect(SUCCEEDED(last->GetCurrentPropertyValue(UIA_PositionInSetPropertyId, &position)));
                expect(position.vt == VT_I4 && position.lVal == 10000); VariantClear(&position);
                ok(xui_window_post(window, drive, this));
                expect(retired.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
                BSTR name{}; const auto unavailable = row->get_CurrentName(&name); SysFreeString(name);
                expect(unavailable == UIA_E_ELEMENTNOTAVAILABLE);
            }
            CoUninitialize();
            ok(xui_window_post(window, finish, this));
        });
    }
    void run() {
        static_assert(sizeof(xui_virtual_viewport_options) == 24 && sizeof(xui_virtual_viewport_request) == 72 &&
            sizeof(xui_virtual_item_info) == 40 && sizeof(xui_control_interaction) == 16);
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Virtual viewport ABI"), 700, 500};
        ok(xui_window_create(&options, &window));
        xui_handle outer{}; ok(xui_stack_create(window, 1, &outer));
        ok(xui_content_host_create(window, &host)); ok(xui_stack_add(outer, host, 1)); ok(xui_window_content(window, outer));
        ok(xui_content_begin(host, &scope));
        root = stack(); rows_root = stack();
        ok(xui_create(window, XUI_SCROLL_VIEW, text("Virtual items"), rows_root, &scroll));
        ok(xui_stack_add(root, scroll, 1)); ok(xui_content_commit(scope, root));
        xui_virtual_viewport_options viewport{sizeof(viewport), XUI_VIRTUAL_VIEWPORT_VERSION, 10000, 128, 1};
        ok(xui_virtual_viewport_begin(scroll, &viewport, notified, this, &lease));
        expect(notifications == 0);
        xui_handle owner{}; ok(xui_content_owner(lease, &owner)); expect(owner == scope);
        const auto initial = read_request(); expect(initial.committed.height == 0 && initial.requested.height == 0);
        expect(xui_virtual_viewport_flush_committed(lease, initial.epoch) == XUI_INVALID_ARGUMENT);
        const auto result = xui_window_run(window);
        if (invalid_coverage) {
            expect(result == XUI_NATIVE_ERROR && phase == 11);
            ok(xui_window_destroy(window)); return;
        }
        ok(result);
        if (automation_thread.joinable()) automation_thread.join();
        expect(phase == 10 && commits >= 9 && notifications > 0 && interactions > 0 && retirement_signalled);
        ok(xui_window_destroy(window));
    }
};
void run() {
    Fixture{}.run();
    Fixture missing; missing.invalid_coverage = 1; missing.run();
    Fixture geometry; geometry.invalid_coverage = 2; geometry.run();
}
}

#include "xui/xui_retained_pages.h"

namespace retained_pages_abi {
struct Row { uint64_t id{}; xui_handle root{}, input{}, action{}; HWND edit{}; int native_id{}; };
void contracts() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Retained page construction"), 640, 480};
    xui_handle window{}, outer{}, host{}, second_host{}, scope{}, pages{}, first{}, second{}, foreign_window{}, foreign_root{};
    ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &outer));
    ok(xui_content_host_create(window, &host)); ok(xui_content_host_create(window, &second_host));
    ok(xui_stack_add(outer, host, 1)); ok(xui_stack_add(outer, second_host, 0)); ok(xui_window_content(window, outer));
    pages = 99; expect(xui_retained_pages_create(window, text("No candidate"), &pages) == XUI_BUSY && pages == 0);
    ok(xui_content_begin(host, &scope)); ok(xui_retained_pages_create(window, text("Pages"), &pages));
    xui_page_entry entries[]{{sizeof(xui_page_entry), 1, 11, text("First")}, {sizeof(xui_page_entry), 1, 22, text("Second")}};
    ok(xui_retained_pages_set(pages, entries, 2, 11));
    uint64_t selected{}; uint32_t visible{}, count{};
    const auto unchanged = [&] {
        ok(xui_retained_pages_get_state(pages, &selected, &visible, &count));
        expect(selected == 11 && visible == 1 && count == 2);
    };
    for (unsigned i = 0; i < 6; ++i) {
        auto invalid = entries[1];
        if (i == 0) invalid.id = 0;
        if (i == 1) invalid.id = UINT64_MAX;
        if (i == 2) invalid.title = text("");
        if (i == 3) invalid.title = text(" \t");
        if (i == 4) invalid.enabled = 2;
        if (i == 5) invalid.id = 11;
        xui_page_entry bad[]{entries[0], invalid};
        expect(xui_retained_pages_set(pages, bad, 2, 11) == XUI_INVALID_ARGUMENT); unchanged();
    }
    auto wrong_size = entries[0]; wrong_size.size = 0;
    expect(xui_retained_pages_validate(pages, &wrong_size, 1, 11) == XUI_VERSION_MISMATCH);
    expect(xui_retained_pages_set(pages, nullptr, 1, 11) == XUI_INVALID_ARGUMENT);
    expect(xui_retained_pages_set(pages, entries, 2, 33) == XUI_INVALID_ARGUMENT);
    expect(xui_retained_pages_set_visible(pages, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_retained_pages_insert(pages, 0, 11, pages) == XUI_INVALID_ARGUMENT);
    ok(xui_stack_create(window, 1, &first)); ok(xui_stack_create(window, 1, &second));
    ok(xui_retained_pages_insert(pages, 0, 11, first));
    expect(xui_content_commit(scope, pages) == XUI_INVALID_ARGUMENT);
    expect(xui_retained_pages_insert(pages, 1, 11, second) == XUI_INVALID_ARGUMENT);
    ok(xui_window_create(&options, &foreign_window)); ok(xui_stack_create(foreign_window, 1, &foreign_root));
    expect(xui_retained_pages_insert(pages, 1, 22, foreign_root) == XUI_INVALID_ARGUMENT);
    ok(xui_window_destroy(foreign_window));
    ok(xui_retained_pages_insert(pages, 1, 22, second));
    expect(xui_retained_pages_insert(pages, 2, 33, first) == XUI_INVALID_ARGUMENT);
    expect(xui_retained_pages_get_page_id(pages, 2, &selected) == XUI_INVALID_ARGUMENT);
    ok(xui_retained_pages_validate_move(pages, first, UINT32_MAX));
    expect(xui_retained_pages_move(pages, first, UINT32_MAX) == XUI_INVALID_ARGUMENT);
    const auto selector = create(window, XUI_TAB_STRIP);
    ok(xui_page_selector_set(selector, entries, 2, 11)); ok(xui_page_selector_connect(selector, pages));
    const auto candidate = [&] {
        xui_handle value{}; ok(xui_stack_create(window, 1, &value)); return value;
    }();
    ok(xui_stack_add(candidate, selector, 0));
    expect(xui_content_commit(scope, candidate) == XUI_INVALID_ARGUMENT);
    ok(xui_stack_add(candidate, pages, 1));
    ok(xui_content_commit(scope, candidate));
    unchanged();
    xui_handle previous{}; ok(xui_content_context(window, scope, &previous));
    expect(xui_retained_pages_set(pages, entries, 2, 22) == XUI_BUSY);
    xui_handle ignored{}; ok(xui_content_context(window, previous, &ignored));
    uint32_t baseline{}; ok(xui_content_handle_count(window, &baseline));
    ok(xui_content_begin_append(scope));
    expect(xui_retained_pages_set(pages, entries, 2, 22) == XUI_BUSY);
    expect(xui_page_selector_set(selector, entries, 2, 11) == XUI_BUSY);
    const auto appended = create(window, XUI_TAB_STRIP);
    ok(xui_page_selector_set(appended, entries, 2, 11));
    ok(xui_page_selector_connect(appended, pages));
    ok(xui_content_end_append(scope, 0));
    expect(xui_page_selector_get_state(appended, &selected, &count) == XUI_INVALID_HANDLE);
    ok(xui_content_handle_count(window, &count)); expect(count == baseline);
    xui_handle other_scope{}, other_pages{}, other_root{};
    ok(xui_content_begin(second_host, &other_scope));
    ok(xui_retained_pages_create(window, text("Other arena"), &other_pages));
    ok(xui_stack_create(window, 1, &other_root));
    expect(xui_retained_pages_insert(pages, 2, 33, other_root) == XUI_INVALID_ARGUMENT);
    expect(xui_page_selector_connect(selector, other_pages) == XUI_INVALID_ARGUMENT);
    const auto legacy = create(window, XUI_PAGE_VIEW);
    expect(xui_retained_pages_get_state(legacy, &selected, &visible, &count) == XUI_WRONG_KIND);
    ok(xui_content_release(other_scope)); unchanged();
    ok(xui_content_release(scope)); ok(xui_content_handle_count(window, &count)); expect(count == 4);
    ok(xui_window_destroy(window));
}
struct Fixture {
    xui_handle window{}, outer{}, host{}, scope{}, root{}, pages{}, tabs{}, navigation{}, outside{};
    std::vector<xui_page_entry> entries{{sizeof(xui_page_entry), 1, 11, text("First page")},
        {sizeof(xui_page_entry), 1, 22, text("Second page")}};
    Row first{11}, second{22};
    HWND frame{}, tab_window{};
    unsigned changes{}, selections{}, activations{}, closes{};
    uint64_t requested{};
    bool finished{};
    std::thread worker;
    std::promise<void> removed;
    static xui_status XUI_CALL selector_event(void* context, const xui_event* event) {
        auto& self = *static_cast<Fixture*>(context);
        if (event->kind == XUI_SELECTION) { ++self.selections; self.requested = event->value; }
        if (event->kind == XUI_CLICK) ++self.activations;
        if (event->kind == XUI_CANCEL) ++self.closes;
        if (event->kind == XUI_SELECTION || event->kind == XUI_CLICK)
            expect(xui_retained_pages_set(self.pages, self.entries.data(), 2, event->value) == XUI_BUSY);
        return XUI_OK;
    }
    void fixed(xui_handle handle, float width, float height) {
        const xui_axis_constraints axes{sizeof(axes), XUI_AXIS_CONSTRAINTS_VERSION,
            {XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, width}, {XUI_AXIS_OVERRIDE | XUI_AXIS_LENGTH, height}};
        ok(xui_element_set_axis_constraints(handle, &axes));
    }
    void row(Row& value, const char* contents) {
        ok(xui_stack_create(window, 1, &value.root));
        const auto name = "Page editor " + std::to_string(value.id);
        ok(xui_create(window, XUI_TEXT_INPUT, text(name.c_str()), 0, &value.input));
        ok(xui_create(window, XUI_BUTTON, text("Page action"), 0, &value.action));
        const auto automation = "page-field-" + std::to_string(value.id);
        xui_property properties[]{
            {sizeof(xui_property), XUI_TEXT, value.input, text(contents)},
            {sizeof(xui_property), XUI_AUTOMATION_ID, value.input, text(automation.c_str())}};
        ok(xui_update(window, properties, 2));
        ok(xui_stack_add(value.root, value.input, 0)); ok(xui_stack_add(value.root, value.action, 0));
    }
    static HWND named(HWND frame, const wchar_t* text) {
        struct Search { const wchar_t* text; HWND found{}; } search{text};
        EnumChildWindows(frame, [](HWND hwnd, LPARAM context) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(context);
            wchar_t value[128]{}; GetWindowTextW(hwnd, value, 128);
            if (std::wstring_view(value) == search.text) { search.found = hwnd; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        expect(search.found != nullptr); return search.found;
    }
    void flush() { SendMessageW(frame, WM_APP + 12, 0, 0); }
    void state(uint64_t selected, bool visible = true) {
        uint64_t actual{}; uint32_t shown{}, count{};
        ok(xui_retained_pages_get_state(pages, &actual, &shown, &count));
        expect(actual == selected && shown == uint32_t(visible) && count == entries.size());
    }
    void project(uint64_t selected) {
        ok(xui_retained_pages_set(pages, entries.data(), static_cast<uint32_t>(entries.size()), selected));
        for (auto selector : {tabs, navigation}) {
            ok(xui_page_selector_set(selector, entries.data(), static_cast<uint32_t>(entries.size()), selected));
            uint64_t actual{}; uint32_t count{}; ok(xui_page_selector_get_state(selector, &actual, &count));
            expect(actual == selected && count == entries.size());
        }
        flush(); state(selected);
    }
    void identity(bool focused) {
        expect(IsWindow(first.edit) && IsWindow(second.edit) && GetDlgCtrlID(first.edit) == first.native_id &&
            GetDlgCtrlID(second.edit) == second.native_id);
        DWORD start{}, end{};
        SendMessageW(first.edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        expect(start == 2 && end == 6 && SendMessageW(first.edit, EM_CANUNDO, 0, 0));
        if (focused) expect(GetFocus() == first.edit);
    }
    void start() {
        frame = FindWindowW(L"Xui.Window.1", L"Retained pages ABI"); expect(frame != nullptr);
        first.edit = named(frame, L"First retained text"); second.edit = named(frame, L"Second retained text");
        tab_window = named(frame, L"Page tabs");
        first.native_id = GetDlgCtrlID(first.edit); second.native_id = GetDlgCtrlID(second.edit);
        expect(IsWindowVisible(first.edit) && !IsWindowVisible(second.edit));
        const auto bounds = grid_layout_abi::bounds(pages);
        expect(bounds.width == 600 && bounds.height == 180);
        ok(xui_focus(first.input, 0));
        SendMessageW(first.edit, EM_SETSEL, -1, -1);
        SendMessageW(first.edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L" \U0001f642"));
        SendMessageW(first.edit, EM_SETSEL, 2, 6);
        expect(changes == 1);
        expect(xui_retained_pages_validate(pages, entries.data(), 2, 22) == XUI_BUSY);
        expect(xui_retained_pages_set(pages, entries.data(), 2, 22) == XUI_BUSY);
        expect(xui_retained_pages_validate_visible(pages, 0) == XUI_BUSY);
        expect(xui_retained_pages_set_visible(pages, 0) == XUI_BUSY);
        auto visible = value(); visible.first = 0;
        expect(xui_feature_set(pages, XUI_F_VISIBLE, &visible) == XUI_BUSY);
        expect(xui_retained_pages_remove(pages, first.root) == XUI_BUSY);
        state(11); identity(true);
        SendMessageW(first.edit, WM_IME_STARTCOMPOSITION, 0, 0);
        expect(xui_retained_pages_validate(pages, entries.data(), 2, 11) == XUI_BUSY);
        expect(xui_retained_pages_validate_move(pages, second.root, 100) == XUI_BUSY);
        expect(xui_content_begin_append(scope) == XUI_BUSY);
        xui_control_interaction interaction{sizeof(interaction), XUI_CONTROL_INTERACTION_VERSION};
        ok(xui_control_interaction_get(first.input, &interaction)); expect(interaction.is_composing && interaction.has_focus);
        SendMessageW(first.edit, WM_IME_ENDCOMPOSITION, 0, 0);
        ok(xui_retained_pages_validate_move(pages, first.root, 100));
        expect(xui_retained_pages_move(pages, first.root, 100) == XUI_INVALID_ARGUMENT);
        ok(xui_retained_pages_move(pages, first.root, 1));
        uint64_t id{}; ok(xui_retained_pages_get_page_id(pages, 0, &id)); expect(id == 22);
        expect(xui_retained_pages_set(pages, entries.data(), 2, 11) == XUI_INVALID_ARGUMENT);
        std::swap(entries[0], entries[1]); project(11); identity(true);
        ok(xui_retained_pages_move(pages, first.root, 0)); std::swap(entries[0], entries[1]); project(11); identity(true);
        expect(selections == 0 && activations == 0 && closes == 0 && changes == 1);
        expect(xui_content_stack_move(pages, first.root, 1) == XUI_WRONG_KIND);
        expect(xui_content_stack_remove(root, pages) == XUI_BUSY);
        ok(xui_focus(outside, 0)); project(22); identity(false);
        expect(!IsWindowVisible(first.edit) && IsWindowVisible(second.edit));
        ok(xui_retained_pages_validate_visible(pages, 0)); ok(xui_retained_pages_set_visible(pages, 0)); flush();
        state(22, false); expect(!IsWindowVisible(first.edit) && !IsWindowVisible(second.edit));
        expect(grid_layout_abi::bounds(pages).width == 0 && grid_layout_abi::bounds(pages).height == 0);
        expect(xui_focus(second.input, 0) != XUI_OK);
        ok(xui_retained_pages_set_visible(pages, 1)); flush(); state(22); identity(false);
        auto disabled = entries; disabled[0].enabled = 0;
        expect(xui_page_selector_validate(tabs, disabled.data(), 2, 22) == XUI_INVALID_ARGUMENT);
        ok(xui_page_selector_validate(navigation, disabled.data(), 2, 22));
        xui_property enable{sizeof(enable), XUI_ENABLED, navigation, {}, 0, 0, 0, 0, 0};
        ok(xui_update(window, &enable, 1)); project(11);
        enable.integer = 1; ok(xui_update(window, &enable, 1));
        ok(xui_focus(tabs, 0));
        SendMessageW(tab_window, WM_KEYDOWN, VK_RIGHT, 1);
        expect(selections == 1 && requested == 22); state(11);
        project(22);
        SendMessageW(tab_window, WM_KEYDOWN, VK_RETURN, 1); expect(activations == 1);
        uint32_t closable{}; ok(xui_page_selector_get_closable(tabs, &closable)); expect(closable == 0);
        SendMessageW(tab_window, WM_KEYDOWN, VK_DELETE, 1); expect(closes == 0);
        ok(xui_page_selector_set_closable(tabs, 1)); flush();
        ok(xui_page_selector_get_closable(tabs, &closable)); expect(closable == 1);
        SendMessageW(tab_window, WM_KEYDOWN, VK_DELETE, 1); expect(closes == 1);
        ok(xui_page_selector_set_closable(tabs, 0));
        ok(xui_subscribe(tabs, selector_event, this));
        ok(xui_page_selector_get_closable(tabs, &closable)); expect(closable == 0);
        identity(false);
        auto retirement = removed.get_future();
        worker = std::thread([this, retirement = std::move(retirement)]() mutable {
            const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED); expect(SUCCEEDED(com));
            {
                Microsoft::WRL::ComPtr<IUIAutomation> automation;
                expect(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))));
                Microsoft::WRL::ComPtr<IUIAutomationElement> first_provider, second_provider, root_provider;
                expect(SUCCEEDED(automation->ElementFromHandle(first.edit, &first_provider)));
                expect(SUCCEEDED(automation->ElementFromHandle(second.edit, &second_provider)));
                expect(SUCCEEDED(automation->ElementFromHandle(frame, &root_provider)));
                BOOL offscreen{};
                expect(SUCCEEDED(first_provider->get_CurrentIsOffscreen(&offscreen)) && offscreen);
                expect(SUCCEEDED(second_provider->get_CurrentIsOffscreen(&offscreen)) && !offscreen);
                Microsoft::WRL::ComPtr<IUIAutomationValuePattern> value;
                expect(SUCCEEDED(second_provider->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))));
                BSTR actual{}; expect(SUCCEEDED(value->get_CurrentValue(&actual)) && std::wstring_view(actual) == L"Second retained text");
                SysFreeString(actual);
                for (const auto& entry : {std::pair{L"page-field-11", 0}, std::pair{L"page-field-22", 1}}) {
                    VARIANT id{}; id.vt = VT_BSTR; id.bstrVal = SysAllocString(entry.first);
                    Microsoft::WRL::ComPtr<IUIAutomationCondition> identity, control, condition;
                    expect(SUCCEEDED(automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &identity)));
                    VariantClear(&id); expect(SUCCEEDED(automation->get_ControlViewCondition(&control)));
                    expect(SUCCEEDED(automation->CreateAndCondition(identity.Get(), control.Get(), &condition)));
                    Microsoft::WRL::ComPtr<IUIAutomationElementArray> results;
                    expect(SUCCEEDED(root_provider->FindAll(TreeScope_Descendants, condition.Get(), &results)));
                    int count{}; expect(SUCCEEDED(results->get_Length(&count)));
                    if (count != entry.second) std::wcerr << entry.first << L" ControlView count=" << count << L'\n';
                    expect(count == entry.second);
                }
                ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
                    if (execute) static_cast<Fixture*>(context)->mutations();
                    return XUI_OK;
                }, this));
                expect(retirement.wait_for(std::chrono::seconds(20)) == std::future_status::ready);
                expect(first_provider->get_CurrentIsOffscreen(&offscreen) == UIA_E_ELEMENTNOTAVAILABLE);
                ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
                    if (!execute) return XUI_OK;
                    auto& self = *static_cast<Fixture*>(context);
                    ok(xui_content_release(self.scope));
                    uint32_t count{}; ok(xui_content_handle_count(self.window, &count)); expect(count == 3);
                    self.finished = true; return xui_window_close(self.window);
                }, this));
            }
            CoUninitialize();
        });
    }
    void mutations() {
        ok(xui_focus(outside, 0));
        expect(xui_content_release_element(scope, first.input) == XUI_BUSY);
        ok(xui_retained_pages_remove(pages, first.root)); expect(!IsWindow(first.edit) && IsWindow(second.edit));
        for (auto handle : {first.action, first.input, first.root}) ok(xui_content_release_element(scope, handle));
        entries.erase(entries.begin()); project(22);
        uint32_t baseline{}; ok(xui_content_handle_count(window, &baseline));
        for (unsigned i = 0; i < 8; ++i) {
            ok(xui_content_begin_append(scope));
            Row row{100 + i}; this->row(row, "Inserted page text");
            ok(xui_content_end_append(scope, 1));
            auto future = entries; future.insert(future.begin(), {sizeof(xui_page_entry), 1, row.id, text("Inserted page")});
            ok(xui_retained_pages_validate(pages, future.data(), 2, 22));
            ok(xui_retained_pages_validate_move(pages, second.root, 1));
            ok(xui_retained_pages_insert(pages, 0, row.id, row.root));
            entries = std::move(future); project(22);
            uint64_t actual{}; ok(xui_retained_pages_get_page_id(pages, 0, &actual)); expect(actual == row.id);
            expect(IsWindow(second.edit) && GetDlgCtrlID(second.edit) == second.native_id);
            row.edit = named(frame, L"Inserted page text"); expect(!IsWindowVisible(row.edit));
            ok(xui_retained_pages_remove(pages, row.root)); expect(!IsWindow(row.edit));
            for (auto handle : {row.action, row.input, row.root}) ok(xui_content_release_element(scope, handle));
            entries.erase(entries.begin()); project(22);
            uint32_t count{}; ok(xui_content_handle_count(window, &count)); expect(count == baseline);
        }
        removed.set_value();
    }
    void run() {
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Retained pages ABI"), 940, 760};
        ok(xui_window_create(&options, &window)); ok(xui_window_visual_style_set(window, XUI_STYLE_WINUI));
        ok(xui_stack_create(window, 1, &outer)); ok(xui_content_host_create(window, &host));
        ok(xui_stack_add(outer, host, 1)); ok(xui_window_content(window, outer));
        ok(xui_content_begin(host, &scope)); ok(xui_stack_create(window, 1, &root));
        ok(xui_retained_pages_create(window, text("Native retained pages"), &pages)); fixed(pages, 600, 180);
        ok(xui_retained_pages_validate(pages, entries.data(), 2, 11));
        ok(xui_retained_pages_set(pages, entries.data(), 2, 11));
        row(first, "First retained text"); row(second, "Second retained text");
        ok(xui_retained_pages_insert(pages, 0, first.id, first.root));
        ok(xui_retained_pages_insert(pages, 1, second.id, second.root));
        xui_feature_options tab_options{sizeof(tab_options), XUI_FEATURE_VERSION, text("Page tabs")};
        ok(xui_feature_create(window, XUI_TAB_STRIP, &tab_options, &tabs));
        navigation = create(window, XUI_NAVIGATION_VIEW);
        xui_property sizes[]{{sizeof(xui_property), XUI_FIXED_SIZE, tabs, {}, 600, 40},
            {sizeof(xui_property), XUI_FIXED_SIZE, navigation, {}, 300, 180}};
        ok(xui_update(window, sizes, 2));
        for (auto selector : {tabs, navigation}) {
            ok(xui_subscribe(selector, selector_event, this));
            ok(xui_page_selector_validate(selector, entries.data(), 2, 11));
            ok(xui_page_selector_set(selector, entries.data(), 2, 11));
            ok(xui_page_selector_connect(selector, pages));
        }
        ok(xui_page_selector_set_closable(tabs, 0));
        ok(xui_create(window, XUI_BUTTON, text("Outside page focus"), 0, &outside));
        for (auto element : {tabs, navigation, outside, pages}) ok(xui_stack_add(root, element, 0));
        ok(xui_subscribe(first.input, [](void* context, const xui_event* event) -> xui_status {
            if (event->kind == XUI_CHANGE) ++static_cast<Fixture*>(context)->changes;
            return XUI_OK;
        }, this));
        ok(xui_content_commit(scope, root));
        ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
            if (execute) static_cast<Fixture*>(context)->start();
            return XUI_OK;
        }, this));
        const auto result = xui_window_run(window);
        if (worker.joinable()) worker.join();
        ok(result); expect(finished); ok(xui_window_destroy(window));
    }
};
void run() {
    static_assert(sizeof(xui_page_entry) == 32);
    expect(xui_retained_pages_version() == XUI_RETAINED_PAGES_VERSION);
    contracts();
    Fixture fixture; fixture.run();
}
}

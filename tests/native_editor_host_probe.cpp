#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/foundation.hpp"
#include "drawing.hpp"
#include "control_accessibility.hpp"
#include "native_editor_test_support.hpp"
#include "host_peer.hpp"
#include "native_document.hpp"
#include "xui/native_edit.hpp"
#if __has_include("native_field_host.hpp")
#include "text_input_host.hpp"
#include "document_host.hpp"
#include "password_host.hpp"
#include "date_time_host.hpp"
#endif
#include <UIAutomation.h>
#include <commctrl.h>
#include <richedit.h>
#include <psapi.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <future>
#include <new>
#include <thread>
#include <string_view>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace {
std::atomic<std::uint64_t> allocations{}, requested_bytes{};
thread_local std::uint64_t local_allocations{}, local_bytes{};
bool measured{};
bool diagnostic_uia{};
}
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    requested_bytes.fetch_add(size, std::memory_order_relaxed);
    ++local_allocations; local_bytes += size;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
constexpr UINT update_message = WM_APP + 12, metrics_message = WM_APP + 60;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void success(HRESULT value, const char* message) {
    if (FAILED(value)) throw std::runtime_error(std::string(message) + ": HRESULT=" + std::to_string(value));
}
std::uint64_t ticks(FILETIME value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
struct Cpu { std::uint64_t kernel{}, user{}; };
Cpu cpu(HANDLE handle, bool process = false) {
    FILETIME created{}, exited{}, kernel{}, user{};
    require((process ? GetProcessTimes(handle, &created, &exited, &kernel, &user) :
        GetThreadTimes(handle, &created, &exited, &kernel, &user)) != FALSE, "Read exact thread/process CPU counters");
    return {ticks(kernel), ticks(user)};
}
struct Measurement {
    Cpu owner_before, owner_after, query_before, query_after, process_before, process_after;
    std::uint64_t allocations{}, bytes{}, caller_allocations{}, caller_bytes{}, utc_before{}, utc_after{};
    double wall_ms{};
};
template<class F> Measurement measure(HANDLE owner, unsigned count, F action) {
    Measurement result;
    const auto before_allocations = allocations.load(), before_bytes = requested_bytes.load();
    const auto before_local = local_allocations, before_local_bytes = local_bytes;
    FILETIME utc{};
    std::chrono::steady_clock::time_point before{};
    if (measured) {
        GetSystemTimePreciseAsFileTime(&utc); result.utc_before = ticks(utc);
        result.owner_before = cpu(owner);
        result.query_before = cpu(GetCurrentThread());
        result.process_before = cpu(GetCurrentProcess(), true);
        before = std::chrono::steady_clock::now();
    }
    for (unsigned index = 0; index != count; ++index) action(index);
    if (measured) {
        result.wall_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        result.process_after = cpu(GetCurrentProcess(), true);
        result.query_after = cpu(GetCurrentThread());
        result.owner_after = cpu(owner);
        GetSystemTimePreciseAsFileTime(&utc); result.utc_after = ticks(utc);
    }
    result.allocations = allocations.load() - before_allocations;
    result.bytes = requested_bytes.load() - before_bytes;
    result.caller_allocations = local_allocations - before_local;
    result.caller_bytes = local_bytes - before_local_bytes;
    for (const auto pair : {std::pair{result.owner_before, result.owner_after},
        std::pair{result.query_before, result.query_after}, std::pair{result.process_before, result.process_after}})
        require(pair.second.kernel >= pair.first.kernel && pair.second.user >= pair.first.user,
            "Raw CPU counters are monotonic");
    require(result.utc_after >= result.utc_before, "Measured UTC does not move backwards");
    return result;
}
std::uint64_t mix(std::uint64_t checksum, std::uint64_t value) {
    return ((checksum * 16777619) ^ value) & 0x7fffffffffffffffULL;
}
struct Writes {
    std::uint64_t text{}, fonts{}, selections{}, insets{}, notifications{}, date_values{}, date_ranges{}, destroyed{}, placements{}, updates{}, geometry_notifications{};
    Writes operator-(const Writes& other) const {
        return {text - other.text, fonts - other.fonts, selections - other.selections,
            insets - other.insets, notifications - other.notifications, date_values - other.date_values,
            date_ranges - other.date_ranges, destroyed - other.destroyed, placements - other.placements, updates - other.updates,
            geometry_notifications - other.geometry_notifications};
    }
    bool editor_empty() const { return !text && !fonts && !selections && !insets && !date_values && !date_ranges; }
};
struct Work {
    std::uint64_t updates{}, text_callbacks{}, password_callbacks{}, date_callbacks{}, checksum{1}, queries{};
};
struct Resources {
    DWORD gdi{}, user{}, handles{};
    SIZE_T private_bytes{}, working_set{};
};
Resources resources() {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    DWORD handles{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) &&
        GetProcessHandleCount(GetCurrentProcess(), &handles), "Read owned process resource/private-commit counters");
    return {xui::test::gui_resources(GR_GDIOBJECTS), xui::test::gui_resources(GR_USEROBJECTS),
        handles, memory.PrivateUsage, memory.WorkingSetSize};
}
void report_resources(const char* name, const Resources& value) {
    std::cout << ",\"" << name << "\":{\"gdi\":" << value.gdi << ",\"user\":" << value.user <<
        ",\"handles\":" << value.handles << ",\"private_commit_bytes\":" << value.private_bytes <<
        ",\"working_set_bytes\":" << value.working_set << '}';
}
void report(const char* phase, unsigned count, const Measurement& value, const Work& work, const Writes& writes,
    const Resources& before, const Resources& after) {
    const auto emit_cpu = [](const char* name, Cpu before, Cpu after) {
        std::cout << ",\"" << name << "_kernel_before100ns\":" << before.kernel
            << ",\"" << name << "_kernel_after100ns\":" << after.kernel
            << ",\"" << name << "_user_before100ns\":" << before.user
            << ",\"" << name << "_user_after100ns\":" << after.user;
    };
    std::cout << "{\"phase\":\"" << phase << "\",\"iterations\":" << count << ",\"measured\":" << (measured ? "true" : "false");
    if (measured) {
        std::cout << ",\"wall_ms\":" << value.wall_ms << ",\"utc_before_filetime100ns\":" << value.utc_before
            << ",\"utc_after_filetime100ns\":" << value.utc_after;
        emit_cpu("owner", value.owner_before, value.owner_after);
        emit_cpu("query", value.query_before, value.query_after);
        emit_cpu("process", value.process_before, value.process_after);
    }
    std::cout << ",\"cpp_allocations\":" << value.allocations << ",\"cpp_requested_bytes\":" << value.bytes
        << ",\"caller_cpp_allocations\":" << value.caller_allocations << ",\"caller_cpp_requested_bytes\":" << value.caller_bytes
        << ",\"updates\":" << work.updates << ",\"text_callbacks\":" << work.text_callbacks
        << ",\"password_callbacks\":" << work.password_callbacks << ",\"date_callbacks\":" << work.date_callbacks
        << ",\"queries\":" << work.queries << ",\"checksum\":" << work.checksum
        << ",\"native_text_writes\":" << writes.text << ",\"native_font_writes\":" << writes.fonts
        << ",\"native_selection_writes\":" << writes.selections << ",\"native_inset_writes\":" << writes.insets
        << ",\"native_notifications\":" << writes.notifications << ",\"native_date_value_writes\":" << writes.date_values
        << ",\"native_date_range_writes\":" << writes.date_ranges << ",\"native_destructions\":" << writes.destroyed
        << ",\"native_placement_notifications\":" << writes.placements << ",\"native_host_updates\":" << writes.updates
        << ",\"native_move_size_notifications\":" << writes.geometry_notifications;
    report_resources("resources_before", before); report_resources("resources_after", after);
    std::cout << "}\n" << std::flush;
}
unsigned descendants(HWND window) {
    unsigned count{};
    EnumChildWindows(window, [](HWND, LPARAM value) -> BOOL { ++*reinterpret_cast<unsigned*>(value); return TRUE; },
        reinterpret_cast<LPARAM>(&count));
    return count;
}
HWND child(HWND parent, const wchar_t* name, unsigned ordinal = 0) {
    struct Search { const wchar_t* name; unsigned ordinal; HWND result{}; } search{name, ordinal};
    EnumChildWindows(parent, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t name[128]{}; GetClassNameW(hwnd, name, 128);
        if (!_wcsicmp(name, search.name) && search.ordinal-- == 0) { search.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find exact owned native field");
    return search.result;
}
void storage_work() {
    std::cout << "{\"phase\":\"actual-type-storage\",\"host_peer\":" << sizeof(HostPeer) <<
        ",\"native_edit_bridge\":" << sizeof(NativeEditBridge) << ",\"native_document_bridge\":" << sizeof(NativeDocumentBridge) <<
        ",\"remaining_control_snapshot\":" << sizeof(ControlSnapshot) << ",\"remaining_control_accessibility\":" << sizeof(ControlAccessibility);
#if __has_include("native_field_host.hpp")
    std::cout << ",\"native_field_peer\":" << sizeof(NativeFieldHostPeer) << ",\"text_input_peer\":" << sizeof(TextInputHostPeer) <<
        ",\"text_clear_peer\":" << sizeof(TextClearHostPeer) << ",\"document_peer\":" << sizeof(DocumentTextHostPeer) <<
        ",\"password_peer\":" << sizeof(PasswordHostPeer) << ",\"date_time_peer\":" << sizeof(DateTimeHostPeer) <<
        ",\"native_field_accessibility\":" << sizeof(NativeFieldAccessibility) <<
        ",\"native_password_bridge\":" << sizeof(NativePasswordBridge) << ",\"native_date_time_bridge\":" << sizeof(NativeDateTimeBridge);
#endif
    std::cout << "}\n" << std::flush;
    WindowOptions options;
    options.title = L"XUI actual native editor factory storage";
    options.size = {640, 480}; options.show_activated = false;
    options.visual_style = VisualStyle::winui; options.theme = ThemeMode::light;
    Window window(options);
    window.set_presentation("Segoe UI", 14, false, false);
    auto host = std::make_shared<ContentHost>();
    auto root = std::make_shared<Stack>(Axis::vertical); root->add(host, 1);
    window.set_content(root);
    bool completed{};
    window.post([&] {
        const auto settle = [&] {
            MSG message{}; unsigned count{};
            while (PeekMessageW(&message, window.native_window(), update_message, update_message, PM_REMOVE)) {
                require(++count <= 32, "Native materialization reaches a bounded settled state");
                DispatchMessageW(&message);
            }
            require(window.state() == WindowState::open, "Actual native factory retains its live owner");
        };
        const auto control = [](const char* kind) -> std::shared_ptr<Control> {
            std::shared_ptr<Control> result;
            if (!std::strcmp(kind, "input")) {
                auto input = std::make_shared<TextInput>(L"Actual native field factory caption");
                input->set_text(L"Actual native materialized field \U0001f600"); result = std::move(input);
            } else if (!std::strcmp(kind, "plain") || !std::strcmp(kind, "rich")) {
                std::shared_ptr<DocumentText> document = !std::strcmp(kind, "plain") ?
                    std::make_shared<MultilineText>() : std::static_pointer_cast<DocumentText>(std::make_shared<RichText>());
                document->set_text(L"Actual native materialized document \U0001f600"); result = std::move(document);
            } else if (!std::strcmp(kind, "password")) {
                auto password = std::make_shared<PasswordInput>();
                password->set_password(L"owned-storage-fixture"); result = std::move(password);
            } else if (!std::strcmp(kind, "label")) result = std::make_shared<Label>(L"Unrelated remaining-family storage");
            else result = std::make_shared<DateTimePicker>(L"Actual native field factory date/time",
                !std::strcmp(kind, "date") ? DateTimePresentation::date :
                    !std::strcmp(kind, "time") ? DateTimePresentation::time : DateTimePresentation::calendar);
            result->set_automation_id(L"actual-native-field-factory-identity");
            result->set_help_text(L"Actual native field factory independent metadata");
            return result;
        };
        Resources warmed;
        for (unsigned cycle = 0; cycle != 3; ++cycle) {
            for (const auto kind : {"input", "plain", "rich", "password", "date", "time", "calendar", "label"}) {
                const auto model = control(kind);
                settle();
                const auto before = allocations.load(), bytes = requested_bytes.load();
                window.replace_content(*host, model);
                settle();
                const auto count = allocations.load() - before, requested = requested_bytes.load() - bytes;
                const auto attached = descendants(window.native_window());
                require(attached == (!std::strcmp(kind, "input") || !std::strcmp(kind, "time") ? 2u : 1u),
                    "Actual factory creates exactly its authored native children, including native date up/down or caption");
                std::cout << "{\"phase\":\"actual-native-materialization\",\"cycle\":" << cycle <<
                    ",\"kind\":\"" << kind << "\",\"cpp_allocations\":" << count << ",\"cpp_requested_bytes\":" << requested <<
                    ",\"child_hwnds\":" << attached << "}\n";
                if (!std::strcmp(kind, "input")) {
                    const auto clear_before = allocations.load(), clear_bytes = requested_bytes.load();
                    require(window.focus(*model, false), "Actual clear factory focuses only its owned input");
                    settle();
                    const auto clear_count = allocations.load() - clear_before, clear_requested = requested_bytes.load() - clear_bytes;
                    require(descendants(window.native_window()) == attached + 1, "Actual focus-driven factory adds exactly one native clear action");
                    std::cout << "{\"phase\":\"actual-native-materialization\",\"cycle\":" << cycle <<
                        ",\"kind\":\"clear\",\"cpp_allocations\":" << clear_count << ",\"cpp_requested_bytes\":" <<
                        clear_requested << ",\"child_hwnds\":1}\n";
                }
                window.replace_content(*host, {});
                settle();
                require(descendants(window.native_window()) == 0 && Drawing::live_targets() == 1,
                    "Actual native factory retires every child and preserves exactly its shared renderer");
            }
            const auto after = resources();
            if (!cycle) warmed = after;
            else require(after.gdi == warmed.gdi && after.user == warmed.user, "Repeated actual native factories have zero warmed GDI/USER growth");
            std::cout << "{\"phase\":\"materialization-cycle-resources\",\"cycle\":" << cycle;
            report_resources("warmed", warmed); report_resources("current", after);
            std::cout << "}\n" << std::flush;
        }
        completed = true; window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << '\n';
    require(!result && completed && Drawing::live_targets() == 0, "Actual native storage/factory workload retires its renderer and owner");
}
void embedding_work() {
    WindowOptions options;
    options.title = L"XUI actual embedded native editor seam";
    options.size = {520, 400}; options.show_activated = false;
    options.visual_style = VisualStyle::winui; options.theme = ThemeMode::light;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({24, 24, 24, 24}); root->set_spacing(16);
    auto number = std::make_shared<NumericInput>(L"Native embedding number");
    number->set_locale(std::locale::classic()); number->set_value(42);
    auto combo = std::make_shared<ComboBox>(L"Native embedding combo", true);
    combo->set_items({{1, L"Native embedding first choice"}, {2, L"Native embedding second choice"}}, 1);
    const std::array<std::shared_ptr<Control>, 2> owners{number, combo};
    const std::array<std::shared_ptr<TextInput>, 2> models{number->editor(), combo->editor()};
    for (const auto& owner : owners) { owner->set_fixed_size({360, 100}); root->add(owner); }
    auto away = std::make_shared<Button>(L"Owned embedding focus destination"); root->add(away);
    unsigned callbacks{};
    number->on_change([&](double) { ++callbacks; });
    combo->on_change([&](std::uint64_t) { ++callbacks; });
    combo->on_edit([&](const std::wstring&) { ++callbacks; });
    window.set_content(root);
    bool completed{};
    window.post([&] {
        const auto hwnd = window.native_window();
        const auto settle = [&] {
            MSG message{}; unsigned count{};
            while (PeekMessageW(&message, hwnd, update_message, update_message, PM_REMOVE)) {
                require(++count <= 32, "Embedded native presentation reaches a bounded settled state");
                DispatchMessageW(&message);
            }
            SendMessageW(hwnd, update_message, 0, 0);
            require(window.state() == WindowState::open, "Embedded presentation retains its live window");
        };
        settle();
        const std::array<std::wstring, 2> text{models[0]->text(), models[1]->text()};
        struct Editors { std::array<HWND, 2> windows{}; const std::array<std::wstring, 2>& text; unsigned count{}; } editors{{}, text};
        EnumChildWindows(hwnd, [](HWND native, LPARAM data) -> BOOL {
            auto& editors = *reinterpret_cast<Editors*>(data);
            wchar_t name[128]{}; GetClassNameW(native, name, 128);
            if (_wcsicmp(name, L"EDIT")) return TRUE;
            ++editors.count;
            std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(native)) + 1, L'\0');
            value.resize(GetWindowTextW(native, value.data(), static_cast<int>(value.size())));
            for (unsigned index = 0; index != 2; ++index) if (value == editors.text[index]) editors.windows[index] = native;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&editors));
        require(editors.count == 2 && editors.windows[0] && editors.windows[1] && editors.windows[0] != editors.windows[1],
            "Actual NumericInput and editable ComboBox each own exactly one distinct native EDIT");
        const auto children = descendants(hwnd);
        const auto peers = SendMessageW(hwnd, metrics_message, 14, 0);
        std::array<LRESULT, 2> undo;
        for (unsigned index = 0; index != 2; ++index) {
            SendMessageW(editors.windows[index], EM_SETSEL, 1, 2);
            undo[index] = SendMessageW(editors.windows[index], EM_CANUNDO, 0, 0);
        }
        unsigned checks{};
        for (const auto mode : {ThemeMode::light, ThemeMode::dark}) for (const auto size : {17.0f, 19.0f}) {
            window.set_theme(mode);
            PartStyleValues field;
            field.background = ThemeColor{0x123456, 0x654321};
            field.foreground = ThemeColor{0x246813, 0x315724};
            field.padding = Insets{12, 7, 16, 8}; field.border_thickness = Insets{2, 3, 4, 5};
            field.corner_radius = 16.0f;
            PartStyleValues font; font.font_family = make_style_font_family("Consolas");
            font.font_size = size; font.font_weight = 600; font.font_style = StyleFontStyle::italic;
            for (unsigned index = 0; index != 2; ++index) {
                owners[index]->set_control_style_values(StylePart::field, field);
                models[index]->set_control_style_values(StylePart::text, font);
            }
            settle();
            for (unsigned index = 0; index != 2; ++index) {
                const auto native = editors.windows[index];
                DWORD process{};
                require(GetWindowThreadProcessId(native, &process) && process == GetCurrentProcessId() && GetParent(native) != hwnd,
                    "Embedded EDIT remains inside its actual owned composite HWND");
                DWORD preserved_begin{}, preserved_end{};
                SendMessageW(native, EM_GETSEL, reinterpret_cast<WPARAM>(&preserved_begin), reinterpret_cast<LPARAM>(&preserved_end));
                require(preserved_begin == 1 && preserved_end == 2,
                    "Actual embedding theme/font/inset updates preserve the native selection before focus changes");
                require(window.focus(*away) && window.focus(*models[index], false), "Focus reaches actual retained embedded editor");
                settle();
                require(GetFocus() == native && models[index]->focused(), "Native and model focus agree for both embeddings");
                DWORD begin{}, end{};
                SendMessageW(native, EM_GETSEL, reinterpret_cast<WPARAM>(&begin), reinterpret_cast<LPARAM>(&end));
                LOGFONTW actual_font{};
                const auto font_handle = reinterpret_cast<HFONT>(SendMessageW(native, WM_GETFONT, 0, 0));
                require(GetObjectW(font_handle, sizeof(actual_font), &actual_font) != 0 &&
                    std::wstring_view(actual_font.lfFaceName) == L"Consolas" && actual_font.lfWeight == 600 && actual_font.lfItalic,
                    "Embedded native fields consume actual authored font family/weight/style");
                const auto dpi = GetDpiForWindow(native);
                require(actual_font.lfHeight == -static_cast<int>(std::lround(size * dpi / 96.0f)),
                    "Embedded native field font height exactly follows authored size and its actual DPI");
                const auto dc = GetDC(native); require(dc != nullptr, "Acquire only the owned embedded field color context");
                const auto brush = reinterpret_cast<HBRUSH>(SendMessageW(hwnd, WM_CTLCOLOREDIT,
                    reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(native)));
                const auto palette = Palette::system(mode, VisualStyle::winui);
                const auto expected_background = palette.high_contrast ? palette.field : D2D1::ColorF(mode == ThemeMode::light ? 0x123456 : 0x654321);
                const auto expected_foreground = palette.high_contrast ? palette.text : D2D1::ColorF(mode == ThemeMode::light ? 0x246813 : 0x315724);
                const auto color = [](D2D1_COLOR_F value) { return RGB(BYTE(std::lround(value.r * 255)), BYTE(std::lround(value.g * 255)), BYTE(std::lround(value.b * 255))); };
                const bool colors = brush && GetBkColor(dc) == color(expected_background) && GetTextColor(dc) == color(expected_foreground);
                TEXTMETRICW metrics{};
                const auto previous = SelectObject(dc, font_handle);
                const bool measured_font = previous && previous != HGDI_ERROR && GetTextMetricsW(dc, &metrics);
                if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
                ReleaseDC(native, dc);
                require(colors && measured_font, "Native embedded colors consume enclosing field style without pixel capture");
                auto bounds = models[index]->bounds();
                bounds.x -= owners[index]->bounds().x; bounds.y -= owners[index]->bounds().y;
                const auto insets = models[index]->field_insets({index ? 12.0f : 11.0f, 6, 1, 7});
                const auto scale = dpi / 96.0f;
                const auto height = static_cast<int>(std::lround(std::max(0.0f, bounds.height - insets.top - insets.bottom) * scale));
                const auto line = std::min<int>(metrics.tmHeight, height);
                const LONG left = static_cast<LONG>(std::lround((bounds.x + insets.left) * scale));
                const LONG top = static_cast<LONG>(std::lround((bounds.y + insets.top) * scale)) + (height - line) / 2;
                const RECT expected{left, top, left + static_cast<LONG>(std::lround(
                    std::max(0.0f, bounds.width - insets.left - insets.right) * scale)), top + line};
                RECT actual{};
                require(GetWindowRect(native, &actual) != FALSE, "Read actual embedded native rectangle");
                MapWindowPoints(nullptr, GetParent(native), reinterpret_cast<POINT*>(&actual), 2);
                require(EqualRect(&actual, &expected) && !IsRectEmpty(&actual),
                    "Embedded native line geometry follows concrete parent-relative bounds, insets, font metrics and reserved adornment space");
                std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(native)) + 1, L'\0');
                value.resize(GetWindowTextW(native, value.data(), static_cast<int>(value.size())));
                std::cout << "{\"phase\":\"embedding-retained-state\",\"index\":" << index <<
                    ",\"native_text_matches\":" << (value == text[index]) <<
                    ",\"model_text_matches\":" << (models[index]->text() == text[index]) <<
                    ",\"selection_begin\":" << begin << ",\"selection_end\":" << end <<
                    ",\"undo_before\":" << undo[index] << ",\"undo_after\":" << SendMessageW(native, EM_CANUNDO, 0, 0) <<
                    ",\"children_before\":" << children << ",\"children_after\":" << descendants(hwnd) <<
                    ",\"peers_before\":" << peers << ",\"peers_after\":" << SendMessageW(hwnd, metrics_message, 14, 0) <<
                    ",\"targets\":" << Drawing::live_targets() << ",\"callbacks\":" << callbacks <<
                    ",\"numeric_value\":" << number->value() << ",\"combo_selection\":" << combo->selected().value_or(0) << "}\n" << std::flush;
                require(value == text[index] && models[index]->text() == text[index] &&
                    begin == (index ? 1u : 0u) && end == (index ? 2u : static_cast<DWORD>(text[index].size())) &&
                    SendMessageW(native, EM_CANUNDO, 0, 0) == undo[index] && descendants(hwnd) == children &&
                    SendMessageW(hwnd, metrics_message, 14, 0) == peers && Drawing::live_targets() == 1 &&
                    callbacks == 0 && number->value() == 42 && combo->selected() == 1,
                    "Embedding presentation preserves actual HWNDs, native/model text, NumericInput select-all versus ComboBox retained focus selection, undo, one renderer and enclosing values without extra clear peers or callbacks");
                SendMessageW(native, EM_SETSEL, 1, 2);
                ++checks;
            }
        }
        require(checks == 8, "Both real embeddings pass every light/dark and authored-font presentation");
        std::cout << "{\"phase\":\"actual-native-embedding\",\"checks\":" << checks <<
            ",\"native_editors\":2,\"child_hwnds\":" << children << ",\"peers\":" << peers <<
            ",\"callbacks\":0,\"targets\":1}\n" << std::flush;
        completed = true; window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << '\n';
    require(!result && completed && Drawing::live_targets() == 0, "Actual native embedding fixture retires its owner and shared renderer");
}
LRESULT CALLBACK observe(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto& writes = *reinterpret_cast<Writes*>(data);
    if (diagnostic_uia && (message == WM_WINDOWPOSCHANGED || message == update_message)) {
        wchar_t name[128]{}; GetClassNameW(hwnd, name, 128);
        std::wcout << L"Observed native event hwnd=" << hwnd << L" class=" << name << L" message=" << message;
        if (message == WM_WINDOWPOSCHANGED) {
            const auto& position = *reinterpret_cast<const WINDOWPOS*>(lp);
            std::wcout << L" flags=" << position.flags << L" x=" << position.x << L" y=" << position.y <<
                L" width=" << position.cx << L" height=" << position.cy;
        }
        std::wcout << L'\n' << std::flush;
    }
    if (message == WM_SETTEXT || message == EM_REPLACESEL || message == EM_STREAMIN || message == EM_SETTEXTEX) ++writes.text;
    if (message == WM_SETFONT || message == EM_SETCHARFORMAT) ++writes.fonts;
    if (message == EM_SETSEL || message == EM_EXSETSEL) ++writes.selections;
    if (message == EM_SETRECT || message == EM_SETRECTNP || message == EM_SETMARGINS) ++writes.insets;
    if (message == WM_COMMAND || message == WM_NOTIFY) ++writes.notifications;
    if (message == DTM_SETSYSTEMTIME || message == MCM_SETCURSEL) ++writes.date_values;
    if (message == DTM_SETRANGE || message == MCM_SETRANGE) ++writes.date_ranges;
    if (message == WM_NCDESTROY) ++writes.destroyed;
    if (message == WM_WINDOWPOSCHANGED) {
        ++writes.placements;
        const auto flags = reinterpret_cast<const WINDOWPOS*>(lp)->flags;
        if (!(flags & SWP_NOMOVE) || !(flags & SWP_NOSIZE)) ++writes.geometry_notifications;
    }
    if (message == update_message) ++writes.updates;
    return DefSubclassProc(hwnd, message, wp, lp);
}
struct Observers {
    Writes writes;
    std::vector<HWND> windows;
    void add(HWND hwnd) {
        require(SetWindowSubclass(hwnd, observe, 901, reinterpret_cast<DWORD_PTR>(&writes)) != FALSE,
            "Observe real owned native work");
        windows.push_back(hwnd);
    }
    void detach() {
        for (auto hwnd : windows) require(RemoveWindowSubclass(hwnd, observe, 901) != FALSE, "Detach native work observer");
        windows.clear();
    }
    ~Observers() { for (auto hwnd : windows) if (IsWindow(hwnd)) RemoveWindowSubclass(hwnd, observe, 901); }
};
struct Fields {
    Window window;
    std::shared_ptr<TextInput> input = std::make_shared<TextInput>(L"Editor metadata caption longer than small string storage");
    std::shared_ptr<MultilineText> plain = std::make_shared<MultilineText>(L"Plain native document");
    std::shared_ptr<RichText> rich = std::make_shared<RichText>(L"Rich native document");
    std::shared_ptr<PasswordInput> password = std::make_shared<PasswordInput>(L"Password metadata name longer than small string storage");
    std::array<std::shared_ptr<DateTimePicker>, 3> dates{
        std::make_shared<DateTimePicker>(L"Date metadata name longer than small string storage"),
        std::make_shared<DateTimePicker>(L"Time metadata name longer than small string storage", DateTimePresentation::time),
        std::make_shared<DateTimePicker>(L"Calendar metadata name longer than small string storage", DateTimePresentation::calendar)};
    std::shared_ptr<ScrollView> scroll;
    std::shared_ptr<ContentHost> retirement_host;
    std::array<HWND, 7> native{};
    Work counters;
    static constexpr std::array<const wchar_t*, 2> input_text{L"input A \U0001f600", L"input B extended \U0001f600"};
    static constexpr std::array<const wchar_t*, 2> plain_text{L"plain A \U0001f600\rline", L"plain B extended \U0001f600\rline"};
    static constexpr std::array<const wchar_t*, 2> rich_text{L"rich A \U0001f600", L"rich B extended \U0001f600"};
    static constexpr std::array<const wchar_t*, 2> password_text{L"owned-fixture-A", L"owned-fixture-B-extended"};
    static DateTimeValue minimum(unsigned parity) { return {2025, 1, 1 + static_cast<int>(parity)}; }
    static DateTimeValue maximum(unsigned parity) { return {2030, 12, 31 - static_cast<int>(parity)}; }
    explicit Fields(const wchar_t* title, bool replaceable = false) : window([&] {
        WindowOptions options; options.title = title; options.size = {660, 520}; options.show_activated = false;
        options.visual_style = VisualStyle::winui; options.theme = ThemeMode::light; return options;
    }()) {
        window.set_presentation("Segoe UI", 14, false, false);
        auto content = std::make_shared<Stack>(Axis::vertical);
        input->set_preferred_size({520, 72}); plain->set_preferred_size({520, 120});
        rich->set_preferred_size({520, 100}); password->set_preferred_size({520, 64});
        input->set_text(input_text[0]); plain->set_text(plain_text[0]);
        rich->set_runs({{rich_text[0], true}}); password->set_password(password_text[0]);
        unsigned id{};
        for (const auto& control : std::array<std::shared_ptr<Control>, 7>{input, plain, rich, password, dates[0], dates[1], dates[2]}) {
            control->set_automation_id(L"native-editor-automation-identity-" + std::to_wstring(++id));
            control->set_help_text(L"Native editor metadata help text with independent retained ownership");
            content->add(control);
        }
        dates[0]->set_preferred_size({520, 48}); dates[1]->set_preferred_size({520, 48});
        dates[2]->set_preferred_size({520, 220});
        for (auto& date : dates) {
            date->set_value({2026, 1, 1, 10, 11, 12});
            date->set_range(minimum(0), maximum(0));
        }
        scroll = std::make_shared<ScrollView>(content);
        auto root = std::make_shared<Stack>(Axis::vertical);
        if (replaceable) {
            retirement_host = std::make_shared<ContentHost>(scroll);
            root->add(retirement_host, 1);
        } else root->add(scroll, 1);
        window.set_content(root);
        input->on_change([this](const auto&) { ++counters.text_callbacks; });
        plain->on_change([this](const auto&) { ++counters.text_callbacks; });
        rich->on_change([this](const auto&) { ++counters.text_callbacks; });
        password->on_change([this] { ++counters.password_callbacks; });
        for (auto& date : dates) date->on_change([this](DateTimeValue) { ++counters.date_callbacks; });
    }
    ~Fields() {
        input->on_change({}); plain->on_change({}); rich->on_change({}); password->on_change({});
        for (auto& date : dates) date->on_change({});
    }
    void flush() {
        MSG message{}; unsigned pending{};
        while (PeekMessageW(&message, window.native_window(), update_message, update_message, PM_REMOVE))
            if (++pending > 1) throw std::runtime_error("Native editor updates remain coalesced: prior updates=" +
                std::to_string(counters.updates) + " queued=" + std::to_string(pending));
        SendMessageW(window.native_window(), update_message, 0, 0);
        ++counters.updates;
        require(window.state() == WindowState::open, "Native phase retains its live owner");
    }
    void settle_construction() {
        MSG message{};
        unsigned count{};
        while (PeekMessageW(&message, window.native_window(), update_message, update_message, PM_REMOVE)) {
            require(++count <= 32, "Owned native construction reaches a bounded settled state");
            DispatchMessageW(&message);
        }
        std::cout << "{\"phase\":\"construction-settle\",\"dispatched_updates\":" << count << "}\n";
        require(window.state() == WindowState::open, "Native construction retains its live owner");
    }
    std::array<HWND, 7> current_native() const {
        struct Search { std::array<HWND, 7> result{}; bool unique{true}; } search;
        EnumChildWindows(window.native_window(), [](HWND hwnd, LPARAM context) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(context);
            wchar_t cls[128]{};
            GetClassNameW(hwnd, cls, 128);
            unsigned field = 7;
            const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            if (!_wcsicmp(cls, L"EDIT")) field = style & ES_PASSWORD ? 3 : 0;
            else if (!_wcsicmp(cls, MSFTEDIT_CLASS)) {
                wchar_t prefix[8]{};
                GetWindowTextW(hwnd, prefix, 8);
                if (std::wstring_view(prefix).starts_with(L"plain ")) field = 1;
                else if (std::wstring_view(prefix).starts_with(L"rich ")) field = 2;
                else { search.unique = false; return FALSE; }
            } else if (!_wcsicmp(cls, DATETIMEPICK_CLASSW)) field = (style & DTS_TIMEFORMAT) == DTS_TIMEFORMAT ? 5 : 4;
            else if (!_wcsicmp(cls, MONTHCAL_CLASSW)) field = 6;
            if (field != 7) {
                if (search.result[field]) { search.unique = false; return FALSE; }
                search.result[field] = hwnd;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        require(search.unique && std::all_of(search.result.begin(), search.result.end(), [](HWND value) { return value != nullptr; }),
            "Resolve each actual owned HWND by native class/style/authored document identity, never mutable z-order");
        return search.result;
    }
    void attach() {
        flush();
        const auto root = window.native_window();
        native = current_native();
        require(window.focus(*input), "Focus the owned native input");
        settle_construction(); UpdateWindow(root);
        if (GetFocus() != native[0] || scroll->maximum_offset() <= 0) {
            wchar_t focus_class[128]{};
            GetClassNameW(GetFocus(), focus_class, 128);
            std::wcerr << L"Native focused class=" << focus_class << L'\n';
            std::cerr << "Native fixture focus=" << GetFocus() << " input=" << native[0] <<
                " maximum_offset=" << scroll->maximum_offset() << " extent=" << scroll->extent() <<
                " viewport_height=" << scroll->viewport().height << " input_visible=" << IsWindowVisible(native[0]) << '\n';
        }
        require(GetFocus() == native[0] && scroll->maximum_offset() > 0, "Fixture has real native focus and retained scroll work");
        for (const auto hwnd : native) SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
        settle_construction(); UpdateWindow(root);
    }
    Work delta(const Work& before) const {
        return {counters.updates - before.updates, counters.text_callbacks - before.text_callbacks,
            counters.password_callbacks - before.password_callbacks, counters.date_callbacks - before.date_callbacks, 1, 0};
    }
    std::size_t length(unsigned field) const {
        if (field == 1 || field == 2) {
            GETTEXTLENGTHEX request{GTL_PRECISE | GTL_NUMCHARS, 1200};
            return static_cast<std::size_t>(SendMessageW(native[field], EM_GETTEXTLENGTHEX,
                reinterpret_cast<WPARAM>(&request), 0));
        }
        return static_cast<std::size_t>(GetWindowTextLengthW(native[field]));
    }
    std::wstring native_text(unsigned field) const {
        const auto size = length(field);
        require(size < 1024, "Read only bounded owned fixture UTF-16 text");
        std::wstring text(size + 1, L'\0');
        if (field == 1 || field == 2) {
            GETTEXTEX request{static_cast<DWORD>(text.size() * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr};
            text.resize(static_cast<std::size_t>(SendMessageW(native[field], EM_GETTEXTEX,
                reinterpret_cast<WPARAM>(&request), reinterpret_cast<LPARAM>(text.data()))));
        } else text.resize(GetWindowTextW(native[field], text.data(), static_cast<int>(text.size())));
        require(text.size() == size, "Native UTF-16 readback has its exact reported length");
        return text;
    }
    std::uint64_t text_check(unsigned parity) const {
        std::uint64_t checksum{1};
        for (unsigned field = 0; field != 4; ++field) {
            const auto expected = field == 0 ? input_text[parity] : field == 1 ? plain_text[parity] :
                field == 2 ? rich_text[parity] : password_text[parity];
            auto text = native_text(field);
            const bool matches = text == expected;
            if (field == 3) {
                bool model_matches{};
                password->with_password([&](std::wstring_view secret) { model_matches = secret == expected && secret == text; });
                volatile wchar_t* secret = text.data();
                for (std::size_t at = 0; at != text.size(); ++at) secret[at] = 0;
                require(model_matches, "Owned secret model and native text match exactly without logging secret contents");
                checksum = mix(checksum, text.size());
            } else {
                require((field == 0 ? input->text() : field == 1 ? plain->text() : rich->text()) == expected,
                    "Every authored model matches the exact committed UTF-16 operation");
                for (const auto value : text) checksum = mix(checksum, value);
            }
            require(matches, "Every actual native UTF-16 code unit matches its authored operation");
        }
        return checksum;
    }
    std::uint64_t date_check(unsigned parity, bool notification) const {
        const auto range_parity = notification ? 0 : parity;
        const auto from_native = [](const SYSTEMTIME& value) {
            return DateTimeValue{value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute, value.wSecond};
        };
        const auto same_date = [](const SYSTEMTIME& actual, DateTimeValue expected) {
            return actual.wYear == expected.year && actual.wMonth == expected.month && actual.wDay == expected.day;
        };
        std::uint64_t checksum{1};
        for (unsigned field = 0; field != 3; ++field) {
            const bool calendar = field == 2;
            const DateTimeValue expected{2026, 1, notification && field == 1 ? 1 : 1 + static_cast<int>(parity),
                notification && field == 1 ? 10 + static_cast<int>(parity) : 10, 11, 12};
            require(dates[field]->value() == expected && dates[field]->minimum() == minimum(range_parity) &&
                dates[field]->maximum() == maximum(range_parity), "All date/time/calendar models preserve their exact full value, clock and range semantics");
            SYSTEMTIME actual{}, range[2]{};
            const auto valid = SendMessageW(native[field + 4], calendar ? MCM_GETCURSEL : DTM_GETSYSTEMTIME,
                0, reinterpret_cast<LPARAM>(&actual));
            const auto flags = SendMessageW(native[field + 4], calendar ? MCM_GETRANGE : DTM_GETRANGE,
                0, reinterpret_cast<LPARAM>(range));
            const bool ranges = calendar ? same_date(range[0], minimum(range_parity)) && same_date(range[1], maximum(range_parity)) :
                from_native(range[0]) == minimum(range_parity) && from_native(range[1]) == maximum(range_parity);
            require((calendar ? valid != 0 : valid == GDT_VALID) && flags == (GDTR_MIN | GDTR_MAX) &&
                ranges,
                "Every real date/time/calendar HWND has its exact bounded native range");
            const DateTimeValue native_expected{2026, 1, 1 + static_cast<int>(parity),
                notification ? 10 + static_cast<int>(parity) : 10, 11, 12};
            require(calendar ? actual.wYear == native_expected.year && actual.wMonth == native_expected.month &&
                actual.wDay == native_expected.day : from_native(actual) == native_expected,
                "Each real HWND returns its presentation-aware exact native selected date/time");
            for (const auto part : {expected.year, expected.month, expected.day, expected.hour, expected.minute, expected.second,
                int(actual.wYear), int(actual.wMonth), int(actual.wDay), int(range[0].wDay), int(range[1].wDay)})
                checksum = mix(checksum, part);
        }
        return checksum;
    }
};
void editor_work() {
    const auto before_construction = resources();
    Fields fields(L"XUI matched native editor workload");
    Observers observers;
    unsigned native_count{};
    bool completed{};
    fields.window.post([&] {
        fields.attach();
        const auto root = fields.window.native_window();
        const auto native = fields.native;
        native_count = descendants(root);
        const auto peers = SendMessageW(root, metrics_message, 14, 0);
        require(peers == 9 && SendMessageW(root, metrics_message, 11, 0) == 1,
            "Eight retained control peers plus one lazy clear action share one renderer");
        observers.add(root);
        EnumChildWindows(root, [](HWND hwnd, LPARAM context) -> BOOL {
            reinterpret_cast<Observers*>(context)->add(hwnd); return TRUE;
        }, reinterpret_cast<LPARAM>(&observers));
        const auto caption = child(root, L"STATIC");
        const auto run = [&](const char* name, unsigned count, auto action, std::array<std::uint64_t, 3> callbacks,
            bool no_writes = false, unsigned updates_per_iteration = 1) {
            const auto before = fields.counters;
            const auto writes = observers.writes;
            const auto before_resources = resources();
            std::uint64_t checksum{1};
            const auto timing = measure(GetCurrentThread(), count, [&](unsigned index) {
                checksum = mix(checksum, action(index));
            });
            auto work = fields.delta(before); work.checksum = checksum;
            const auto native_work = observers.writes - writes;
            require(work.updates == count * updates_per_iteration && work.text_callbacks == callbacks[0] &&
                work.password_callbacks == callbacks[1] && work.date_callbacks == callbacks[2] && checksum,
                "Every native phase performs its exact updates and callback work");
            if (no_writes) require(native_work.editor_empty(), "Unchanged retained editors receive zero text/font/selection/inset writes");
            require(fields.current_native() == native && child(root, L"STATIC") == caption &&
                descendants(root) == native_count && observers.writes.destroyed == 0 &&
                std::all_of(observers.windows.begin(), observers.windows.end(), [](HWND hwnd) { return IsWindow(hwnd) != FALSE; }) &&
                SendMessageW(root, metrics_message, 14, 0) == peers && SendMessageW(root, metrics_message, 11, 0) == 1,
                "No phase recreates native fields, peers or render targets");
            if (!std::strcmp(name, "editor-source-sync")) require(native_work.date_values == count * 3 &&
                native_work.date_ranges == count * 3, "Every source update reaches all three real native date values and ranges");
            if (!std::strcmp(name, "native-edit-notifications")) require(native_work.date_values == count * 3 &&
                native_work.date_ranges == 0, "Every notification phase performs exactly three native date writes without programmatic range churn");
            const auto after_resources = resources();
            require(before_resources.gdi == after_resources.gdi && before_resources.user == after_resources.user,
                "Steady editor work has zero GDI/USER growth");
            report(name, count, timing, work, native_work, before_resources, after_resources);
        };
        run("editor-source-sync", 2048, [&](unsigned index) {
            const auto parity = (index + 1) % 2;
            fields.input->set_text(Fields::input_text[parity]);
            fields.plain->set_text(Fields::plain_text[parity]);
            fields.rich->set_runs({{Fields::rich_text[parity], true}});
            fields.password->set_password(Fields::password_text[parity]);
            for (auto& date : fields.dates) {
                date->set_range(Fields::minimum(parity), Fields::maximum(parity));
                date->set_value({2026, 1, 1 + static_cast<int>(parity), 10, 11, 12});
            }
            fields.flush();
            return mix(fields.text_check(parity), fields.date_check(parity, false));
        }, {0, 0, 0});
        run("native-edit-notifications", 4096, [&](unsigned index) {
            const auto parity = (index + 1) % 2;
            for (unsigned field = 0; field != 4; ++field) {
                const auto value = field == 0 ? Fields::input_text[parity] : field == 1 ? Fields::plain_text[parity] :
                    field == 2 ? Fields::rich_text[parity] : Fields::password_text[parity];
                SendMessageW(native[field], EM_SETSEL, 0, -1);
                SendMessageW(native[field], EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value));
            }
            for (unsigned field = 0; field != 3; ++field) {
                SYSTEMTIME value{}; value.wYear = 2026; value.wMonth = 1; value.wDay = static_cast<WORD>(1 + parity);
                value.wHour = static_cast<WORD>(10 + parity); value.wMinute = 11; value.wSecond = 12;
                const auto hwnd = native[field + 4];
                if (field == 2) {
                    SendMessageW(hwnd, MCM_SETCURSEL, 0, reinterpret_cast<LPARAM>(&value));
                    NMSELCHANGE change{}; change.nmhdr = {hwnd, 0, MCN_SELCHANGE}; change.stSelStart = value; change.stSelEnd = value;
                    SendMessageW(GetParent(hwnd), WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&change));
                } else {
                    SendMessageW(hwnd, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&value));
                    NMDATETIMECHANGE change{}; change.nmhdr = {hwnd, 0, DTN_DATETIMECHANGE}; change.dwFlags = GDT_VALID; change.st = value;
                    SendMessageW(GetParent(hwnd), WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&change));
                }
            }
            fields.flush();
            require(fields.input->text() == Fields::input_text[parity] && fields.plain->text() == Fields::plain_text[parity] &&
                fields.rich->text() == Fields::rich_text[parity], "Actual native notifications publish exact UTF-16 source");
            return mix(fields.text_check(parity), fields.date_check(parity, true));
        }, {4096 * 3, 4096, 4096 * 3});
        run("synthetic-native-composition", 1024, [&](unsigned index) {
            const auto parity = (index + 1) % 2, previous = index % 2;
            for (unsigned field = 0; field != 4; ++field) {
                SendMessageW(native[field], WM_IME_STARTCOMPOSITION, 0, 0);
                SendMessageW(native[field], EM_SETSEL, 0, -1);
                const auto text = field == 0 ? Fields::input_text[parity] : field == 1 ? Fields::plain_text[parity] :
                    field == 2 ? Fields::rich_text[parity] : Fields::password_text[parity];
                SendMessageW(native[field], EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));
            }
            fields.input->invalidate(Invalidation::paint);
            fields.flush();
            require(fields.input->text() == Fields::input_text[previous] && fields.plain->text() == Fields::plain_text[previous] &&
                fields.rich->text() == Fields::rich_text[previous], "Synthetic native composition does not publish intermediate model text");
            bool secret_unchanged{};
            fields.password->with_password([&](std::wstring_view value) { secret_unchanged = value == Fields::password_text[previous]; });
            require(secret_unchanged, "Synthetic composition does not publish an intermediate secret");
            for (unsigned field = 0; field != 4; ++field) {
                const auto expected = field == 0 ? Fields::input_text[parity] : field == 1 ? Fields::plain_text[parity] :
                    field == 2 ? Fields::rich_text[parity] : Fields::password_text[parity];
                auto text = fields.native_text(field);
                const bool unchanged = text == expected;
                if (field == 3) { volatile wchar_t* value = text.data(); for (std::size_t at = 0; at != text.size(); ++at) value[at] = 0; }
                require(unchanged, "Host phases preserve every real composing native UTF-16 value");
            }
            for (unsigned field = 0; field != 4; ++field) SendMessageW(native[field], WM_IME_ENDCOMPOSITION, 0, 0);
            fields.flush();
            return fields.text_check(parity);
        }, {1024 * 3, 1024, 0}, false, 2);
        run("native-selection-caret", 8192, [&](unsigned index) {
            const auto start = index % 2 ? 1u : 0u, end = index % 2 ? 4u : 5u;
            fields.input->set_selection({start, end}); fields.plain->set_selection({start, end});
            fields.flush();
            DWORD actual_start{}, actual_end{};
            SendMessageW(native[0], EM_GETSEL, reinterpret_cast<WPARAM>(&actual_start), reinterpret_cast<LPARAM>(&actual_end));
            CHARRANGE selected{}; SendMessageW(native[1], EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
            GUITHREADINFO info{sizeof(info)}; POINT caret{};
            require(actual_start == start && actual_end == end && selected.cpMin == static_cast<LONG>(start) && selected.cpMax == static_cast<LONG>(end) &&
                GetGUIThreadInfo(GetCurrentThreadId(), &info) && info.hwndFocus == native[0] &&
                info.hwndCaret == native[0] && GetCaretPos(&caret), "Selection phase reads exact native ranges and the owned caret");
            return mix(mix(start, end), mix(static_cast<unsigned>(caret.x), static_cast<unsigned>(caret.y)));
        }, {0, 0, 0});
        const auto layouts = SendMessageW(root, metrics_message, 2, 0);
        const auto scroll_positions = static_cast<unsigned>(fields.scroll->maximum_offset()) + 1;
        require(scroll_positions > 41, "Fixture has enough real unclamped positions for both exact scroll/paint strides");
        run("retained-editor-scroll", 16384, [&](unsigned index) {
            const auto desired = static_cast<float>(((index + 1) * 37u) % scroll_positions);
            const auto before = fields.scroll->offset();
            fields.scroll->set_offset(desired);
            require(fields.scroll->offset() == desired && desired != before, "Every retained scroll operation changes the exact real unclamped offset");
            fields.flush();
            return static_cast<unsigned>(fields.scroll->offset());
        }, {0, 0, 0}, true);
        require(SendMessageW(root, metrics_message, 2, 0) == layouts, "Retained editor scroll does not arrange the window");
        const auto paints = SendMessageW(root, metrics_message, 0, 0);
        run("native-editor-paint", 128, [&](unsigned index) {
            const auto desired = static_cast<float>(((index + 1) * 41u) % scroll_positions);
            const auto before = fields.scroll->offset();
            fields.scroll->set_offset(desired);
            require(fields.scroll->offset() == desired && desired != before, "Every paint operation changes the exact real unclamped offset");
            fields.flush(); UpdateWindow(root);
            return static_cast<unsigned>(fields.scroll->offset());
        }, {0, 0, 0}, true);
        const auto actual_paints = SendMessageW(root, metrics_message, 0, 0) - paints;
        if (actual_paints != 128) std::cerr << "Actual native editor paint count=" << actual_paints << '\n';
        require(actual_paints == 128, "Paint phase presents every exact requested frame");
        require(SendMessageW(root, metrics_message, 33, 0) == 0, "Explicit native editor work creates no animation timer");
        std::cout << "{\"phase\":\"editor-resources\",\"peers\":" << peers << ",\"child_hwnds\":" << native_count
            << ",\"targets\":1,\"gdi\":" << xui::test::gui_resources(GR_GDIOBJECTS)
            << ",\"user\":" << xui::test::gui_resources(GR_USEROBJECTS) << "}\n";
        completed = true; fields.window.close();
    });
    const auto result = Application::run(fields.window);
    if (result) std::wcerr << fields.window.error() << '\n';
    require(!result && completed, "Native editor workload completes every required phase");
    require(Drawing::live_targets() == 0 && observers.writes.destroyed == native_count + 1,
        "Native editor workload observes exactly every real child and root destruction");
    std::cout << "{\"phase\":\"editor-retirement\",\"native_destructions\":" << observers.writes.destroyed;
    report_resources("before_construction", before_construction); report_resources("after_retirement", resources());
    std::cout << "}\n" << std::flush;
}
std::vector<int> runtime_id(IUIAutomationElement* element) {
    SAFEARRAY* values{};
    success(element->GetRuntimeId(&values), "Read actual native UIA identity");
    require(values != nullptr && SafeArrayGetDim(values) == 1, "Native UIA identity is a nonempty one-dimensional array");
    struct Release { SAFEARRAY* values; ~Release() { SafeArrayDestroy(values); } } release{values};
    LONG first{}, last{};
    success(SafeArrayGetLBound(values, 1, &first), "Read native identity start");
    success(SafeArrayGetUBound(values, 1, &last), "Read native identity end");
    require(last >= first && last - first < 64, "Actual native UIA identity has a bounded positive length");
    std::vector<int> result;
    for (auto at = first; at <= last; ++at) {
        int value{}; success(SafeArrayGetElement(values, &at, &value), "Read native UIA identity component");
        result.push_back(value);
    }
    return result;
}
struct Variant {
    VARIANT value{};
    ~Variant() { VariantClear(&value); }
};
struct QueryField {
    HWND window{};
    ComPtr<IUIAutomationElement> element;
    std::wstring name, id, help;
    RECT bounds{};
    bool focused{};
    std::vector<int> identity;
};
ComPtr<IUIAutomationElement> clear_semantic(IUIAutomationElement* root, IUIAutomationCondition* condition) {
    ComPtr<IUIAutomationElementArray> matches;
    std::cout << "Begin exact clear FindAll\n" << std::flush;
    success(root->FindAll(TreeScope_Subtree, condition, &matches), "Resolve the unique actual clear Invoke within its owned HWND subtree");
    std::cout << "Completed exact clear FindAll\n" << std::flush;
    require(matches != nullptr, "Real clear action query returns an element array");
    int count{};
    success(matches->get_Length(&count), "Count real clear action providers");
    require(count == 1, "The owned clear HWND exposes exactly one real semantic Invoke action");
    ComPtr<IUIAutomationElement> result;
    success(matches->GetElement(0, &result), "Retain the unique actual clear semantic action");
    require(result != nullptr, "Real clear semantic action exists");
    return result;
}
void describe_element(const wchar_t* name, IUIAutomationElement* element) {
    std::wcout << name;
    for (const auto property : {UIA_NamePropertyId, UIA_AutomationIdPropertyId, UIA_ControlTypePropertyId,
        UIA_FrameworkIdPropertyId, UIA_ProviderDescriptionPropertyId, UIA_IsInvokePatternAvailablePropertyId}) {
        Variant value;
        success(element->GetCurrentPropertyValue(property, &value.value), "Read exact owned UIA diagnostic property");
        std::wcout << L" property=" << property << L" vt=" << value.value.vt << L" value=";
        if (value.value.vt == VT_BSTR) std::wcout << value.value.bstrVal;
        else if (value.value.vt == VT_I4) std::wcout << value.value.lVal;
        else if (value.value.vt == VT_BOOL) std::wcout << (value.value.boolVal == VARIANT_TRUE);
    }
    std::wcout << L" runtime_id=";
    for (const auto id : runtime_id(element)) std::wcout << id << L',';
    std::wcout << L'\n' << std::flush;
}
void uia_work() {
    Fields fields(L"XUI matched native editor UIA workload", true);
    Observers observers;
    std::thread client;
    std::exception_ptr failure;
    std::promise<void> retired_subtree;
    auto retirement = retired_subtree.get_future();
    bool completed{};
    fields.window.post([&] {
        fields.plain->set_text(L"plain native UIA \U0001f600");
        fields.attach();
        const auto root = fields.window.native_window();
        observers.add(root);
        EnumChildWindows(root, [](HWND hwnd, LPARAM context) -> BOOL {
            reinterpret_cast<Observers*>(context)->add(hwnd); return TRUE;
        }, reinterpret_cast<LPARAM>(&observers));
        const std::shared_ptr<void> owner(OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId()),
            [](HANDLE value) { if (value) CloseHandle(value); });
        require(owner != nullptr, "Open only the owned UI thread CPU handle");
        client = std::thread([&, root, owner] {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            struct Com { HRESULT result; ~Com() { if (SUCCEEDED(result)) CoUninitialize(); } } com{initialized};
            bool closed_requested{};
            try {
                success(initialized, "Initialize owned native UIA query thread");
                ComPtr<IUIAutomation> automation;
                success(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)),
                    "Create real native UIA client");
                std::vector<QueryField> query;
                const std::array<std::shared_ptr<Control>, 5> models{
                    fields.input, fields.password, fields.dates[0], fields.dates[1], fields.dates[2]};
                for (unsigned index = 0; index != models.size(); ++index) {
                    QueryField field;
                    field.window = fields.native[index ? index + 2 : 0];
                    success(automation->ElementFromHandle(field.window, &field.element), "Resolve actual native HWND provider");
                    field.name = models[index]->name(); field.id = models[index]->automation_id(); field.help = models[index]->help_text();
                    field.focused = index == 0; field.identity = runtime_id(field.element.Get());
                    query.push_back(std::move(field));
                }
                QueryField caption;
                caption.window = child(root, L"STATIC");
                success(automation->ElementFromHandle(caption.window, &caption.element), "Resolve retained real native caption provider");
                caption.identity = runtime_id(caption.element.Get()); query.push_back(std::move(caption));
                const auto clear_name = L"Clear " + fields.input->name();
                struct Search { const std::wstring& name; HWND window{}; } search{clear_name};
                EnumChildWindows(root, [](HWND hwnd, LPARAM data) -> BOOL {
                    auto& search = *reinterpret_cast<Search*>(data);
                    wchar_t name[256]{}; GetWindowTextW(hwnd, name, 256);
                    if (search.name == name) { search.window = hwnd; return FALSE; }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&search));
                require(search.window != nullptr, "Resolve the retained real clear action HWND");
                ComPtr<IUIAutomationElement> clear_root;
                success(automation->ElementFromHandle(search.window, &clear_root), "Get actual clear root provider");
                wchar_t clear_class[128]{};
                GetClassNameW(search.window, clear_class, 128);
                std::wcout << L"Owned clear native class=" << clear_class << L" visible=" << IsWindowVisible(search.window) << L'\n';
                describe_element(L"Clear FromHandle", clear_root.Get());
                ComPtr<IUIAutomationTreeWalker> raw;
                ComPtr<IUIAutomationElement> raw_child;
                success(automation->get_RawViewWalker(&raw), "Get actual raw UIA tree walker");
                success(raw->GetFirstChildElement(clear_root.Get(), &raw_child), "Read exact clear HWND raw-view first child");
                if (raw_child) describe_element(L"Clear RawView first child", raw_child.Get());
                else std::cout << "Clear RawView first child is null\n" << std::flush;
                Variant framework, invoke_available;
                success(clear_root->GetCurrentPropertyValue(UIA_FrameworkIdPropertyId, &framework.value), "Read actual clear frontend framework");
                success(clear_root->GetCurrentPropertyValue(UIA_IsInvokePatternAvailablePropertyId, &invoke_available.value),
                    "Read actual clear frontend root Invoke availability");
                require(framework.value.vt == VT_BSTR && invoke_available.value.vt == VT_BOOL,
                    "Actual clear frontend framework and Invoke metadata have their exact shapes");
                std::wcout << L"Actual clear HWND UIA framework=" << framework.value.bstrVal <<
                    L" root_invoke_available=" << (invoke_available.value.boolVal == VARIANT_TRUE) << L'\n' << std::flush;
                ComPtr<IUIAutomationCondition> condition;
                VARIANT invoke_property{}; invoke_property.vt = VT_BOOL; invoke_property.boolVal = VARIANT_TRUE;
                success(automation->CreatePropertyCondition(UIA_IsInvokePatternAvailablePropertyId, invoke_property, &condition),
                    "Create bounded exact clear semantic-action query");
                auto clear_action = clear_semantic(clear_root.Get(), condition.Get());
                ComPtr<IUIAutomationInvokePattern> invoke;
                std::cout << "Begin actual clear Invoke pattern\n" << std::flush;
                success(clear_action->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)), "Get actual clear Invoke");
                require(invoke != nullptr, "Real clear semantic child owns Invoke");
                std::cout << "Begin actual clear Invoke action\n" << std::flush;
                success(invoke->Invoke(), "Invoke actual clear through COM and native dispatch barriers");
                std::cout << "Completed actual clear Invoke action\n" << std::flush;
                if (diagnostic_uia) std::cout << "Begin native restoration promise allocation\n" << std::flush;
                const auto restored = std::make_shared<std::promise<void>>();
                auto restoration = restored->get_future();
                if (diagnostic_uia) std::cout << "Begin enqueue native clear restoration\n" << std::flush;
                require(fields.window.post([&, restored, root] {
                    if (diagnostic_uia) std::cout << "Begin owner-thread native clear restoration\n" << std::flush;
                    require(fields.input->text().empty() && fields.counters.text_callbacks == 1,
                        "Real clear Invoke commits exactly one native edit callback");
                    require(SendMessageW(fields.native[0], EM_UNDO, 0, 0) &&
                        fields.input->text() == Fields::input_text[0] && fields.counters.text_callbacks == 2,
                        "Native undo restores real UIA clear text with exactly one further callback");
                    fields.flush(); UpdateWindow(root); restored->set_value();
                    if (diagnostic_uia) std::cout << "Completed owner-thread native clear restoration\n" << std::flush;
                }), "Post bounded native clear restoration");
                if (diagnostic_uia) std::cout << "Completed enqueue native clear restoration\n" << std::flush;
                require(restoration.wait_for(std::chrono::seconds(20)) == std::future_status::ready,
                    "Native UIA clear restoration completes within its bound");
                if (diagnostic_uia) std::cout << "Completed bounded restoration future\n" << std::flush;
                QueryField clear;
                clear.window = search.window; clear.element = clear_action; clear.name = clear_name;
                clear.id = L"xui-text-clear-" + fields.input->automation_id();
                clear.identity = runtime_id(clear.element.Get()); query.push_back(std::move(clear));
                for (auto& field : query) field.bounds = clipped_bounds(field.window);
                std::array<ComPtr<IUIAutomationElement>, 2> documents;
                std::array<ComPtr<IUIAutomationTextPattern>, 2> text;
                std::array<ComPtr<IUIAutomationTextPattern2>, 2> text2;
                std::array<std::vector<int>, 2> document_ids;
                std::array<RECT, 2> document_bounds;
                std::array<std::wstring, 2> document_text{fields.plain->text(), fields.rich->text()};
                unsigned text2_count{};
                for (unsigned index = 0; index != 2; ++index) {
                    if (diagnostic_uia) std::cout << "Begin actual OS Text/Text2 acquisition " << index << '\n' << std::flush;
                    success(automation->ElementFromHandle(fields.native[index + 1], &documents[index]), "Resolve real OS RichEdit provider");
                    document_ids[index] = runtime_id(documents[index].Get());
                    document_bounds[index] = clipped_bounds(fields.native[index + 1]);
                    success(documents[index]->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text[index])), "Retain OS native Text pattern");
                    require(text[index] != nullptr, "Both real RichEdit controls expose OS Text");
                    Variant available;
                    success(documents[index]->GetCurrentPropertyValue(UIA_IsTextPattern2AvailablePropertyId, &available.value),
                        "Read actual OS Text2 availability");
                    require(available.value.vt == VT_BOOL, "OS Text2 availability has its exact boolean shape");
                    if (available.value.boolVal == VARIANT_TRUE) {
                        success(documents[index]->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text2[index])),
                            "Retain available OS native Text2 pattern");
                        require(text2[index] != nullptr, "Available OS Text2 has a real pattern");
                        ++text2_count;
                    }
                }
                Work work;
                HRESULT password_result{};
                VARTYPE password_type{};
                struct Observation { Writes writes; Work callbacks; };
                const auto observation = [&] {
                    const auto sample = std::make_shared<std::promise<Observation>>();
                    auto future = sample->get_future();
                    require(fields.window.post([&, sample] {
                        sample->set_value({observers.writes, fields.counters});
                    }), "Post owned native observation outside the UIA measurement");
                    require(future.wait_for(std::chrono::seconds(20)) == std::future_status::ready,
                        "Owned native observation completes within its bound");
                    return future.get();
                };
                const auto before_observation = observation();
                const auto before_resources = resources();
                if (diagnostic_uia) std::cout << "Begin observed getter interval\n" << std::flush;
                const auto timing = measure(owner.get(), 1024, [&](unsigned iteration) {
                    if (diagnostic_uia && iteration % 128 == 0) std::cout << "Actual UIA getter iteration " << iteration << '\n' << std::flush;
                    for (const auto& field : query) {
                        for (const auto property : {UIA_NamePropertyId, UIA_AutomationIdPropertyId, UIA_HelpTextPropertyId}) {
                            if (diagnostic_uia && !iteration)
                                std::cout << "Begin actual metadata field " << (&field - query.data()) << " property " << property << '\n' << std::flush;
                            Variant value;
                            success(field.element->GetCurrentPropertyValue(property, &value.value), "Query real HWND UIA metadata");
                            ++work.queries;
                            const auto& expected = property == UIA_NamePropertyId ? field.name :
                                property == UIA_AutomationIdPropertyId ? field.id : field.help;
                            require(value.value.vt == VT_BSTR && value.value.bstrVal &&
                                std::wstring_view(value.value.bstrVal, SysStringLen(value.value.bstrVal)) == expected,
                                "Every real HWND metadata query returns exact independent UTF-16 metadata");
                            work.checksum = mix(work.checksum, SysStringLen(value.value.bstrVal));
                        }
                        for (const auto property : {UIA_HasKeyboardFocusPropertyId, UIA_IsOffscreenPropertyId}) {
                            Variant value;
                            success(field.element->GetCurrentPropertyValue(property, &value.value), "Query real HWND focus/offscreen state");
                            ++work.queries;
                            const bool expected = property == UIA_HasKeyboardFocusPropertyId ? field.focused : IsRectEmpty(&field.bounds) != FALSE;
                            require(value.value.vt == VT_BOOL && (value.value.boolVal == VARIANT_TRUE) == expected,
                                "Native focus and clipped offscreen booleans retain their exact values");
                            work.checksum = mix(work.checksum, expected);
                        }
                        Variant bounds;
                        success(field.element->GetCurrentPropertyValue(UIA_BoundingRectanglePropertyId, &bounds.value),
                            "Query actual clipped native UIA geometry");
                        ++work.queries;
                        require(bounds.value.vt == (VT_ARRAY | VT_R8) && bounds.value.parray &&
                            SafeArrayGetDim(bounds.value.parray) == 1 && bounds.value.parray->rgsabound[0].cElements == 4,
                            "Native geometry has exactly four double components");
                        const double expected_bounds[]{double(field.bounds.left), double(field.bounds.top),
                            double(field.bounds.right - field.bounds.left), double(field.bounds.bottom - field.bounds.top)};
                        for (LONG at = 0; at != 4; ++at) {
                            double actual{}; success(SafeArrayGetElement(bounds.value.parray, &at, &actual), "Read actual native bounds component");
                            require(actual == expected_bounds[at], "Every native UIA bounds component matches the actual owned clipped HWND");
                        }
                        work.checksum = mix(work.checksum, field.bounds.right - field.bounds.left);
                    }
                    Variant secure, denied;
                    success(query[1].element->GetCurrentPropertyValue(UIA_IsPasswordPropertyId, &secure.value), "Read native password classification");
                    require(secure.value.vt == VT_BOOL && secure.value.boolVal == VARIANT_TRUE, "Native UIA always classifies password as secret");
                    const auto result = query[1].element->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &denied.value);
                    require((FAILED(result) && denied.value.vt == VT_EMPTY) || (SUCCEEDED(result) &&
                        (denied.value.vt == VT_EMPTY || (denied.value.vt == VT_BSTR && SysStringLen(denied.value.bstrVal) == 0))),
                        "Real native password value is denied or empty, never plaintext");
                    if (!iteration) { password_result = result; password_type = denied.value.vt; }
                    require(result == password_result && denied.value.vt == password_type, "Every password-denial query preserves its exact HRESULT/output shape");
                    work.queries += 2;
                    for (unsigned index = 0; index != 2; ++index) {
                        ComPtr<IUIAutomationTextRange> range;
                        success(text[index]->get_DocumentRange(&range), "Get bounded actual OS Text document range");
                        require(range != nullptr, "OS Text range remains independently available");
                        BSTR value{};
                        success(range->GetText(static_cast<int>(document_text[index].size()), &value), "Read actual native RichEdit UTF-16 text");
                        const bool matches = value && std::wstring_view(value, SysStringLen(value)) == document_text[index];
                        const auto length = SysStringLen(value); SysFreeString(value);
                        require(matches, "OS Text returns exact real document content, not a model-only proxy");
                        work.checksum = mix(work.checksum, length); work.queries += 2;
                        if (text2[index]) {
                            BOOL active{}; ComPtr<IUIAutomationTextRange> caret;
                            success(text2[index]->GetCaretRange(&active, &caret), "Query actual OS Text2 native caret range");
                            require(caret != nullptr && active == FALSE, "Unfocused RichEdit retains a real inactive native caret range");
                            ++work.queries;
                        }
                    }
                });
                const auto after_observation = observation();
                if (diagnostic_uia) std::cout << "End observed getter interval\n" << std::flush;
                const auto native_work = after_observation.writes - before_observation.writes;
                work.updates = after_observation.callbacks.updates - before_observation.callbacks.updates;
                work.text_callbacks = after_observation.callbacks.text_callbacks - before_observation.callbacks.text_callbacks;
                work.password_callbacks = after_observation.callbacks.password_callbacks - before_observation.callbacks.password_callbacks;
                work.date_callbacks = after_observation.callbacks.date_callbacks - before_observation.callbacks.date_callbacks;
                report("real-hwnd-uia-getters", 1024, timing, work, native_work, before_resources, resources());
                require(native_work.editor_empty() && !native_work.destroyed && !native_work.geometry_notifications &&
                    !work.text_callbacks && !work.password_callbacks && !work.date_callbacks,
                    "Actual native UIA getters preserve every editor without writes, recreation, movement or model callbacks");
                require(work.queries == 1024ull * (7 * 6 + 2 + 4 + text2_count), "Every actual UIA getter and OS Text query is counted");
                for (const auto& field : query) {
                    const auto bounds = clipped_bounds(field.window);
                    require(IsWindow(field.window) && EqualRect(&bounds, &field.bounds),
                        "Every real UIA field retains its exact native clipped geometry after all queries");
                    ComPtr<IUIAutomationElement> fresh;
                    success(automation->ElementFromHandle(field.window, &fresh), "Reacquire actual native root without recreating it");
                    require(runtime_id(field.element.Get()) == field.identity, "Retained real UIA identity survives every getter");
                    if (field.window == search.window) {
                        const auto fresh_child = clear_semantic(fresh.Get(), condition.Get());
                        require(fresh_child && runtime_id(fresh_child.Get()) == field.identity, "Fresh clear semantic provider has its original runtime identity");
                    } else require(runtime_id(fresh.Get()) == field.identity, "Fresh native provider has its original runtime identity");
                }
                for (unsigned index = 0; index != 2; ++index) {
                    const auto bounds = clipped_bounds(fields.native[index + 1]);
                    require(IsWindow(fields.native[index + 1]) && EqualRect(&bounds, &document_bounds[index]),
                        "Both OS RichEdit Text/Text2 providers retain their exact native clipped geometry");
                    require(runtime_id(documents[index].Get()) == document_ids[index], "OS RichEdit runtime identity survives all Text/Text2 queries");
                }
                std::cout << "{\"phase\":\"uia-native-contract\",\"query_fields\":7,\"os_text_providers\":2,\"os_text2_providers\":"
                    << text2_count << ",\"password_hresult\":" << password_result << ",\"password_variant_type\":"
                    << password_type << ",\"clear_callbacks\":2}\n" << std::flush;
                require(fields.window.post([&] {
                    fields.window.replace_content(*fields.retirement_host, {});
                    retired_subtree.set_value();
                }), "Post owned native UIA subtree retirement while the owner STA keeps pumping");
                require(retirement.wait_for(std::chrono::seconds(20)) == std::future_status::ready, "Native UIA retirement completes within bound");
                require(IsWindow(root) && !IsWindow(search.window), "Retirement destroys the exact real clear HWND while its owner window remains live");
                BSTR retired_name{};
                const auto retired = clear_action->get_CurrentName(&retired_name);
                std::cout << "{\"phase\":\"uia-retirement\",\"name_hresult\":" << retired <<
                    ",\"name_length\":" << SysStringLen(retired_name);
                SysFreeString(retired_name);
                Variant retired_property;
                const auto property_result = clear_action->GetCurrentPropertyValueEx(UIA_NamePropertyId, TRUE, &retired_property.value);
                const auto invoke_result = invoke->Invoke();
                std::cout << ",\"property_no_default_hresult\":" << property_result <<
                    ",\"property_no_default_type\":" << retired_property.value.vt <<
                    ",\"invoke_hresult\":" << invoke_result << ",\"owner_window_live\":true}\n" << std::flush;
                require(retired == UIA_E_ELEMENTNOTAVAILABLE && property_result == UIA_E_ELEMENTNOTAVAILABLE &&
                    invoke_result == UIA_E_ELEMENTNOTAVAILABLE, "Real retained clear metadata and Invoke disconnect explicitly after subtree retirement");
                completed = true;
                require(fields.window.post([&] { fields.window.close(); }), "Close native UIA owner after every client assertion");
                closed_requested = true;
            } catch (const std::exception& error) {
                failure = std::current_exception();
                std::cerr << "Actual native UIA worker failure: " << error.what() << '\n' << std::flush;
            } catch (...) {
                failure = std::current_exception();
                std::cerr << "Actual native UIA worker failure: nonstandard exception\n" << std::flush;
            }
            if (!closed_requested && !fields.window.post([&] { fields.window.close(); }))
                std::cerr << "Native UIA workload could not post owner close\n";
        });
    });
    const auto result = Application::run(fields.window);
    if (client.joinable()) client.join();
    if (result) std::wcerr << fields.window.error() << '\n';
    if (failure) std::rethrow_exception(failure);
    require(!result && completed && Drawing::live_targets() == 0, "Actual native UIA workload retires all providers and renderers");
}
}
int main(int argc, char** argv) {
    if (argc != 2 || (std::string_view(argv[1]) != "--verify" && std::string_view(argv[1]) != "--verify-uia" &&
        std::string_view(argv[1]) != "--verify-storage" && std::string_view(argv[1]) != "--verify-embedding" &&
        std::string_view(argv[1]) != "--measure")) {
        std::cerr << "Usage: native-editor-host-probe --verify|--verify-uia|--verify-storage|--verify-embedding|--measure\n"; return 2;
    }
    measured = std::string_view(argv[1]) == "--measure";
    diagnostic_uia = std::string_view(argv[1]) == "--verify-uia";
    try {
        std::cout << std::setprecision(17);
        std::cout << "{\"allocation_scope\":\"ordinary-global-C++-new-and-new[]; excludes-aligned-new-and-COM-OS-DLL-heaps\"}\n";
        if (std::string_view(argv[1]) == "--verify-storage") { storage_work(); return 0; }
        if (std::string_view(argv[1]) == "--verify-embedding") { embedding_work(); return 0; }
        if (std::string_view(argv[1]) != "--verify-uia") { storage_work(); embedding_work(); editor_work(); }
        uia_work();
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

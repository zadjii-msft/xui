#include "xui/application.hpp"
#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include "../src/drawing.hpp"
#include "../demo/host_fixtures.hpp"
#include "suggestion_capture.hpp"
#include "owned_window_capture.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace xui { struct DrawingTestAccess { static void lose() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; } }; }
namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void flush(HWND hwnd) { SendMessageW(hwnd, WM_APP + 12, 0, 0); InvalidateRect(hwnd, nullptr, FALSE); UpdateWindow(hwnd); }
HWND child(HWND root, const wchar_t* name) {
    struct Find { const wchar_t* name; HWND result{}; } find{name};
    EnumChildWindows(root, [](HWND hwnd, LPARAM value) -> BOOL {
        auto& f = *reinterpret_cast<Find*>(value); wchar_t text[256]{}; GetWindowTextW(hwnd, text, 256);
        if (std::wstring_view(text) == f.name) { f.result = hwnd; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&find));
    require(find.result != nullptr, "Owned native control exists"); return find.result;
}
void save(const owned_window_capture::Pixels& pixels, const std::filesystem::path& path) {
    BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER), pixels.width, -pixels.height, 1, 32, BI_RGB};
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.data.size() * 4);
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<char*>(&header), sizeof(header));
    out.write(reinterpret_cast<char*>(&info), sizeof(info)); out.write(reinterpret_cast<const char*>(pixels.data.data()), pixels.data.size() * 4);
    require(bool(out), "Save owned live capture");
}
int uia(HWND hwnd, bool retain = false) {
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Client COM");
    ComPtr<IUIAutomation> automation; require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))), "UIA client");
    ComPtr<IUIAutomationElement> root; require(SUCCEEDED(automation->ElementFromHandle(hwnd, &root)), "UIA root");
    VARIANT name{}; name.vt = VT_BSTR; name.bstrVal = SysAllocString(L"Blue fixture");
    ComPtr<IUIAutomationCondition> condition; automation->CreatePropertyCondition(UIA_NamePropertyId, name, &condition); VariantClear(&name);
    ComPtr<IUIAutomationElement> shape; root->FindFirst(TreeScope_Subtree, condition.Get(), &shape); require(shape != nullptr, "Real scene semantic child");
    ComPtr<IUIAutomationSelectionItemPattern> selection;
    require(SUCCEEDED(shape->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection))) && selection, "Scene semantic selection");
    require(SUCCEEDED(selection->Select()), "UIA selects stable shape");
    ComPtr<IUIAutomationElement> container; selection->get_CurrentSelectionContainer(&container);
    ComPtr<IUIAutomationSelectionPattern> single;
    require(container && SUCCEEDED(container->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&single))) && single, "Scene semantic selection container");
    BOOL multiple{TRUE}; single->get_CurrentCanSelectMultiple(&multiple); require(!multiple, "Scene UIA does not advertise unsupported multi-selection");
    if (retain) {
        PostMessageW(hwnd, WM_KEYDOWN, VK_F11, 0);
        for (int i = 0; i < 500 && IsWindow(hwnd); ++i) Sleep(10);
        require(!IsWindow(hwnd), "Owned host disposed while provider remains retained");
        BSTR text{}; const auto result = shape->get_CurrentName(&text); SysFreeString(text);
        require(FAILED(result), "Retained provider reports unavailable after host disposal");
    }
    std::cout << "External UIA selected scene child\n";
    return 0;
}
void run_case(ThemeMode theme, UINT dpi, bool engines, const std::wstring& exe) {
    const auto fixtures = std::filesystem::absolute(L"hosts-fixtures");
    Window window({L"XUI hosts fixture", {720, 560}, theme});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto anchor = std::make_shared<Button>(L"Popup anchor"); root->add(anchor);
    auto pages = std::make_shared<PageView>(); root->add(pages, 1);
    auto vector = std::make_shared<VectorCanvas>(L"Vector fixture");
    auto shape = VectorShape::rectangle(1, {20, 20, 160, 110}); shape.fill = {0.1f, 0.4f, 0.8f, 1}; shape.interactive = true; shape.name = L"Blue fixture";
    vector->set_scene(std::make_shared<const VectorScene>(std::vector<VectorShape>{shape})); pages->add_page(vector);
    auto map = std::make_shared<MapView>(L"Map fixture");
    map->set_overlay({{{11, {0, 179}, L"East fixture"}, {12, {0, -179}, L"West fixture"}}, {}});
    map->set_view({0, 179}, 3); pages->add_page(map);
    auto media = std::make_shared<MediaPlayback>(); media->set_volume(0); media->set_preferred_size({680, 360});
    auto media_stack = std::make_shared<Stack>(Axis::vertical); media_stack->add(media);
    auto filler = std::make_shared<Label>(L"Owned scroll fixture"); filler->set_fixed_size({680, 650}); media_stack->add(filler);
    auto media_scroll = std::make_shared<ScrollView>(media_stack); pages->add_page(media_scroll);
    auto web = std::make_shared<WebContent>(); web->set_profile_root((fixtures / L"profiles").wstring()); pages->add_page(web);
    for (auto control : {std::static_pointer_cast<Control>(vector), std::static_pointer_cast<Control>(map),
        std::static_pointer_cast<Control>(media), std::static_pointer_cast<Control>(web)}) control->set_help_text(L"");
    web->on_state([ptr = web.get()](HostState state) { std::wcerr << L"Web state " << static_cast<int>(state) << L": " << ptr->status()->name() << L'\n'; });
    window.set_content(root);
    auto popup = std::make_shared<Popup>(std::make_shared<Label>(L"Native boundary popup"));
    int stage{}, selected{}; bool in_tick{}, idle_armed{}, web_frame_prepared{}; std::atomic<bool> done{};
    std::wstring dom; std::string error;
    ULONGLONG start = GetTickCount64(), transition = start;
    std::uint64_t idle_paints{};
    PROCESS_INFORMATION client{};
    vector->on_select([&](ShapeId id) { require(id == 1, "Native callback identity"); ++selected; });
    auto next = [&] { ++stage; transition = GetTickCount64(); std::cout << "Stage " << stage << '\n' << std::flush; };
    window.on_key([&](const KeyEvent& key) {
        if (key.key != Key::f12) return false;
        if (in_tick || done) return true;
        in_tick = true;
        struct Tick { bool& active; ~Tick() { active = false; } } tick{in_tick};
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI hosts fixture");
        try {
            require(GetTickCount64() - start < 90000, "Native hosts fixture timeout");
            if (media->state() == HostState::error) {
                std::wcerr << L"Media error: " << media->error() << L'\n';
                throw std::runtime_error("Media runtime reported an error");
            }
            if (web->state() == HostState::error) {
                std::wcerr << L"Web error: " << web->error() << L'\n';
                throw std::runtime_error("Web runtime reported an error");
            }
            switch (stage) {
            case 0: {
                RECT outer{}; GetWindowRect(hwnd, &outer); const auto current = GetDpiForWindow(hwnd);
                outer.right = outer.left + MulDiv(outer.right - outer.left, dpi, current);
                outer.bottom = outer.top + MulDiv(outer.bottom - outer.top, dpi, current);
                SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&outer)); flush(hwnd);
                if (engines) {
                    require(!GetModuleHandleW(L"mfplay.dll"), "Media runtime is lazy before first explicit load");
                    require(!GetModuleHandleW(L"EmbeddedBrowserWebView.dll"), "Browser runtime is lazy before first explicit load");
                    suggestion_capture::memory(GetCurrentProcess(), "hosts-before-runtime");
                }
                const auto path = std::filesystem::path(L"hosts-captures") / (L"vector-" + std::to_wstring(static_cast<int>(theme)) + L"-" + std::to_wstring(dpi) + L".bmp");
                suggestion_capture::bitmap(hwnd, nullptr, path);
                DrawingTestAccess::lose(); flush(hwnd); flush(hwnd); require(Drawing::live_targets() == 1, "Scene shares one recreated root target");
                if (engines) {
                    std::wstring cmd = L"\"" + exe + L"\" --uia " + std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd));
                    STARTUPINFOW startup{sizeof(startup)};
                    require(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &client), "Start owned UIA client"); CloseHandle(client.hThread);
                } else { vector->select(1); }
                next(); break;
            }
            case 1:
                if (engines) {
                    if (WaitForSingleObject(client.hProcess, 0) != WAIT_OBJECT_0) break;
                    DWORD code{}; GetExitCodeProcess(client.hProcess, &code); CloseHandle(client.hProcess); client.hProcess = nullptr;
                    require(code == 0 && selected == 1, "External UIA semantic selection succeeds once");
                }
                pages->select(1); flush(hwnd);
                { const auto before = map->center().longitude; SendMessageW(child(hwnd, L"Map fixture"), WM_KEYDOWN, VK_RIGHT, 0); require(map->center().longitude != before, "Native map arrow pan"); }
                SendMessageW(child(child(hwnd, L"Map fixture"), L"Interactive scene elements"), WM_KEYDOWN, VK_END, 0);
                require(map->selected() == 12, "Native keyboard selects a stable geographic marker");
                map->pan(4, 0); require(map->selected() == 12, "Pan preserves selected marker identity");
                { auto target = child(hwnd, L"Map fixture"); const auto before = map->center().longitude;
                  SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 80));
                  SendMessageW(target, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(65, 90));
                  SendMessageW(target, WM_LBUTTONUP, 0, MAKELPARAM(65, 90));
                  require(map->center().longitude != before, "Native mouse drag pans map"); }
                suggestion_capture::bitmap(hwnd, nullptr, std::filesystem::path(L"hosts-captures") / (L"map-" + std::to_wstring(static_cast<int>(theme)) + L"-" + std::to_wstring(dpi) + L".bmp"));
                window.show_popup(popup, *anchor); flush(hwnd); window.dismiss_popup(*popup);
                { const auto pending = map->request_overlay(); pages->select(2); flush(hwnd);
                  require(pending.stop.stop_requested(), "Hidden map cancels provider requests"); }
                if (!engines) { pages->select(3); flush(hwnd); stage = 18; break; }
                media->load_local((fixtures / L"tone.wav").wstring()); next(); break;
            case 2:
                if (media->state() == HostState::error) throw std::runtime_error("MF audio failed");
                if (media->state() != HostState::ready) break;
                require(media->duration() > 2.9 && media->duration() < 3.1, "Real WAV duration");
                media->play(); next(); break;
            case 3:
                if (media->state() != HostState::playing) break;
                media->pause(); next(); break;
            case 4:
                if (media->state() != HostState::paused) break;
                media->seek(1.25); next(); break;
            case 5:
                media->refresh(); if (std::abs(media->position() - 1.25) > 0.1) break;
                media->stop(); next(); break;
            case 6:
                if (media->state() != HostState::stopped) break;
                media->load_local((fixtures / L"video.avi").wstring()); next(); break;
            case 7:
                if (media->state() == HostState::error) throw std::runtime_error("MF video failed");
                if (media->state() != HostState::ready) break;
                media->play(); next(); break;
            case 8:
                if (media->state() != HostState::playing || GetTickCount64() - transition < 350) break;
                { RECT current{}; GetWindowRect(hwnd, &current);
                  SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&current));
                  window.set_theme(ThemeMode::light); DrawingTestAccess::lose(); flush(hwnd); flush(hwnd);
                  require(Drawing::live_targets() == 1, "Active video survives DPI, theme, and root target recreation"); }
                { auto pixels = owned_window_capture::capture(hwnd); std::size_t colored{};
                  for (auto p : pixels.data) { const auto r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255; if (g < 70 && ((r > 180 && b < 70) || (b > 180 && r < 70))) ++colored; }
                  save(pixels, L"hosts-captures\\media-live.bmp"); require(colored > 5000, "Actual MF video frames are visible");
                  Sleep(180); auto later = owned_window_capture::capture(hwnd); std::size_t changed{};
                  require(later.data.size() == pixels.data.size(), "Video capture extent stays stable");
                  for (std::size_t i = 0; i < pixels.data.size(); ++i) if (pixels.data[i] != later.data[i]) ++changed;
                  require(changed > 5000, "Native video advances between actual compositor frames");
                  save(later, L"hosts-captures\\media-next.bmp");
                  std::cout << "Video colored pixels=" << colored << " changed pixels=" << changed << '\n'; }
                { bool blocked{}; try { window.show_popup(popup, *anchor); } catch (const std::logic_error&) { blocked = true; } require(blocked && !popup->is_open(), "Active external video rejects retained popup"); }
                media->pause(); stage = 17; transition = GetTickCount64(); break;
            case 17:
                if (media->state() != HostState::paused && GetTickCount64() - transition < 5000) break;
                require(media->state() == HostState::paused, "Native video pauses");
                { auto visual = FindWindowExW(child(hwnd, L"Media playback"), nullptr, L"STATIC", nullptr);
                  require(visual != nullptr, "Native video visual exists");
                  InvalidateRect(visual, nullptr, TRUE); UpdateWindow(visual);
                  auto pixels = owned_window_capture::capture(hwnd); std::size_t colored{};
                  for (auto p : pixels.data) if (((p >> 8) & 255) < 70 && (((p >> 16) & 255) > 180 || (p & 255) > 180)) ++colored;
                  require(colored > 5000, "Paused video retains its real frame after native repaint");
                  save(pixels, L"hosts-captures\\media-paused-repaint.bmp"); }
                media_scroll->set_offset(60); flush(hwnd);
                { auto pixels = owned_window_capture::capture(hwnd); std::size_t leaked{};
                  for (int y = 0; y < 30; ++y) for (int x = 0; x < pixels.width; ++x) {
                      auto p = pixels.data[static_cast<std::size_t>(y) * pixels.width + x];
                      if (((p >> 8) & 255) < 70 && (((p >> 16) & 255) > 180 || (p & 255) > 180)) ++leaked;
                  }
                  require(leaked == 0, "Native video is clipped below the scroll viewport header"); }
                media_scroll->set_offset(1000); flush(hwnd); require(media->state() == HostState::suspended, "Fully scrolled-out media unloads");
                pages->select(3); flush(hwnd); require(media->state() == HostState::suspended, "Hidden media releases player");
                web->set_html(L"<!doctype html><html><body style='background:rgb(15,90,45);color:white'><h1 id='owned'>Owned DOM fixture</h1><input aria-label='Web fixture input'><button>Owned button</button></body></html>");
#ifndef XUI_ENABLE_WEBVIEW2
                require(web->state() == HostState::error, "Disabled optional web host reports a visible error");
                std::cout << "WebView2 feature disabled. Browser runtime checks did not run.\n";
                web->unload(); pages->select(0); flush(hwnd); stage = 18; break;
#endif
                stage = 8; next(); break;
            case 9:
                if (web->state() == HostState::error) throw std::runtime_error("WebView2 unavailable or failed");
                if (web->state() != HostState::ready) break;
                web->evaluate(L"document.getElementById('owned').textContent", [&](std::wstring value, std::wstring failure) { require(failure.empty(), "Web script succeeded"); dom = std::move(value); });
                next(); break;
            case 10:
                if (dom.empty()) break;
                require(dom == L"\"Owned DOM fixture\"", "Real web engine evaluates owned DOM");
                if (!web_frame_prepared) {
                    window.set_theme(ThemeMode::high_contrast); DrawingTestAccess::lose(); flush(hwnd); flush(hwnd);
                    require(Drawing::live_targets() == 1, "Active web content survives theme and root target recreation");
                    web_frame_prepared = true;
                }
                { auto pixels = owned_window_capture::capture(hwnd); std::size_t green{};
                  for (auto p : pixels.data) if (((p >> 16) & 255) < 30 && ((p >> 8) & 255) > 75 && ((p >> 8) & 255) < 105 && (p & 255) > 30 && (p & 255) < 65) ++green;
                  save(pixels, L"hosts-captures\\web-live.bmp");
                  if (green <= 5000 && GetTickCount64() - transition < 8000) break;
                  require(green > 5000, "Actual WebView2 HTML pixels are visible"); std::cout << "Web green pixels=" << green << " DOM=Owned DOM fixture\n"; }
                web->focus_content(); require(IsChild(child(hwnd, L"Web content"), GetFocus()), "Web keyboard focus enters native runtime");
                { const auto before = GetFocus(); bool blocked{};
                  try { window.show_popup(popup, *anchor); } catch (const std::logic_error&) { blocked = true; }
                  require(blocked && GetFocus() == before, "A rejected retained popup preserves native browser focus"); }
                dom.clear(); web->evaluate(L"window.open('https://example.invalid');document.getElementById('owned').textContent='DOM changed';document.querySelector('button').focus();document.getElementById('owned').textContent",
                    [&](std::wstring value, std::wstring failure) { require(failure.empty(), "Owned script mutation"); dom = std::move(value); });
                next(); break;
            case 11:
                if (dom.empty()) break;
                require(dom == L"\"DOM changed\"", "Owned DOM mutation is real");
                PostMessageW(GetFocus(), WM_KEYDOWN, VK_TAB, 1); PostMessageW(GetFocus(), WM_KEYUP, VK_TAB, 1);
                stage = 16; transition = GetTickCount64(); break;
            case 16:
                if (GetFocus() != child(hwnd, L"Popup anchor") && GetTickCount64() - transition < 5000) break;
                require(GetFocus() == child(hwnd, L"Popup anchor"), "Native browser Tab returns to XUI focus traversal");
                web->reload(); stage = 11; next(); break;
            case 12:
                if (web->state() != HostState::ready || GetTickCount64() - transition < 300) break;
                pages->select(0); flush(hwnd); require(web->state() == HostState::suspended, "Hidden web releases controller");
                window.show_popup(popup, *anchor); flush(hwnd); window.dismiss_popup(*popup);
                pages->select(3); flush(hwnd); require(web->state() == HostState::suspended, "Hidden host does not auto-resume");
                web->set_html(L"<h1>Obsolete load</h1>"); web->stop();
                require(web->state() == HostState::stopped, "Stop cancels an in-flight web environment request");
                web->reload(); web->set_html(L"<h1 id='latest'>Latest owned load</h1>");
                next(); break;
            case 13:
                if (web->state() != HostState::ready) break;
                dom.clear(); web->evaluate(L"document.getElementById('latest').textContent", [&](std::wstring value, std::wstring failure) { require(failure.empty(), "Latest generation evaluates"); dom = std::move(value); });
                next(); break;
            case 14:
                if (dom.empty()) break;
                require(dom == L"\"Latest owned load\"", "Stale environment callbacks cannot replace current content");
                web->unload(); pages->select(2); media_scroll->set_offset(0); flush(hwnd);
                media->load_local((fixtures / L"tone.wav").wstring()); media->unload(); pages->select(0); flush(hwnd); next(); break;
            case 15:
                if (GetTickCount64() - transition < 500) break;
                require(media->state() == HostState::idle && web->state() == HostState::idle, "Stale media and web callbacks cannot revive unloaded hosts");
                suggestion_capture::memory(GetCurrentProcess(), "hosts-after-unload");
                stage = 18; break;
            case 18:
                if (!idle_armed) { flush(hwnd); idle_armed = true; transition = GetTickCount64(); break; }
                if (GetTickCount64() - transition < 500) break;
                idle_paints = SendMessageW(hwnd, WM_APP + 60, 0, 0); next(); break;
            case 19:
                if (GetTickCount64() - transition < 300) break;
                require(static_cast<std::uint64_t>(SendMessageW(hwnd, WM_APP + 60, 0, 0)) == idle_paints, "Zero idle root paints");
                require(Drawing::live_targets() == 1, "One root target with all four retained families");
                done = true; window.close(); break;
            }
        } catch (const std::exception& e) { std::cerr << "Failure at stage " << stage << ": " << e.what() << '\n'; error = e.what(); done = true; window.close(); }
        return true;
    });
    std::jthread driver([&](std::stop_token stop) {
        while (!done && !stop.stop_requested()) {
            if (auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI hosts fixture")) PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
            Sleep(30);
        }
    });
    const auto result = Application::run(window); done = true; driver.join();
    if (result) std::wcerr << L"Host window error: " << window.error() << '\n';
    if (engines) suggestion_capture::memory(GetCurrentProcess(), "hosts-after-window-close");
    require(error.empty(), error.c_str()); require(result == 0, "Owned host window succeeds"); require(Drawing::live_targets() == 0, "Root target released after close");
}
void lifetime_case(int kind) {
    auto window = std::make_unique<Window>(WindowOptions{L"XUI host lifetime", {420, 300}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto media = std::make_shared<MediaPlayback>(); media->set_volume(0);
    auto web = std::make_shared<WebContent>();
    web->set_profile_root(std::filesystem::absolute(L"hosts-fixtures\\lifetime-profiles").wstring());
    root->add(kind < 2 ? std::static_pointer_cast<Element>(media) : std::static_pointer_cast<Element>(web), 1);
    window->set_content(root);
    int callbacks{}; std::atomic<bool> done{};
    auto state = [&](HostState value) {
        if (value != HostState::loading) return;
        ++callbacks; done = true;
        if (kind == 1 || kind == 3) window.reset(); else window->close();
        if (kind == 3) throw std::runtime_error("Expected host callback exception after disposal");
    };
    media->on_state(state); web->on_state(state);
    window->on_key([&](const KeyEvent& key) {
        if (key.key != Key::f12 || done) return false;
        if (kind < 2) media->load_local(std::filesystem::absolute(L"hosts-fixtures\\tone.wav").wstring());
        else web->set_html(L"<h1>Disposed during asynchronous environment creation</h1>");
        return true;
    });
    std::jthread driver([&](std::stop_token stop) {
        for (int i = 0; i < 400 && !done && !stop.stop_requested(); ++i) {
            if (auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI host lifetime")) PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
            Sleep(20);
        }
    });
    const auto result = Application::run(*window); done = true; driver.join();
    require(callbacks == 1, "One state callback before owner disposal");
    require(result == (kind == 3 ? 1 : 0), "Asynchronous host closure and throw-after-disposal result");
    require(Drawing::live_targets() == 0, "Disposed host releases shared root target");
    media->play(); media->unload(); web->unload();
    bool revoked{}; web->evaluate(L"1", [&](auto, std::wstring error) { revoked = !error.empty(); });
    require(revoked, "Disposed web script adapter is revoked");
}
void retained_provider_case(const std::wstring& exe) {
    Window window({L"XUI retained scene provider", {420, 300}});
    auto root = std::make_shared<Stack>(Axis::vertical); auto vector = std::make_shared<VectorCanvas>();
    auto shape = VectorShape::rectangle(1, {10, 10, 50, 50}); shape.interactive = true; shape.name = L"Blue fixture";
    vector->set_scene(std::make_shared<const VectorScene>(std::vector<VectorShape>{shape}));
    root->add(vector, 1); window.set_content(root);
    PROCESS_INFORMATION client{}; std::atomic<bool> done{};
    window.on_key([&](const KeyEvent& key) {
        if (key.key == Key::f11) { done = true; window.close(); return true; }
        if (key.key != Key::f12 || client.hProcess) return false;
        auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI retained scene provider");
        std::wstring cmd = L"\"" + exe + L"\" --uia-retain " + std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd));
        STARTUPINFOW startup{sizeof(startup)};
        require(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &client), "Start retained UIA client");
        CloseHandle(client.hThread); return true;
    });
    std::jthread driver([&](std::stop_token stop) {
        while (!done && !stop.stop_requested()) {
            if (auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI retained scene provider")) PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
            Sleep(30);
        }
    });
    require(Application::run(window) == 0, "Retained provider fixture closed"); done = true; driver.join();
    require(client.hProcess && WaitForSingleObject(client.hProcess, 10000) == WAIT_OBJECT_0, "Retained UIA client finishes");
    DWORD code{}; GetExitCodeProcess(client.hProcess, &code); CloseHandle(client.hProcess);
    require(code == 0, "Retained virtual scene provider disconnects");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--uia") return uia(reinterpret_cast<HWND>(_wcstoui64(argv[2], nullptr, 10)));
        if (argc == 3 && std::wstring_view(argv[1]) == L"--uia-retain") return uia(reinterpret_cast<HWND>(_wcstoui64(argv[2], nullptr, 10)), true);
        std::filesystem::create_directories(L"hosts-captures");
        const auto fixtures = std::filesystem::absolute(L"hosts-fixtures"); host_fixtures::wave(fixtures / L"tone.wav"); host_fixtures::video(fixtures / L"video.avi");
        wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
        for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
            for (UINT dpi : {96u, 144u, 192u}) run_case(theme, dpi, theme == ThemeMode::dark && dpi == 96, exe);
        lifetime_case(0); lifetime_case(1);
#ifdef XUI_ENABLE_WEBVIEW2
        lifetime_case(2); lifetime_case(3);
#endif
        retained_provider_case(exe);
        for (int i = 0; i < 100; ++i) { std::error_code error; std::filesystem::remove_all(fixtures, error); if (!error) break; Sleep(50); }
        require(!std::filesystem::exists(fixtures), "Owned fixtures and runtime profiles cleaned after shutdown");
#ifdef XUI_ENABLE_WEBVIEW2
        std::cout << "Four native families, real Media Foundation WAV/video, real WebView2 DOM/pixels, UIA selection, nine theme/DPI cases, and lifecycle passed\n";
#else
        std::cout << "Media, scene, and map native tests passed. WebView2 integration was disabled and is not verified by this run.\n";
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

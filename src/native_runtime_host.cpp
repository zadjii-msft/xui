#include "native_runtime_host.hpp"
#include <mfplay.h>
#include <mferror.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/event.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#ifdef XUI_ENABLE_WEBVIEW2
#include <WebView2.h>
#endif

namespace xui {
using Microsoft::WRL::ComPtr;
namespace {
#ifdef XUI_ENABLE_WEBVIEW2
struct WebProfile : std::enable_shared_from_this<WebProfile> {
    std::filesystem::path path;
    unsigned pending{};
    bool retired{};
    HANDLE process{};
    ComPtr<ICoreWebView2Environment5> environment;
    EventRegistrationToken exit_token{};
    bool exited{};
    void release_environment() {
        if (environment) environment->remove_BrowserProcessExited(exit_token);
        environment.Reset();
    }
    ~WebProfile() {
        release_environment();
        if (process) CloseHandle(process);
    }
    void observe_environment(ICoreWebView2Environment* value) {
        if (!value || FAILED(value->QueryInterface(IID_PPV_ARGS(&environment)))) return;
        const auto weak = weak_from_this();
        if (FAILED(environment->add_BrowserProcessExited(
            Microsoft::WRL::Callback<ICoreWebView2BrowserProcessExitedEventHandler>(
                [weak](ICoreWebView2Environment*, ICoreWebView2BrowserProcessExitedEventArgs*) -> HRESULT {
                    if (auto profile = weak.lock()) profile->exited = true;
                    return S_OK;
                }).Get(), &exit_token))) environment.Reset();
    }
    void observe(ICoreWebView2Controller* controller) {
        if (!controller || process) return;
        ComPtr<ICoreWebView2> web;
        UINT32 pid{};
        if (SUCCEEDED(controller->get_CoreWebView2(&web)) && web &&
            SUCCEEDED(web->get_BrowserProcessId(&pid)) && pid)
            process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    }
};
thread_local std::vector<std::shared_ptr<WebProfile>> web_profiles;
void collect_web_profiles() {
    std::erase_if(web_profiles, [](const auto& profile) {
        if (!profile->retired || profile->pending ||
            (profile->environment && !profile->exited) ||
            (profile->process && WaitForSingleObject(profile->process, 0) != WAIT_OBJECT_0)) return false;
        std::error_code error;
        std::filesystem::remove_all(profile->path, error);
        return !error;
    });
}
#endif
std::wstring failure(HRESULT hr) { return L"Windows host error " + std::to_wstring(static_cast<unsigned long>(hr)); }
class NativeFailure final : public std::runtime_error {
public:
    explicit NativeFailure(HRESULT hr) : std::runtime_error("Native runtime operation failed"), result(hr) {}
    HRESULT result;
};
void require_hr(HRESULT hr) { if (FAILED(hr)) throw NativeFailure(hr); }
struct MediaEvent { MFP_EVENT_TYPE type{}; HRESULT result{}; ComPtr<IMFPMediaItem> item; };
struct MediaMailbox {
    std::mutex mutex;
    HWND target{};
    bool alive{true}, posted{};
    std::vector<MediaEvent> events;
};
class MediaCallback final : public IMFPMediaPlayerCallback {
public:
    explicit MediaCallback(std::shared_ptr<MediaMailbox> mailbox) : mailbox_(std::move(mailbox)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(IMFPMediaPlayerCallback)) return E_NOINTERFACE;
        *value = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --references_; if (!n) delete this; return n; }
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override {
        try {
            std::lock_guard lock(mailbox_->mutex);
            if (!mailbox_->alive) return;
            MediaEvent item{event->eEventType, event->hrEvent};
            if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED)
                item.item = reinterpret_cast<MFP_MEDIAITEM_CREATED_EVENT*>(event)->pMediaItem;
            if (mailbox_->events.size() < 16) mailbox_->events.push_back(std::move(item));
            else mailbox_->events.back().result = E_OUTOFMEMORY;
            if (!mailbox_->posted) mailbox_->posted = PostMessageW(mailbox_->target, runtime_event_message, 0, 0) != FALSE;
        } catch (...) {}
    }
private:
    std::atomic<ULONG> references_{1};
    std::shared_ptr<MediaMailbox> mailbox_;
};
}
struct NativeRuntimeHost::State : std::enable_shared_from_this<State> {
    std::shared_ptr<RuntimeHost> model;
    std::function<void()> failed;
    std::function<bool()> can_activate;
    HWND parent{}, visual{};
    RECT placed_bounds{};
    RECT placed_clip{};
    int placed_radius{};
    bool placed{};
    bool shown{}, disposed{}, has_request{}, has_video{};
    std::uint64_t revision{}, epoch{};
    HMODULE mf_module{};
    ComPtr<IMFPMediaPlayer> player;
    std::shared_ptr<MediaMailbox> mailbox;
    std::filesystem::path profile;
    std::vector<std::filesystem::path> retired_profiles;
    std::set<std::uint64_t> pending_environments;
    static LRESULT CALLBACK visual_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) noexcept {
        auto* state = reinterpret_cast<State*>(data);
        if (message == WM_NCDESTROY) {
            state->visual = nullptr;
            RemoveWindowSubclass(hwnd, visual_procedure, id);
        }
        if (message == WM_ERASEBKGND && state->player && state->has_video && !state->disposed) return 1;
        if (message == WM_PAINT && state->player && state->has_video && !state->disposed) {
            auto current = state->player;
            PAINTSTRUCT paint{}; BeginPaint(hwnd, &paint);
            if (FAILED(current->UpdateVideo()))
                FillRect(paint.hdc, &paint.rcPaint, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd, &paint); return 0;
        }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
#ifdef XUI_ENABLE_WEBVIEW2
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    std::shared_ptr<WebProfile> profile_lifetime;
    std::uint64_t navigation{}, script_id{};
    bool owned_navigation_pending{}, stopping{};
    std::wstring owned_document;
    std::map<std::uint64_t, std::function<void(std::wstring, std::wstring)>> scripts;
#endif
    template<class F> void guard(F&& action) noexcept {
        auto keep = shared_from_this();
        try { if (!disposed) action(); }
        catch (const NativeFailure& error) {
            try { close(); if (!disposed) model->publish_state(HostState::error, failure(error.result)); }
            catch (...) { if (failed) failed(); }
        } catch (const std::filesystem::filesystem_error&) {
            try { close(); if (!disposed) model->publish_state(HostState::error, L"XUI cannot create or access the owned web profile."); }
            catch (...) { if (failed) failed(); }
        } catch (...) { if (failed) failed(); }
    }
    void close() {
        ++epoch;
        has_video = false;
#ifdef XUI_ENABLE_WEBVIEW2
        scripts.clear();
        owned_navigation_pending = false; stopping = false; owned_document.clear();
        if (web) web->Stop();
        if (controller) controller->Close();
        web.Reset(); controller.Reset(); environment.Reset();
        if (profile_lifetime) {
            profile_lifetime->retired = true;
            profile_lifetime.reset();
        }
        collect_web_profiles();
#endif
        if (mailbox) {
            std::lock_guard lock(mailbox->mutex);
            mailbox->alive = false; mailbox->events.clear();
        }
        if (player) player->Shutdown();
        player.Reset(); mailbox.reset();
        if (mf_module) FreeLibrary(std::exchange(mf_module, nullptr));
        if (visual) ShowWindow(visual, SW_HIDE);
        has_request = false;
        if (!profile.empty()) {
            retired_profiles.push_back(profile);
            profile.clear();
        }
        if (pending_environments.empty())
            std::erase_if(retired_profiles, [](const auto& path) {
                std::error_code error; return !std::filesystem::exists(path, error) && !error;
            });
    }
    bool active() const {
        return has_request;
    }
    void update_position() {
        auto media = std::dynamic_pointer_cast<MediaPlayback>(model);
        if (!media || !player) return;
        auto current = player; const auto generation = epoch;
        PROPVARIANT position{}, duration{};
        const auto p = current->GetPosition(MFP_POSITIONTYPE_100NS, &position);
        const auto d = current->GetDuration(MFP_POSITIONTYPE_100NS, &duration);
        const auto seconds = [](const PROPVARIANT& value) {
            return value.vt == VT_UI8 ? value.uhVal.QuadPart / 1e7 : value.vt == VT_I8 ? value.hVal.QuadPart / 1e7 : 0;
        };
        if (!disposed && generation == epoch)
            media->publish_position(SUCCEEDED(p) ? seconds(position) : 0, SUCCEEDED(d) ? seconds(duration) : 0);
        PropVariantClear(&position); PropVariantClear(&duration);
    }
    void media_command(MediaOperation operation, double value) {
        auto media = std::static_pointer_cast<MediaPlayback>(model);
        if (operation == MediaOperation::unload) { close(); media->publish_state(HostState::idle, L"Not loaded"); return; }
        if (operation == MediaOperation::load) {
            if (!shown) return;
            if (can_activate && !can_activate()) { media->publish_state(HostState::error, L"Dismiss XUI overlays before loading native media."); return; }
            close(); revision = media->revision();
            const auto attrs = GetFileAttributesW(media->source().c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                media->publish_state(HostState::error, L"The explicit local media file does not exist."); return;
            }
            mf_module = LoadLibraryExW(L"mfplay.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!mf_module) { media->publish_state(HostState::error, L"Windows Media Foundation is not installed."); return; }
            const auto create = reinterpret_cast<decltype(&MFPCreateMediaPlayer)>(GetProcAddress(mf_module, "MFPCreateMediaPlayer"));
            if (!create) { close(); media->publish_state(HostState::error, L"Windows MFPlay is unavailable."); return; }
            mailbox = std::make_shared<MediaMailbox>(); mailbox->target = parent;
            ComPtr<IMFPMediaPlayerCallback> callback; callback.Attach(new MediaCallback(mailbox));
            HRESULT hr = create(nullptr, FALSE, 0, callback.Get(), visual, &player);
            if (SUCCEEDED(hr)) hr = player->SetVolume(static_cast<float>(media->volume()));
            if (SUCCEEDED(hr)) hr = player->CreateMediaItemFromURL(media->source().c_str(), FALSE, 0, nullptr);
            if (FAILED(hr)) { close(); media->publish_state(HostState::error, failure(hr)); return; }
            has_request = true; ShowWindow(visual, SW_SHOWNA);
            media->publish_state(HostState::loading, L"Load local media");
            return;
        }
        if (!player) return;
        auto current = player; const auto generation = epoch;
        HRESULT hr = S_OK;
        switch (operation) {
        case MediaOperation::play: hr = current->Play(); break;
        case MediaOperation::pause: hr = current->Pause(); break;
        case MediaOperation::stop: hr = current->Stop(); break;
        case MediaOperation::volume: hr = current->SetVolume(static_cast<float>(value)); break;
        case MediaOperation::seek: {
            update_position();
            PROPVARIANT position{}; position.vt = VT_I8;
            position.hVal.QuadPart = static_cast<LONGLONG>(std::min(value, media->duration()) * 1e7);
            if (disposed || generation != epoch) return;
            hr = current->SetPosition(MFP_POSITIONTYPE_100NS, &position); break;
        }
        default: break;
        }
        if (disposed || generation != epoch) return;
        update_position();
        if (disposed || generation != epoch) return;
        if (FAILED(hr)) media->publish_state(HostState::error, failure(hr));
    }
    void media_events() {
        if (!mailbox) return;
        std::vector<MediaEvent> events;
        { std::lock_guard lock(mailbox->mutex); events.swap(mailbox->events); mailbox->posted = false; }
        const auto generation = epoch;
        for (auto& event : events) {
            if (disposed || generation != epoch || !player) return;
            auto current = player;
            if (FAILED(event.result)) { close(); model->publish_state(HostState::error, failure(event.result)); return; }
            switch (event.type) {
            case MFP_EVENT_TYPE_MEDIAITEM_CREATED: {
                BOOL present{}, selected{};
                require_hr(event.item->HasVideo(&present, &selected));
                if (disposed || generation != epoch) return;
                has_video = present && selected;
                require_hr(current->SetMediaItem(event.item.Get())); break;
            }
            case MFP_EVENT_TYPE_MEDIAITEM_SET:
                update_position();
                if (disposed || generation != epoch || !player) return;
                current->UpdateVideo();
                if (disposed || generation != epoch) return;
                model->publish_state(HostState::ready, L"Ready. Use Play to start."); break;
            case MFP_EVENT_TYPE_PLAY: model->publish_state(HostState::playing, L"Playing"); break;
            case MFP_EVENT_TYPE_PAUSE: model->publish_state(HostState::paused, L"Paused"); break;
            case MFP_EVENT_TYPE_STOP:
            case MFP_EVENT_TYPE_PLAYBACK_ENDED:
                update_position();
                if (disposed || generation != epoch) return;
                model->publish_state(HostState::stopped, L"Stopped"); break;
            case MFP_EVENT_TYPE_POSITION_SET: update_position(); break;
            default: break;
            }
        }
    }
#ifdef XUI_ENABLE_WEBVIEW2
    template<class F> static auto guarded(std::weak_ptr<State> weak, std::uint64_t generation, F function) {
        return [weak, generation, function](auto* sender, auto* args) -> HRESULT {
            if (auto self = weak.lock(); self && !self->disposed && self->epoch == generation)
                self->guard([&] { function(*self, sender, args); });
            return S_OK;
        };
    }
    void web_ready(ICoreWebView2Controller* value) {
        controller = value; require_hr(controller->get_CoreWebView2(&web));
        ComPtr<ICoreWebView2Settings> settings; require_hr(web->get_Settings(&settings));
        require_hr(settings->put_AreDevToolsEnabled(FALSE));
        require_hr(settings->put_AreDefaultContextMenusEnabled(FALSE));
        require_hr(settings->put_IsStatusBarEnabled(FALSE));
        ComPtr<ICoreWebView2Settings3> settings3;
        if (SUCCEEDED(settings.As(&settings3))) settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
        auto weak = weak_from_this(); const auto generation = epoch;
        EventRegistrationToken token{};
        require_hr(web->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
            guarded(weak, generation, [](State& s, auto*, ICoreWebView2NavigationStartingEventArgs* args) {
                if (s.stopping) { args->put_Cancel(TRUE); return; }
                LPWSTR uri{}; require_hr(args->get_Uri(&uri)); const std::wstring text = uri ? uri : L""; CoTaskMemFree(uri);
                auto model = std::static_pointer_cast<WebContent>(s.model);
                BOOL user{}; args->get_IsUserInitiated(&user);
                const bool owned = !user && s.owned_navigation_pending && model->is_html() &&
                    text.starts_with(L"data:text/html;charset=utf-8;base64,");
                if (owned) { s.owned_navigation_pending = false; s.owned_document = text; }
                if (!owned && !model->allows(text)) {
                    args->put_Cancel(TRUE);
                    model->publish_state(HostState::error, L"Navigation is blocked by the explicit origin policy."); return;
                }
                args->get_NavigationId(&s.navigation);
                model->publish_state(HostState::loading, L"Load web content");
            })).Get(), &token));
        require_hr(web->add_NavigationCompleted(Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
            guarded(weak, generation, [](State& s, auto*, ICoreWebView2NavigationCompletedEventArgs* args) {
                if (s.stopping) return;
                UINT64 id{}; args->get_NavigationId(&id); if (id != s.navigation) return;
                BOOL success{}; args->get_IsSuccess(&success);
                s.model->publish_state(success ? HostState::ready : HostState::error, success ? L"Web content ready" : L"Web navigation failed or was canceled.");
            })).Get(), &token));
        require_hr(web->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            guarded(weak, generation, [](State&, auto*, ICoreWebView2NewWindowRequestedEventArgs* args) { args->put_Handled(TRUE); })).Get(), &token));
        require_hr(web->add_PermissionRequested(Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(
            guarded(weak, generation, [](State&, auto*, ICoreWebView2PermissionRequestedEventArgs* args) { args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); })).Get(), &token));
        ComPtr<ICoreWebView2_4> web4; require_hr(web.As(&web4));
        require_hr(web4->add_DownloadStarting(Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
            guarded(weak, generation, [](State&, auto*, ICoreWebView2DownloadStartingEventArgs* args) { args->put_Cancel(TRUE); args->put_Handled(TRUE); })).Get(), &token));
        require_hr(web->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL));
        require_hr(web->add_WebResourceRequested(Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            guarded(weak, generation, [](State& s, auto*, ICoreWebView2WebResourceRequestedEventArgs* args) {
                ComPtr<ICoreWebView2WebResourceRequest> request; require_hr(args->get_Request(&request));
                LPWSTR uri{}; require_hr(request->get_Uri(&uri)); const std::wstring text = uri ? uri : L""; CoTaskMemFree(uri);
                if (text != s.owned_document && !std::static_pointer_cast<WebContent>(s.model)->allows(text)) {
                    ComPtr<ICoreWebView2WebResourceResponse> response;
                    require_hr(s.environment->CreateWebResourceResponse(nullptr, 403, L"Blocked by XUI origin policy", L"", &response));
                    require_hr(args->put_Response(response.Get()));
                }
            })).Get(), &token));
        require_hr(controller->add_MoveFocusRequested(Microsoft::WRL::Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
            guarded(weak, generation, [](State& s, auto*, ICoreWebView2MoveFocusRequestedEventArgs* args) {
                COREWEBVIEW2_MOVE_FOCUS_REASON reason{}; args->get_Reason(&reason); args->put_Handled(TRUE);
                const auto before = s.epoch; SetFocus(s.parent);
                if (!s.disposed && s.epoch == before)
                    PostMessageW(s.parent, WM_NEXTDLGCTL, reason == COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS, FALSE);
            })).Get(), &token));
        require_hr(web->add_ProcessFailed(Microsoft::WRL::Callback<ICoreWebView2ProcessFailedEventHandler>(
            guarded(weak, generation, [](State& s, auto*, auto*) { s.close(); s.model->publish_state(HostState::error, L"The web runtime process failed."); })).Get(), &token));
        RECT bounds{}; GetClientRect(visual, &bounds); require_hr(controller->put_Bounds(bounds));
        require_hr(controller->put_IsVisible(TRUE)); ShowWindow(visual, SW_SHOWNA);
        navigate_current();
    }
    void navigate_current() {
        auto content = std::static_pointer_cast<WebContent>(model);
        stopping = false;
        owned_navigation_pending = content->is_html();
        if (content->is_html()) {
            const std::wstring policy = L"<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; frame-src 'none'; worker-src 'none'; form-action 'none'; base-uri 'none'; object-src 'none'\">";
            require_hr(web->NavigateToString((policy + content->content()).c_str()));
        } else require_hr(web->Navigate(content->content().c_str()));
    }
    void web_command(WebOperation op) {
        auto content = std::static_pointer_cast<WebContent>(model);
        if (op == WebOperation::unload) { close(); content->publish_state(HostState::idle, L"Not loaded"); return; }
        if (op == WebOperation::load) {
            if (!shown) return;
            if (can_activate && !can_activate()) { content->publish_state(HostState::error, L"Dismiss XUI overlays before loading web content."); return; }
            close(); revision = content->revision();
            if (content->profile_root().empty()) { content->publish_state(HostState::error, L"Set an explicit local profile root before loading web content."); return; }
            static std::atomic<std::uint64_t> sequence{};
            if (pending_environments.size() >= 8) { content->publish_state(HostState::error, L"Eight native web creation requests are pending. Wait before loading again."); return; }
            if (retired_profiles.size() >= 8) { content->publish_state(HostState::error, L"Eight retired web profiles remain locked. Wait for the owned runtimes to exit before loading again."); return; }
            std::filesystem::create_directories(content->profile_root());
            for (int attempt = 0; attempt < 32; ++attempt) {
                const auto candidate = std::filesystem::path(content->profile_root()) /
                    (L"xui-web-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence));
                if (std::filesystem::create_directory(candidate)) { profile = candidate; break; }
            }
            if (profile.empty()) { content->publish_state(HostState::error, L"An owned web profile directory is unavailable."); return; }
            profile_lifetime = std::make_shared<WebProfile>();
            profile_lifetime->path = profile;
            web_profiles.push_back(profile_lifetime);
            const auto lifetime = profile_lifetime;
            has_request = true;
            const auto weak = weak_from_this(); const auto generation = epoch;
            pending_environments.insert(generation);
            ++lifetime->pending;
            const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.c_str(), nullptr,
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [weak, generation, lifetime](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                        --lifetime->pending;
                        auto self = weak.lock();
                        if (!self) return S_OK;
                        if (self->disposed || self->epoch != generation) { self->pending_environments.erase(generation); return S_OK; }
                        self->guard([&] {
                            if (FAILED(result) || !env) { self->pending_environments.erase(generation); self->close(); self->model->publish_state(HostState::error, L"Microsoft Edge WebView2 runtime is unavailable: " + failure(result)); return; }
                            lifetime->observe_environment(env);
                            self->environment = env;
                            ++lifetime->pending;
                            const auto created = env->CreateCoreWebView2Controller(self->visual,
                                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                    [weak, generation, lifetime](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                        --lifetime->pending;
                                        lifetime->observe(controller);
                                        if (!controller) lifetime->release_environment();
                                        auto self = weak.lock();
                                        if (self) self->pending_environments.erase(generation);
                                        if (!self || self->disposed || self->epoch != generation) { if (controller) controller->Close(); return S_OK; }
                                        self->guard([&] {
                                            if (FAILED(result) || !controller) { self->close(); self->model->publish_state(HostState::error, failure(result)); return; }
                                            self->web_ready(controller);
                                        }); return S_OK;
                                    }).Get());
                            if (FAILED(created)) {
                                --lifetime->pending; lifetime->release_environment();
                                self->pending_environments.erase(generation);
                            }
                            require_hr(created);
                        }); return S_OK;
                    }).Get());
            if (FAILED(hr)) { --lifetime->pending; pending_environments.erase(generation); }
            if (disposed || epoch != generation) return;
            if (FAILED(hr)) { close(); content->publish_state(HostState::error, L"Microsoft Edge WebView2 runtime is unavailable: " + failure(hr)); return; }
            content->publish_state(HostState::loading, L"Create optional WebView2 runtime");
            return;
        }
        if (op == WebOperation::stop && !web) {
            close(); revision = content->revision();
            content->publish_state(HostState::stopped, L"Web loading stopped"); return;
        }
        if (op == WebOperation::reload && !web) {
            if (content->has_source()) web_command(WebOperation::load);
            else content->publish_state(HostState::error, L"Load web content before requesting a reload.");
            return;
        }
        if (!web) return;
        auto current = web; auto current_controller = controller; const auto generation = epoch;
        if (op == WebOperation::stop) {
            stopping = true; navigation = 0; require_hr(current->Stop());
            if (!disposed && epoch == generation) content->publish_state(HostState::stopped, L"Web loading stopped");
        }
        else if (op == WebOperation::reload) { stopping = false; if (content->is_html()) navigate_current(); else require_hr(current->Reload()); }
        else if (op == WebOperation::focus) require_hr(current_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC));
    }
    void evaluate(std::wstring script, std::function<void(std::wstring, std::wstring)> callback) {
        if (!web || scripts.size() >= 8) { callback({}, L"Web engine is not ready or eight scripts are pending."); return; }
        auto weak = weak_from_this(); const auto generation = epoch, id = ++script_id;
        scripts.emplace(id, std::move(callback));
        auto current = web;
        const auto hr = current->ExecuteScript(script.c_str(), Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [weak, generation, id](HRESULT result, LPCWSTR json) -> HRESULT {
                if (auto self = weak.lock(); self && !self->disposed && self->epoch == generation) self->guard([&] {
                    auto entry = self->scripts.extract(id); if (entry.empty()) return;
                    std::wstring text = json ? json : L"";
                    auto callback = std::move(entry.mapped());
                    if (FAILED(result) || text.size() > 65536) callback({}, L"Web script failed or its result exceeded 65536 units.");
                    else callback(std::move(text), {});
                }); return S_OK;
            }).Get());
        if (FAILED(hr)) { auto entry = scripts.extract(id); if (!entry.empty()) entry.mapped()({}, failure(hr)); }
    }
#else
    void web_command(WebOperation operation) {
        if (operation == WebOperation::unload) { close(); model->publish_state(HostState::idle, L"Not loaded"); }
        else model->publish_state(HostState::error, L"WebView2 is disabled. Build with XUI_ENABLE_WEBVIEW2 and the pinned SDK.");
    }
    void evaluate(std::wstring, std::function<void(std::wstring, std::wstring)> callback) { callback({}, L"WebView2 is disabled in this build."); }
#endif
    void resize(UINT dpi) {
        const auto b = model->visual_bounds(); const float scale = dpi / 96.0f;
        const RECT bounds{static_cast<LONG>(std::lround(b.x * scale)), static_cast<LONG>(std::lround(b.y * scale)),
            static_cast<LONG>(std::lround((b.x + b.width) * scale)), static_cast<LONG>(std::lround((b.y + b.height) * scale))};
        const bool changed = !placed || !EqualRect(&bounds, &placed_bounds);
        const auto* style = model->effective_control_style_values(StylePart::root);
        const auto outer = model->bounds();
        HIGHCONTRASTW contrast{sizeof(contrast)};
        const bool high_contrast = style && SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON);
        const float radius = style && !high_contrast ? style->corner_radius.value_or(
            model->visual_style() == VisualStyle::winui ? 4.0f : 0.0f) : 0.0f;
        const int clip_radius = static_cast<int>(std::lround(std::min({radius, outer.width / 2, outer.height / 2}) * scale));
        const RECT clip{-bounds.left, -bounds.top,
            static_cast<LONG>(std::lround(outer.width * scale)) - bounds.left,
            static_cast<LONG>(std::lround(outer.height * scale)) - bounds.top};
        if (clip_radius != placed_radius || (clip_radius && !EqualRect(&clip, &placed_clip))) {
            HRGN region = clip_radius ? CreateRoundRectRgn(clip.left, clip.top, clip.right + 1, clip.bottom + 1,
                clip_radius * 2, clip_radius * 2) : nullptr;
            if ((clip_radius && !region) || !SetWindowRgn(visual, region, FALSE)) {
                if (region) DeleteObject(region);
                throw std::runtime_error("Cannot clip the owned runtime surface");
            }
            placed_radius = clip_radius; placed_clip = clip;
        }
        if (changed) {
            SetWindowPos(visual, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                SWP_NOACTIVATE | SWP_NOREDRAW);
            placed_bounds = bounds; placed = true;
        }
#ifdef XUI_ENABLE_WEBVIEW2
        if (controller) {
            const RECT content{0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
            if (changed) controller->put_Bounds(content);
            controller->NotifyParentWindowPositionChanged();
        }
#endif
        if (player && changed) player->UpdateVideo();
    }
};
NativeRuntimeHost::NativeRuntimeHost(std::shared_ptr<RuntimeHost> model, HWND parent, std::function<void()> failed, std::function<bool()> can_activate)
    : state_(std::make_shared<State>()) {
    auto& s = *state_; s.model = std::move(model); s.parent = parent; s.failed = std::move(failed); s.can_activate = std::move(can_activate);
    s.visual = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | SS_BLACKRECT,
        0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!s.visual) throw std::runtime_error("Cannot create runtime visual");
    auto weak = std::weak_ptr<State>(state_);
    if (auto media = std::dynamic_pointer_cast<MediaPlayback>(s.model))
        media->bind([weak](MediaOperation op, double value) { if (auto self = weak.lock()) self->guard([&] { self->media_command(op, value); }); });
    if (auto web = std::dynamic_pointer_cast<WebContent>(s.model))
        web->bind([weak](WebOperation op) { if (auto self = weak.lock()) self->guard([&] { self->web_command(op); }); },
            [weak](std::wstring script, std::function<void(std::wstring, std::wstring)> callback) {
                if (auto self = weak.lock()) self->guard([&] { self->evaluate(std::move(script), std::move(callback)); });
            });
    if (!SetWindowSubclass(s.visual, State::visual_procedure, 1, reinterpret_cast<DWORD_PTR>(&s)))
        throw std::runtime_error("Cannot attach native video repaint handling");
}
NativeRuntimeHost::~NativeRuntimeHost() {
    auto s = std::move(state_); s->disposed = true; s->close();
    s->model->detached();
    if (auto media = std::dynamic_pointer_cast<MediaPlayback>(s->model)) media->bind({});
    if (auto web = std::dynamic_pointer_cast<WebContent>(s->model)) web->bind({}, {});
    if (IsWindow(s->visual)) DestroyWindow(s->visual);
}
void NativeRuntimeHost::sync(bool visible, UINT dpi) {
    auto s = state_;
    s->guard([&] {
        s->resize(dpi);
        if (!visible || (s->can_activate && !s->can_activate())) { suspend(); return; }
        s->shown = true;
        if (auto media = std::dynamic_pointer_cast<MediaPlayback>(s->model); media && s->revision != media->revision()) {
            s->revision = media->revision(); if (!media->source().empty()) s->media_command(MediaOperation::load, 0);
        }
        if (auto web = std::dynamic_pointer_cast<WebContent>(s->model); web && s->revision != web->revision()) {
            s->revision = web->revision(); if (web->has_source()) s->web_command(WebOperation::load);
        }
    });
}
void NativeRuntimeHost::event() { auto s = state_; s->guard([&] { s->media_events(); }); }
void NativeRuntimeHost::suspend() {
    auto s = state_; s->shown = false;
    if (s->active()) { s->close(); s->model->publish_state(HostState::suspended, L"Unloaded while hidden. Load explicitly to resume."); }
}
void NativeRuntimeHost::cancel_owner() {
    auto s = state_; s->disposed = true; ++s->epoch;
    if (s->mailbox) {
        std::lock_guard lock(s->mailbox->mutex);
        s->mailbox->alive = false; s->mailbox->events.clear();
    }
#ifdef XUI_ENABLE_WEBVIEW2
    s->scripts.clear();
#endif
    s->model->detached();
}
bool NativeRuntimeHost::active() const { return state_->active(); }
bool NativeRuntimeHost::drain_shutdown() {
#ifdef XUI_ENABLE_WEBVIEW2
    const auto deadline = GetTickCount64() + 30000;
    // Completion handlers run on the creating STA, including after its HWND is
    // destroyed. Keep COM alive until callbacks and owned browser processes exit.
    while (!web_profiles.empty()) {
        collect_web_profiles();
        if (web_profiles.empty()) break;
        if (GetTickCount64() >= deadline) {
            for (const auto& profile : web_profiles)
                std::fwprintf(stderr, L"Owned web shutdown: %ls pending=%u retired=%d collection_exited=%d process_wait=%lu\n",
                    profile->path.c_str(), profile->pending, profile->retired, profile->exited,
                    profile->process ? WaitForSingleObject(profile->process, 0) : 0);
            return false;
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (GetTickCount64() >= deadline) break;
        }
    }
#endif
    return true;
}
bool NativeRuntimeHost::contains_native(HWND window) const {
    return state_->visual && (window == state_->visual || IsChild(state_->visual, window));
}
}

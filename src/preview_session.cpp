#include "preview_session.hpp"
#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <new>
#include <condition_variable>

namespace xui::preview {
namespace {
std::atomic<unsigned> sessions{};
std::atomic<bool> circuit_open{};
std::mutex retirement_mutex;
std::condition_variable retired;
void win32(bool value) {
    if (!value) throw Failure{PreviewReason::broker_failed, PreviewPhase::activation, HRESULT_FROM_WIN32(GetLastError())};
}
void deliver(const std::shared_ptr<Session>& session, PreviewStatus status) {
    std::lock_guard lock(session->mutex);
    if (session->alive) session->result = status;
}
struct Mapping {
    Channel* channel{};
    ~Mapping() { if (channel) UnmapViewOfFile(channel); }
};
struct Attributes {
    std::vector<BYTE> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list{};
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
};
void supervise(std::shared_ptr<Session> session, std::wstring helper, std::wstring path,
    RECT rect, std::uint64_t generation) {
    struct Count { ~Count() { std::lock_guard lock(retirement_mutex); --sessions; retired.notify_all(); } } count;
    PreviewStatus status{generation, PreviewState::loading};
    Handle process, job;
    try {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, sizeof(Channel), nullptr));
        Handle request(CreateEventW(&security, FALSE, FALSE, nullptr));
        Handle response(CreateEventW(&security, FALSE, FALSE, nullptr));
        win32(mapping && request && response);
        Mapping shared{static_cast<Channel*>(MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Channel)))};
        win32(shared.channel != nullptr);
        auto& channel = *new (shared.channel) Channel;
        channel.generation = generation;
        channel.rect = rect;
        std::copy(path.begin(), path.end(), channel.path);
        job.reset(CreateJobObjectW(nullptr, nullptr));
        win32(bool(job));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY |
            JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        limits.ProcessMemoryLimit = 256ull * 1024 * 1024;
        win32(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE);
        Attributes attributes;
        SIZE_T bytes{};
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        attributes.storage.resize(bytes);
        attributes.list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.storage.data());
        if (!InitializeProcThreadAttributeList(attributes.list, 1, 0, &bytes)) { attributes.list = nullptr; win32(false); }
        HANDLE inherited[]{mapping.value, request.value, response.value};
        win32(UpdateProcThreadAttribute(attributes.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr) != FALSE);
        wchar_t arguments[160]{};
        swprintf_s(arguments, L" %llx %llx %llx", reinterpret_cast<unsigned long long>(mapping.value),
            reinterpret_cast<unsigned long long>(request.value), reinterpret_cast<unsigned long long>(response.value));
        auto command = L"\"" + helper + L"\"" + arguments;
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes.list;
        PROCESS_INFORMATION created{};
        win32(CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
            &startup.StartupInfo, &created) != FALSE);
        process.reset(created.hProcess);
        { std::lock_guard lock(session->mutex); session->broker_process = created.dwProcessId; }
        Handle thread(created.hThread);
        if (!AssignProcessToJobObject(job.value, process.value)) {
            const auto error = GetLastError();
            TerminateProcess(process.value, error);
            throw Failure{PreviewReason::broker_failed, PreviewPhase::activation, HRESULT_FROM_WIN32(error)};
        }
        win32(ResumeThread(thread.value) != static_cast<DWORD>(-1));
        LONG issued = 1;
        InterlockedExchange(&channel.issued, issued);
        win32(SetEvent(request.value) != FALSE);
        auto deadline = GetTickCount64() + startup_ms;
        bool pending = true, retiring = false;
        auto heartbeat = GetTickCount64();
        RECT placed = rect;
        for (;;) {
            HANDLE waits[]{process.value, response.value};
            const auto wait = WaitForMultipleObjects(2, waits, FALSE, 15);
            if (wait == WAIT_FAILED) win32(false);
            if (wait == WAIT_OBJECT_0) {
                // Process exit makes the final packet stable, including a caption
                // close that races a newly issued heartbeat.
                if (channel.status.generation == generation) status = channel.status;
                { std::lock_guard lock(session->mutex); session->provider = channel.provider; }
                if (status.cleanup != PreviewCleanup::unloaded) {
                    DWORD code{}; GetExitCodeProcess(process.value, &code);
                    if (status.state != PreviewState::failed)
                        status = {generation, PreviewState::failed, PreviewReason::broker_failed,
                            static_cast<PreviewPhase>(channel.phase), HRESULT_FROM_WIN32(code ? code : ERROR_PROCESS_ABORTED)};
                    status.cleanup = PreviewCleanup::provider_unknown;
                    if (channel.phase >= static_cast<LONG>(PreviewPhase::activation)) circuit_open = true;
                }
                break;
            }
            const auto actions = InterlockedExchange(&channel.focus_actions, 0);
            if (actions) {
                std::lock_guard lock(session->mutex);
                if (session->alive) session->focus_actions |= actions;
            }
            if (pending && InterlockedCompareExchange(&channel.completed, 0, 0) == issued) {
                pending = false;
                { std::lock_guard lock(session->mutex); session->provider = channel.provider; }
                status = channel.status;
                if (!retiring) deliver(session, status);
                heartbeat = GetTickCount64() + 500;
                if (retiring) { deadline = GetTickCount64() + retire_ms; }
            }
            bool cancel{};
            std::optional<bool> focus;
            {
                std::lock_guard lock(session->mutex);
                cancel = session->cancel;
                rect = session->rect;
                if (!pending) { focus = session->focus; session->focus.reset(); }
            }
            if (cancel && !retiring && pending) {
                retiring = true;
                deadline = std::min(deadline, GetTickCount64() + retire_ms);
            }
            if ((pending || retiring) && GetTickCount64() >= deadline) {
                if (!cancel) status = {generation, PreviewState::failed, PreviewReason::timed_out,
                    static_cast<PreviewPhase>(InterlockedCompareExchange(&channel.phase, 0, 0)),
                    HRESULT_FROM_WIN32(WAIT_TIMEOUT), PreviewCleanup::provider_unknown};
                else status.cleanup = PreviewCleanup::provider_unknown;
                circuit_open = true;
                win32(TerminateJobObject(job.value, WAIT_TIMEOUT) != FALSE);
                WaitForSingleObject(process.value, retire_ms);
                break;
            }
            if (pending) continue;
            Command next{};
            if (cancel || status.state == PreviewState::failed || status.state == PreviewState::unsupported) next = Command::unload;
            else if (focus) { next = Command::focus; channel.reverse = *focus ? 1 : 0; }
            else if (!EqualRect(&rect, &placed)) { next = Command::resize; channel.rect = rect; placed = rect; }
            else if (GetTickCount64() >= heartbeat) next = Command::ping;
            else continue;
            if (next == Command::unload && retiring && channel.command == Command::unload) continue;
            channel.command = next;
            if (next == Command::unload) retiring = true;
            InterlockedExchange(&channel.issued, ++issued);
            win32(SetEvent(request.value) != FALSE);
            pending = true;
            deadline = GetTickCount64() + (retiring ? retire_ms : operation_ms);
        }
    } catch (const Failure& failure) {
        status = {generation, PreviewState::failed, failure.reason, failure.phase, failure.hr,
            process ? PreviewCleanup::provider_unknown : PreviewCleanup::none};
    } catch (const std::bad_alloc&) {
        status = {generation, PreviewState::failed, PreviewReason::resource_limit, PreviewPhase::activation, E_OUTOFMEMORY};
    } catch (...) {
        status = {generation, PreviewState::failed, PreviewReason::broker_failed, PreviewPhase::activation, E_UNEXPECTED};
    }
    // The private job owns only our helper, never SCM-created or shared provider processes.
    job.reset();
    deliver(session, status);
}
}
std::wstring preview_helper_path() {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&preview_helper_path), &module)) throw std::runtime_error("Cannot locate the preview module");
    std::array<wchar_t, max_path> path{};
    auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) throw std::runtime_error("Cannot locate the preview helper directory");
    std::wstring result(path.data(), count);
    result.resize(result.find_last_of(L'\\') + 1);
    return result + L"xui_preview_host.exe";
}
std::shared_ptr<Session> start_session(std::wstring helper, std::wstring path, RECT rect, std::uint64_t generation) {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&start_session), &module)) throw std::runtime_error("Cannot retain preview worker code");
    auto session = std::make_shared<Session>();
    session->rect = rect;
    if (path.size() >= max_path) throw std::invalid_argument("Preview source path is too long");
    if (circuit_open) {
        session->result = PreviewStatus{generation, PreviewState::failed, PreviewReason::resource_limit,
            PreviewPhase::activation, HRESULT_FROM_WIN32(ERROR_BUSY)};
        return session;
    }
    if (sessions.fetch_add(1) >= max_sessions) {
        --sessions;
        session->result = PreviewStatus{generation, PreviewState::failed, PreviewReason::resource_limit,
            PreviewPhase::activation, HRESULT_FROM_WIN32(ERROR_BUSY)};
        return session;
    }
    try { std::thread(supervise, session, std::move(helper), std::move(path), rect, generation).detach(); }
    catch (...) { --sessions; throw; }
    return session;
}
bool drain_sessions(unsigned timeout_ms) {
    std::unique_lock lock(retirement_mutex);
    return retired.wait_for(lock, std::chrono::milliseconds(timeout_ms), [] { return sessions.load() == 0; });
}
}

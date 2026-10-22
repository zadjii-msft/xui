#include "private_desktop.hpp"
#include <string_view>
#include <vector>

namespace {
std::wstring quote(std::wstring_view argument) {
    std::wstring result = L"\"";
    size_t slashes{};
    for (const auto ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
std::wstring object_name(HANDLE object) {
    DWORD bytes{};
    GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &bytes);
    std::vector<wchar_t> name(bytes / sizeof(wchar_t));
    xui::test::desktop_require(bytes && GetUserObjectInformationW(object, UOI_NAME, name.data(), bytes, &bytes),
        "Read private desktop identity");
    return name.data();
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        xui::test::desktop_require(argc >= 2, "Usage: xui_headless_runner <executable> [arguments...]");
        xui::test::PrivateDesktop desktop;
        USEROBJECTFLAGS flags{};
        DWORD bytes{};
        xui::test::desktop_require(GetUserObjectInformationW(desktop.station, UOI_FLAGS, &flags, sizeof(flags), &bytes)
            && !(flags.dwFlags & WSF_VISIBLE), "Headless tests require a noninteractive window station");
        auto identity = object_name(desktop.station) + L"\\" + object_name(desktop.desktop);
        std::wstring command;
        for (int index = 1; index < argc; ++index) {
            if (index > 1) command += L' ';
            command += quote(argv[index]);
        }
        std::cout << "Running on a noninteractive window station.\n" << std::flush;
        STARTUPINFOW startup{sizeof(startup)};
        startup.lpDesktop = identity.data();
        PROCESS_INFORMATION process{};
        const auto job = CreateJobObjectW(nullptr, nullptr);
        xui::test::desktop_require(job != nullptr, "Create headless process lifetime");
        struct Job { HANDLE value; ~Job() { CloseHandle(value); } } lifetime{job};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        xui::test::desktop_require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
            "Bound headless process tree");
        xui::test::desktop_require(CreateProcessW(argv[1], command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED, nullptr, nullptr, &startup, &process), "Start headless command");
        const bool assigned = AssignProcessToJobObject(job, process.hProcess) != FALSE;
        const bool resumed = assigned && ResumeThread(process.hThread) != static_cast<DWORD>(-1);
        if (!resumed) TerminateProcess(process.hProcess, 1);
        const auto stopped = WaitForSingleObject(process.hProcess, 15 * 60 * 1000);
        if (stopped != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        DWORD result{1};
        const bool read = GetExitCodeProcess(process.hProcess, &result) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        xui::test::desktop_require(resumed && stopped == WAIT_OBJECT_0 && read, "Headless command completed");
        return static_cast<int>(result);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#pragma once
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace xui::test {
inline void desktop_require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct PrivateDesktop {
    HWINSTA original_station{GetProcessWindowStation()}, station{};
    HDESK original_desktop{GetThreadDesktop(GetCurrentThreadId())}, desktop{};
    PrivateDesktop() {
        try {
            station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
            desktop_require(station && SetProcessWindowStation(station), "Create private clipboard window station");
            desktop = CreateDesktopW(L"XuiClipboard", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
            desktop_require(desktop && SetThreadDesktop(desktop), "Create private clipboard desktop");
        } catch (...) { release(); throw; }
    }
    ~PrivateDesktop() { release(); }
    void release() {
        if (!SetThreadDesktop(original_desktop) || !SetProcessWindowStation(original_station))
            std::cerr << "Cannot restore the isolated test desktop: " << GetLastError() << '\n';
        if (desktop && !CloseDesktop(desktop)) std::cerr << "Cannot close the isolated test desktop\n";
        if (station && !CloseWindowStation(station)) std::cerr << "Cannot close the isolated test station\n";
    }
};
inline void isolated_process(const wchar_t* argument, DWORD timeout = 20000) {
    const auto before = GetClipboardSequenceNumber();
    wchar_t executable[32768]{};
    desktop_require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Locate clipboard subprocess");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" " + argument;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const auto job = CreateJobObjectW(nullptr, nullptr);
    desktop_require(job != nullptr, "Create isolated-test process lifetime");
    struct Job { HANDLE value; ~Job() { CloseHandle(value); } } lifetime{job};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    desktop_require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != 0,
        "Bound the isolated test process tree");
    desktop_require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startup, &process) != 0,
        "Start private clipboard subprocess");
    const bool assigned = AssignProcessToJobObject(job, process.hProcess) != FALSE;
    const bool resumed = assigned && ResumeThread(process.hThread) != static_cast<DWORD>(-1);
    if (!resumed) TerminateProcess(process.hProcess, 1);
    const auto stopped = WaitForSingleObject(process.hProcess, timeout);
    if (stopped == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
    DWORD result{1};
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    desktop_require(resumed && stopped == WAIT_OBJECT_0 && result == 0, "Isolated Windows integration checks pass");
    desktop_require(GetClipboardSequenceNumber() == before, "Clipboard tests leave the interactive clipboard unchanged");
}
}

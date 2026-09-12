#pragma once
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

namespace resource_probe {
inline void heaps() {
    std::vector<HANDLE> handles(GetProcessHeaps(0, nullptr));
    const auto count = GetProcessHeaps(static_cast<DWORD>(handles.size()), handles.data());
    if (count > handles.size()) return;
    size_t busy{}, committed{}, unsupported{};
    for (DWORD i = 0; i < count; ++i) {
        if (!HeapLock(handles[i])) { ++unsupported; continue; }
        PROCESS_HEAP_ENTRY entry{};
        while (HeapWalk(handles[i], &entry)) {
            if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) busy += entry.cbData;
            if (entry.wFlags & PROCESS_HEAP_REGION) committed += entry.Region.dwCommittedSize;
        }
        if (GetLastError() != ERROR_NO_MORE_ITEMS) ++unsupported;
        HeapUnlock(handles[i]);
    }
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    std::cout << "heap_busy_bytes=" << busy << " heap_region_commit_bytes=" << committed
              << " heaps=" << count << " unsupported_heaps=" << unsupported
              << " private_bytes=" << memory.PrivateUsage << " working_set_bytes=" << memory.WorkingSetSize << '\n';
}
inline std::vector<HANDLE> handle_difference(const std::vector<HANDLE>& previous) {
    using Query = LONG(WINAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
    const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    const auto object = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
    if (!query || !object) return {};
    struct Entry { HANDLE handle; ULONG_PTR count, pointers; ULONG access, type, attributes, reserved; };
    struct Snapshot { ULONG_PTR count, reserved; Entry entries[1]; };
    std::vector<std::max_align_t> storage(1024);
    ULONG needed{};
    LONG result{};
    do {
        result = query(GetCurrentProcess(), 51, storage.data(), static_cast<ULONG>(storage.size() * sizeof(std::max_align_t)), &needed);
        if (storage.size() > 1024 * 1024) return {};
        if (result < 0) storage.resize(std::max(storage.size() * 2, needed / sizeof(std::max_align_t) + 1));
    } while (result == static_cast<LONG>(0xC0000004));
    if (result < 0) return {};
    const auto* snapshot = reinterpret_cast<const Snapshot*>(storage.data());
    std::vector<HANDLE> handles;
    for (ULONG_PTR i = 0; i < snapshot->count; ++i) {
        const auto handle = snapshot->entries[i].handle;
        handles.push_back(handle);
        if (previous.empty() || std::find(previous.begin(), previous.end(), handle) != previous.end()) continue;
        struct Text { USHORT length, capacity; wchar_t* text; };
        alignas(std::max_align_t) char buffer[2048]{};
        if (object(handle, 2, buffer, sizeof(buffer), &needed) >= 0) {
            const auto& type = *reinterpret_cast<const Text*>(buffer);
            std::wcout << L"new_handle=" << handle << L" type=" << std::wstring_view(type.text, type.length / sizeof(wchar_t));
            if (std::wstring_view(type.text, type.length / sizeof(wchar_t)) == L"Event" &&
                object(handle, 1, buffer, sizeof(buffer), &needed) >= 0) {
                const auto& name = *reinterpret_cast<const Text*>(buffer);
                if (name.length) std::wcout << L" name=" << std::wstring_view(name.text, name.length / sizeof(wchar_t));
            }
            std::wcout << L'\n';
        }
    }
    return handles;
}
}

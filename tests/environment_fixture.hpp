#pragma once
#include <windows.h>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <string>

struct EnvironmentFixture {
    std::wstring name;
    std::optional<std::wstring> previous;
    explicit EnvironmentFixture(const wchar_t* value) {
        static std::atomic<unsigned> next{};
        name = L"XUI_TEST_日本_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++next);
        SetLastError(ERROR_SUCCESS);
        const auto size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
        if (size) {
            std::wstring old(size, L'\0');
            old.resize(GetEnvironmentVariableW(name.c_str(), old.data(), size));
            previous = std::move(old);
        } else if (GetLastError() == ERROR_SUCCESS) previous = L"";
        if (!SetEnvironmentVariableW(name.c_str(), value)) throw std::runtime_error("Set owned test environment variable");
    }
    ~EnvironmentFixture() { SetEnvironmentVariableW(name.c_str(), previous ? previous->c_str() : nullptr); }
    EnvironmentFixture(const EnvironmentFixture&) = delete;
    std::wstring reference() const { return L"%" + name + L"%"; }
};

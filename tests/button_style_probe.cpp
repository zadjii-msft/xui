#include "xui/controls.hpp"
#include "xui/collections.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
std::atomic<size_t> allocations{};
void* operator new(size_t n) { ++allocations; if (auto p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
int main() {
    std::cout << "sizeof_control=" << sizeof(xui::Control) << " sizeof_button=" << sizeof(xui::Button)
        << " sizeof_items_view=" << sizeof(xui::ItemsView) << '\n';
    auto start = std::chrono::steady_clock::now();
    auto before = allocations.load();
    size_t checksum{};
    for (size_t i = 0; i < 100000; ++i) {
        xui::Button b(L"Delete");
        b.set_text_measurer([](auto, auto) { return xui::Size{40, 14}; });
        checksum += size_t(b.measure({1000, 1000}).width);
    }
    std::cout << "construct_100k_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()
        << " allocations=" << allocations.load()-before << '\n';
    xui::Button b(L"Delete");
    b.set_text_measurer([](auto, auto) { return xui::Size{40, 14}; });
    start = std::chrono::steady_clock::now(); before = allocations.load();
    for (size_t i = 0; i < 1000000; ++i) {
        b.pointer_move(i % 2 == 0);
        b.set_focused(i % 3 == 0);
        b.set_checked(i % 5 == 0);
        checksum += size_t(b.measure({1000, 1000}).width);
    }
    std::cout << "state_measure_1m_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()
        << " allocations=" << allocations.load()-before << " checksum=" << checksum << '\n';
    PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    std::cout << "private_bytes=" << counters.PrivateUsage << '\n';
}

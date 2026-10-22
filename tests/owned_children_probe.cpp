#include "owned_children_workload.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <windows.h>
#include <psapi.h>

namespace {
std::atomic<std::size_t> allocations{}, bytes{};
struct ThreadCpu { std::uint64_t kernel, user; };
ThreadCpu thread_cpu() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        throw std::runtime_error("Read probe traversal thread CPU time");
    const auto ticks = [](FILETIME value) { return (std::uint64_t{value.dwHighDateTime} << 32) | value.dwLowDateTime; };
    return {ticks(kernel), ticks(user)};
}
SIZE_T private_bytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        throw std::runtime_error("Read probe private memory");
    return counters.PrivateUsage;
}
}
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    bytes.fetch_add(size, std::memory_order_relaxed);
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    try {
        const auto before_tree_bytes = bytes.load();
        auto root = owned_workload::make_tree();
        const auto tree_bytes = bytes.load() - before_tree_bytes;
        std::size_t expected_visits{};
        const auto expected_sum = owned_workload::walk(*root, expected_visits);
        if (expected_visits != owned_workload::nodes) throw std::runtime_error("Probe workload cardinality");
        const auto memory = private_bytes(), before = allocations.load();
        std::uint64_t sum{};
        std::size_t visits{};
        const auto before_cpu = thread_cpu();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < owned_workload::iterations; ++i) sum += owned_workload::walk(*root, visits);
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto after_cpu = thread_cpu();
        const auto allocated = allocations.load() - before;
        if (allocated || visits != owned_workload::nodes * owned_workload::iterations ||
            sum != expected_sum * owned_workload::iterations) throw std::runtime_error("Probe exact work/allocation invariant");
        const auto after_memory = private_bytes();
        if (after_cpu.kernel < before_cpu.kernel || after_cpu.user < before_cpu.user)
            throw std::runtime_error("Probe traversal CPU time moved backwards");
        const auto cpu_ms = (after_cpu.kernel - before_cpu.kernel + after_cpu.user - before_cpu.user) / 10000.0;
        std::cout << "owned_walk nodes=" << expected_visits << " iterations=" << owned_workload::iterations
            << " visits=" << visits << " allocations=" << allocated << " elapsed_ms=" << elapsed
            << " thread_kernel_before_100ns=" << before_cpu.kernel << " thread_kernel_after_100ns=" << after_cpu.kernel
            << " thread_user_before_100ns=" << before_cpu.user << " thread_user_after_100ns=" << after_cpu.user
            << " thread_cpu_ms=" << cpu_ms
            << " tree_allocated_bytes=" << tree_bytes << " private_before=" << memory << " private_after=" << after_memory
            << " sizeof_element=" << sizeof(xui::Element) << " sizeof_control=" << sizeof(xui::Control)
            << " sizeof_split=" << sizeof(xui::SplitView) << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

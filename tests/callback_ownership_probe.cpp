#include "xui/foundation.hpp"
#include <windows.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <new>
#include <string_view>

namespace { thread_local std::uint64_t allocations{}, requested_bytes{}; }
void* operator new(std::size_t size) {
    if (auto* pointer = std::malloc(size ? size : 1)) {
        ++allocations; requested_bytes += size;
        return pointer;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::uint64_t ticks(FILETIME value) {
    return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
template<class F> void measure(const char* phase, unsigned iterations, bool measured, F&& action) {
    FILETIME creation{}, exit{}, kernel_before{}, user_before{}, kernel_after{}, user_after{};
    require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel_before, &user_before) != 0,
        "Read callback owner CPU");
    const auto count = allocations, bytes = requested_bytes;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned index = 0; index != iterations; ++index) action(index);
    const auto wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto used = allocations - count, used_bytes = requested_bytes - bytes;
    require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel_after, &user_after) != 0,
        "Read callback owner CPU after dispatch");
    std::cout << "{\"phase\":\"" << phase << "\",\"iterations\":" << iterations
        << ",\"measured\":" << (measured ? "true" : "false") << ",\"wall_ms\":" << (measured ? wall : 0)
        << ",\"kernel_before100ns\":" << ticks(kernel_before) << ",\"kernel_after100ns\":" << ticks(kernel_after)
        << ",\"user_before100ns\":" << ticks(user_before) << ",\"user_after100ns\":" << ticks(user_after)
        << ",\"cpp_allocations\":" << used << ",\"cpp_requested_bytes\":" << used_bytes << "}\n";
}
}
int main(int argc, char** argv) {
    bool measured{};
    if (argc == 2 && std::string_view(argv[1]) == "--measure") measured = true;
    else if (argc != 1) { std::cerr << "Usage: callback_ownership_probe [--measure]\n"; return 2; }
    try {
        std::cout << std::setprecision(17);
        using namespace xui;
        const unsigned actions = measured ? 134217728 : 256;
        const unsigned edits = measured ? 1048576 : 64;
        std::uint64_t callbacks{}, checksum{};
        const auto button_allocations = allocations, button_bytes = requested_bytes;
        Button action(L"Ordinary observed action");
        action.on_click([&] { ++callbacks; checksum += 7; });
        std::cout << "{\"phase\":\"button-setup\",\"cpp_allocations\":" << allocations - button_allocations
            << ",\"cpp_requested_bytes\":" << requested_bytes - button_bytes << "}\n";
        measure("observed-button", actions, measured, [&](unsigned) {
            require(action.invoke(), "Invoke every actual observed Button");
        });
        require(callbacks == actions && checksum == std::uint64_t(actions) * 7, "Exact observed Button work");
        std::cout << "{\"phase\":\"button-work\",\"callbacks\":" << callbacks << ",\"checksum\":" << checksum << "}\n";

        const auto numeric_allocations = allocations, numeric_bytes = requested_bytes;
        NumericInput number;
        number.set_range({0, 10000000, 1, 10});
        callbacks = checksum = 0;
        number.on_change([&](double value) { ++callbacks; checksum += static_cast<std::uint64_t>(value); });
        const auto step = number.increase_button();
        std::cout << "{\"phase\":\"numeric-setup\",\"cpp_allocations\":" << allocations - numeric_allocations
            << ",\"cpp_requested_bytes\":" << requested_bytes - numeric_bytes << "}\n";
        measure("composed-numeric-step", edits, measured, [&](unsigned) {
            require(step->invoke(), "Invoke every actual composite step Button");
        });
        require(number.value() == edits && callbacks == edits && checksum == std::uint64_t(edits) * (edits + 1) / 2,
            "Exact composite model, parent callbacks and checksum work");
        std::cout << "{\"phase\":\"numeric-work\",\"callbacks\":" << callbacks << ",\"checksum\":" << checksum
            << ",\"value\":" << number.value() << "}\n";
        std::cout << "{\"phase\":\"storage\",\"control\":" << sizeof(Control) << ",\"button\":" << sizeof(Button)
            << ",\"text_input\":" << sizeof(TextInput) << ",\"numeric\":" << sizeof(NumericInput)
            << ",\"combo\":" << sizeof(ComboBox) << "}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

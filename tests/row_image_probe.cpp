#include "images.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local bool measuring{};
thread_local std::size_t allocations{}, bytes{};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
void* operator new(std::size_t size) {
    if (auto* memory = std::malloc(size ? size : 1)) {
        if (measuring) { ++allocations; bytes += size; }
        return memory;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace xui {
struct RowImagesTestAccess {
    static void seed(RowImages& images, const std::vector<RowVisual>& rows) {
        images.clear();
        images.rows_ = rows;
        images.pixels_ = 24;
        for (const auto& row : rows) {
            auto slot = std::make_unique<RowImages::Slot>();
            slot->key = row.key;
            slot->path = row.visual.image_path;
            slot->kind = ImageKind::wic;
            slot->pixels_size = 24;
            auto pixels = std::make_shared<ImagePixels>();
            pixels->id = row.key.id;
            pixels->size = {24, 24};
            slot->pixels = std::move(pixels);
            images.slots_.push_back(std::move(slot));
        }
    }
};
}
int main() {
    try {
        for (std::size_t count : {8, 64, 512}) {
            std::vector<xui::RowVisual> rows;
            for (std::size_t i = 0; i != count; ++i)
                rows.push_back({{i + 1, 1}, {xui::ButtonIcon::none, L"C:\\synthetic\\row-" + std::to_wstring(i) + L".png"}});
            xui::RowImages images;
            xui::RowImagesTestAccess::seed(images, rows);
            std::vector<std::uint64_t> retained;
            retained.reserve(count);
            const unsigned frames = count == 8 ? 1048576 : count == 64 ? 262144 : 32768;
            allocations = bytes = 0;
            double milliseconds{};
            std::uint64_t checksum{};
            for (unsigned i = 0; i != frames; ++i) {
                auto input = rows;
                const auto first = i % count;
                std::rotate(input.begin(), input.begin() + first, input.end());
                retained.clear();
                measuring = true;
                const auto start = std::chrono::steady_clock::now();
                const auto changed = images.sync_visuals(std::move(input), 96, {}, retained);
                milliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                measuring = false;
                require(!changed && retained.size() == count && images.count() == count, "Exact bounded image slots");
                for (std::size_t j = 0; j != count; ++j)
                    require(retained[j] == (first + j) % count + 1, "Stable image identity in input order");
                checksum += retained.front();
            }
            require(!allocations && !bytes, "Image reconciliation must not allocate temporary storage");
            std::cout << "{\"phase\":\"row-images-" << count << "\",\"iterations\":" << frames
                << ",\"wall_ms\":" << milliseconds << ",\"cpp_allocations\":" << allocations
                << ",\"cpp_requested_bytes\":" << bytes << ",\"retained_slots\":" << images.count()
                << ",\"checksum\":" << checksum << "}\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

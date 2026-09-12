#pragma once

#include "xui/image.hpp"
#include <windows.h>
#include <vector>

namespace xui {
struct TaskWake;
class Drawing;
struct ImagePixels {
    std::uint64_t id{};
    ImageSize size{};
    std::vector<std::byte> pixels;
    std::size_t accounted{};
    ~ImagePixels();
};
struct ImageRequest {
    std::atomic<bool> cancelled{};
    std::mutex mutex;
    std::wstring path;
    ImageSize size;
    std::shared_ptr<TaskWake> wake;
    std::shared_ptr<const ImagePixels> pixels;
    std::wstring error;
    bool done{};
    void cancel();
};
struct ImagePeer {
    explicit ImagePeer(Image& value) : control(value) {}
    ~ImagePeer() { detach(); }
    Image& control;
    std::uint64_t revision{};
    bool visible{};
    std::shared_ptr<ImageRequest> request;
    std::shared_ptr<const ImagePixels> pixels;
    void sync(bool shown, const std::shared_ptr<TaskWake>& wake);
    bool deliver();
    void detach();
    void paint(Drawing& drawing, Rect bounds);
};
std::shared_ptr<ImageRequest> request_image(std::wstring path, ImageSize size, std::shared_ptr<TaskWake> wake);
void clear_image_cache();
bool reserve_bitmap(std::size_t bytes);
void finish_bitmap(std::size_t bytes, bool success, double milliseconds);
void release_bitmap(std::size_t bytes);
// Private deterministic test seam. The callback runs on the single decode worker.
enum class ImageDecodeStage { before_decode, reserved, before_delivery };
struct ImageDecodeTestAccess {
    static std::atomic<void(*)(ImageDecodeStage)> hook;
};
}

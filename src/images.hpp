#pragma once

#include "xui/image.hpp"
#include "xui/collections.hpp"
#include <windows.h>
#include <vector>

namespace xui {
struct TaskWake;
class Drawing;
enum class ImageKind { wic, shell };
ImageKind thumbnail_kind(std::wstring_view path, bool directory = false);
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
    ImageKind kind{ImageKind::wic};
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
struct RowVisual {
    ItemKey key;
    ItemVisual visual;
    bool directory{};
};
class RowImages {
public:
    static constexpr std::size_t maximum_rows = 512, maximum_images = 24;
    bool sync(std::shared_ptr<const CollectionIndex> source, std::vector<RowVisual> rows, UINT dpi,
        const std::shared_ptr<TaskWake>& wake, std::vector<std::uint64_t>& retained, std::size_t& remaining);
    void clear();
    ItemVisual visual(ItemKey key) const;
    std::shared_ptr<const ImagePixels> pixels(ItemKey key) const;
    std::size_t count() const { return slots_.size(); }
private:
    struct Slot {
        ItemKey key;
        std::wstring path;
        ImageKind kind{};
        std::shared_ptr<ImageRequest> request;
        std::shared_ptr<const ImagePixels> pixels;
        ~Slot() { if (request) request->cancel(); }
    };
    std::vector<std::unique_ptr<Slot>> slots_;
    std::vector<RowVisual> rows_;
    std::weak_ptr<const CollectionIndex> source_;
    UINT pixels_{};
};
std::shared_ptr<ImageRequest> request_image(std::wstring path, ImageSize size, std::shared_ptr<TaskWake> wake,
    ImageKind kind = ImageKind::wic);
void clear_image_cache();
bool reserve_bitmap(std::size_t bytes);
void finish_bitmap(std::size_t bytes, bool success, double milliseconds);
void release_bitmap(std::size_t bytes);
// Private deterministic test seams. Each callback runs on its respective decode worker.
enum class ImageDecodeStage { before_decode, reserved, before_delivery };
struct ImageDecodeTestAccess {
    static std::atomic<void(*)(ImageDecodeStage)> hook;
    static std::atomic<void(*)(ImageDecodeStage)> shell_hook;
};
}

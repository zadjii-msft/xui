#include "images.hpp"
#include "async.hpp"
#include "drawing.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <list>
#include <stdexcept>

namespace xui {
std::atomic<void(*)(ImageDecodeStage)> ImageDecodeTestAccess::hook{};
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
struct Accounting {
    std::mutex mutex;
    ImageStatistics stats;
};
Accounting& accounting() { static auto* value = new Accounting; return *value; }
void peak_cpu(ImageStatistics& s) { s.cpu_peak = std::max(s.cpu_peak, s.cpu_bytes + s.cpu_reserved); }
void peak_gpu(ImageStatistics& s) { s.gpu_peak = std::max(s.gpu_peak, s.gpu_bytes + s.gpu_reserved); }
struct Key {
    std::wstring path;
    DWORD volume{}, index_high{}, index_low{};
    std::uint64_t write{}, bytes{};
    ImageSize size{};
    bool operator==(const Key&) const = default;
};
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Cancelled {};
void checkpoint(const ImageRequest& r) { if (r.cancelled) throw Cancelled{}; }
void test_boundary(ImageDecodeStage stage) {
    if (const auto hook = ImageDecodeTestAccess::hook.load()) hook(stage);
}
void require(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(operation);
}
class Service {
    struct Entry { Key key; std::shared_ptr<const ImagePixels> pixels; };
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::shared_ptr<ImageRequest>> queue_;
    std::list<Entry> cache_;
    std::uint64_t epoch_{}, next_id_{};
    void prune() {
        for (auto it = queue_.begin(); it != queue_.end();) {
            if ((*it)->cancelled) {
                it = queue_.erase(it);
                auto& a = accounting(); std::lock_guard lock(a.mutex); ++a.stats.cancelled;
            }
            else ++it;
        }
    }
    void evict(bool all_unused) {
        auto& a = accounting();
        for (auto it = cache_.end(); it != cache_.begin();) {
            --it;
            if (it->pixels.use_count() != 1) continue;
            it = cache_.erase(it);
            { std::lock_guard lock(a.mutex); ++a.stats.evicted; }
            if (!all_unused) break;
        }
    }
    bool reserve(std::size_t bytes) {
        std::lock_guard lock(mutex_);
        auto& a = accounting();
        for (;;) {
            {
                std::lock_guard guard(a.mutex);
                if (bytes <= ImageLimits::cpu_bytes - a.stats.cpu_bytes - a.stats.cpu_reserved) {
                    a.stats.cpu_reserved += bytes;
                    peak_cpu(a.stats);
                    return true;
                }
            }
            const auto before = cache_.size();
            evict(false);
            if (cache_.size() == before) return false;
        }
    }
    std::shared_ptr<const ImagePixels> decode(ImageRequest& r, IWICImagingFactory* factory) {
        checkpoint(r);
        Handle file{CreateFileW(r.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open the image file.");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.value, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("Cannot read image file information.");
        Key key;
        key.bytes = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (!key.bytes || key.bytes > ImageLimits::file_bytes)
            throw std::runtime_error("The image file exceeds the 32 MiB limit or is empty.");
        const auto length = GetFinalPathNameByHandleW(file.value, nullptr, 0, FILE_NAME_NORMALIZED);
        if (!length || length > 32767) throw std::runtime_error("Cannot resolve the image path.");
        key.path.resize(length);
        const auto actual = GetFinalPathNameByHandleW(file.value, key.path.data(), length, FILE_NAME_NORMALIZED);
        if (!actual || actual >= length) throw std::runtime_error("Cannot resolve the image path.");
        key.path.resize(actual);
        key.volume = info.dwVolumeSerialNumber;
        key.index_high = info.nFileIndexHigh;
        key.index_low = info.nFileIndexLow;
        key.write = (std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
        key.size = r.size;
        checkpoint(r);
        std::uint64_t epoch{};
        {
            std::lock_guard lock(mutex_);
            epoch = epoch_;
            for (auto it = cache_.begin(); it != cache_.end(); ++it) if (it->key == key) {
                auto pixels = it->pixels;
                cache_.splice(cache_.begin(), cache_, it);
                auto& a = accounting();
                std::lock_guard guard(a.mutex);
                ++a.stats.cache_hits;
                return pixels;
            }
        }
        ComPtr<IWICBitmapDecoder> decoder;
        require(factory->CreateDecoderFromFileHandle(reinterpret_cast<ULONG_PTR>(file.value), nullptr,
            WICDecodeMetadataCacheOnDemand, &decoder), "Cannot decode the image file.");
        checkpoint(r);
        ComPtr<IWICBitmapFrameDecode> frame;
        require(decoder->GetFrame(0, &frame), "Cannot decode the first image frame.");
        UINT width{}, height{};
        require(frame->GetSize(&width, &height), "Cannot read image dimensions.");
        if (!width || !height || width > ImageLimits::source_dimension || height > ImageLimits::source_dimension ||
            std::uint64_t(width) * height > ImageLimits::source_pixels)
            throw std::runtime_error("The image exceeds the source dimension or 16-megapixel limit.");
        const double scale = std::min({1.0, double(r.size.width) / width, double(r.size.height) / height});
        ImageSize size{std::max(1u, static_cast<UINT>(width * scale)),
            std::max(1u, static_cast<UINT>(height * scale))};
        const auto bytes = std::size_t(size.width) * size.height * 4;
        if (!reserve(bytes)) throw std::runtime_error("The image pixel budget is full. Unload other images, then reload this image.");
        struct Reservation {
            std::size_t bytes;
            ~Reservation() {
                auto& a = accounting(); std::lock_guard lock(a.mutex); a.stats.cpu_reserved -= bytes;
            }
        } reservation{bytes};
        test_boundary(ImageDecodeStage::reserved);
        checkpoint(r);
        auto result = std::make_shared<ImagePixels>();
        result->size = size;
        result->pixels.resize(bytes);
        {
            auto& a = accounting(); std::lock_guard lock(a.mutex);
            a.stats.cpu_reserved -= bytes;
            reservation.bytes = 0;
            a.stats.cpu_bytes += bytes;
            result->accounted = bytes;
        }
        ComPtr<IWICBitmapSource> source = frame;
        ComPtr<IWICBitmapScaler> scaler;
        if (width != size.width || height != size.height) {
            require(factory->CreateBitmapScaler(&scaler), "Cannot create the image scaler.");
            require(scaler->Initialize(frame.Get(), size.width, size.height, WICBitmapInterpolationModeFant),
                "Cannot scale the image.");
            source = scaler;
        }
        checkpoint(r);
        ComPtr<IWICFormatConverter> converter;
        require(factory->CreateFormatConverter(&converter), "Cannot create the pixel converter.");
        require(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0, WICBitmapPaletteTypeCustom), "Cannot convert the image pixels.");
        checkpoint(r);
        require(converter->CopyPixels(nullptr, size.width * 4, static_cast<UINT>(bytes),
            reinterpret_cast<BYTE*>(result->pixels.data())), "Cannot read the image pixels.");
        checkpoint(r);
        {
            std::lock_guard lock(mutex_);
            result->id = ++next_id_;
            if (epoch == epoch_ && !r.cancelled) {
                while (cache_.size() >= ImageLimits::cache_entries) {
                    const auto before = cache_.size();
                    evict(false);
                    if (cache_.size() == before) break;
                }
                if (cache_.size() < ImageLimits::cache_entries) cache_.push_front({std::move(key), result});
            }
        }
        auto& a = accounting(); std::lock_guard lock(a.mutex); ++a.stats.decoded;
        return result;
    }
    void run() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            ComPtr<IWICImagingFactory> factory;
            const auto initialized = SUCCEEDED(com) ? CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)) : com;
            for (;;) {
                std::shared_ptr<ImageRequest> request;
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait(lock, [&] { return !queue_.empty(); });
                    request = std::move(queue_.front());
                    queue_.pop_front();
                    auto& a = accounting();
                    std::lock_guard guard(a.mutex);
                    a.stats.active = 1;
                }
                auto& a = accounting();
                const auto start = Clock::now();
                std::shared_ptr<const ImagePixels> pixels;
                std::wstring error;
                try {
                    test_boundary(ImageDecodeStage::before_decode);
                    checkpoint(*request);
                    require(initialized, "Cannot initialize Windows Imaging Component.");
                    pixels = decode(*request, factory.Get());
                } catch (const Cancelled&) {
                } catch (const std::exception& failure) {
                    const std::string text(failure.what());
                    error.assign(text.begin(), text.end());
                } catch (...) { error = L"Image decoding failed."; }
                const auto milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                {
                    std::lock_guard lock(a.mutex);
                    a.stats.decode_ms += milliseconds;
                    a.stats.decode_max_ms = std::max(a.stats.decode_max_ms, milliseconds);
                    if (!error.empty()) ++a.stats.rejected;
                }
                {
                    test_boundary(ImageDecodeStage::before_delivery);
                    std::lock_guard lock(request->mutex);
                    if (!request->cancelled) {
                        request->pixels = std::move(pixels);
                        request->error = std::move(error);
                        request->done = true;
                        if (request->wake) SetEvent(request->wake->event);
                    } else {
                        std::lock_guard guard(a.mutex);
                        ++a.stats.cancelled;
                    }
                }
                pixels.reset();
                request.reset();
                { std::lock_guard lock(a.mutex); a.stats.active = 0; }
            }
        }
        if (SUCCEEDED(com)) CoUninitialize();
    }
public:
    Service() { std::thread([this] { run(); }).detach(); }
    void add(const std::shared_ptr<ImageRequest>& request) {
        std::lock_guard lock(mutex_);
        prune();
        if (queue_.size() >= ImageLimits::queue) {
            request->done = true;
            request->error = L"The image queue is full. Unload other images, then reload this image.";
            auto& a = accounting(); std::lock_guard guard(a.mutex); ++a.stats.rejected;
            return;
        }
        queue_.push_back(request);
        changed_.notify_one();
    }
    void clear(bool all) {
        std::lock_guard lock(mutex_);
        ++epoch_;
        prune();
        if (all) cache_.clear();
        else evict(true);
    }
    void statistics(ImageStatistics& stats) {
        std::lock_guard lock(mutex_);
        auto& a = accounting(); std::lock_guard guard(a.mutex);
        stats = a.stats;
        stats.queued = queue_.size();
        stats.cache_entries = cache_.size();
    }
};
// The worker starts only after the first image request, not for ordinary windows.
std::atomic<Service*> live_service{};
Service& service() {
    (void)accounting();
    // One process-lifetime worker. Neither window close nor C++ exit waits for a codec.
    // Windows reclaims this bounded service at process exit. No per-window worker is created.
    static auto* value = new Service;
    live_service = value;
    return *value;
}
}

Image::Image(std::wstring name) : Control(ControlRole::image, std::move(name), {192, 144}) {}
void Image::set_source(std::wstring path, ImageSize size) {
    if (path.size() > 32767 || path.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("The image path is invalid or too long.");
    if (!size.width || !size.height || size.width > ImageLimits::output_dimension || size.height > ImageLimits::output_dimension)
        throw std::invalid_argument("Image display dimensions must be between 1 and 1024 pixels.");
    if (source_ == path && size_ == size) return;
    source_ = std::move(path);
    size_ = size;
    reload();
}
void Image::reload() { ++revision_; publish(source_.empty() ? ImageStatus::empty : ImageStatus::loading); invalidate(Invalidation::paint); }
void Image::unload() { source_.clear(); reload(); }
void Image::publish(ImageStatus status, std::wstring error) {
    if (status_ == status && error_ == error) return;
    status_ = status;
    error_ = std::move(error);
    invalidate(Invalidation::paint);
}
ImagePixels::~ImagePixels() {
    // Release the allocation before returning its budget to another decode.
    std::vector<std::byte>().swap(pixels);
    auto& a = accounting(); std::lock_guard lock(a.mutex); a.stats.cpu_bytes -= accounted;
}
void ImageRequest::cancel() {
    cancelled = true;
    std::lock_guard lock(mutex);
    pixels.reset();
    wake.reset();
}
std::shared_ptr<ImageRequest> request_image(std::wstring path, ImageSize size, std::shared_ptr<TaskWake> wake) {
    auto request = std::make_shared<ImageRequest>();
    request->path = std::move(path); request->size = size; request->wake = std::move(wake);
    service().add(request);
    return request;
}
void ImagePeer::sync(bool shown, const std::shared_ptr<TaskWake>& wake) {
    if (revision == control.revision() && visible == shown) { deliver(); return; }
    detach();
    revision = control.revision();
    visible = shown;
    if (shown && !control.source().empty()) {
        control.publish(ImageStatus::loading);
        request = request_image(control.source(), control.display_pixels(), wake);
        deliver();
    }
}
bool ImagePeer::deliver() {
    if (!request || revision != control.revision()) return false;
    std::shared_ptr<ImageRequest> completed = request;
    std::lock_guard lock(completed->mutex);
    if (!completed->done || completed->cancelled) return false;
    pixels = std::move(completed->pixels);
    control.publish(pixels ? ImageStatus::ready : ImageStatus::error, std::move(completed->error));
    request.reset();
    return true;
}
void ImagePeer::detach() {
    if (request) request->cancel();
    request.reset(); pixels.reset();
    revision = 0; visible = false;
    control.publish(ImageStatus::empty);
}
void ImagePeer::paint(Drawing& drawing, Rect bounds) {
    if (!pixels || revision != control.revision() || bounds.width <= 0 || bounds.height <= 0) return;
    if (!drawing.image(pixels, bounds)) control.publish(ImageStatus::error, L"The bitmap budget is full or the upload failed.");
    else control.publish(ImageStatus::ready);
}
ImageStatistics ImageResources::statistics() {
    ImageStatistics stats;
    if (auto* value = live_service.load()) value->statistics(stats);
    else { auto& a = accounting(); std::lock_guard lock(a.mutex); stats = a.stats; }
    return stats;
}
void ImageResources::clear_unused() { if (auto* value = live_service.load()) value->clear(false); }
void clear_image_cache() { if (auto* value = live_service.load()) value->clear(true); }
bool reserve_bitmap(std::size_t bytes) {
    auto& a = accounting(); std::lock_guard lock(a.mutex);
    if (bytes > ImageLimits::gpu_bytes - a.stats.gpu_bytes - a.stats.gpu_reserved) { ++a.stats.rejected; return false; }
    a.stats.gpu_reserved += bytes; peak_gpu(a.stats); return true;
}
void finish_bitmap(std::size_t bytes, bool success, double milliseconds) {
    auto& a = accounting(); std::lock_guard lock(a.mutex);
    a.stats.gpu_reserved -= bytes;
    if (success) { a.stats.gpu_bytes += bytes; ++a.stats.uploaded; }
    else ++a.stats.rejected;
    a.stats.upload_ms += milliseconds;
    a.stats.upload_max_ms = std::max(a.stats.upload_max_ms, milliseconds);
}
void release_bitmap(std::size_t bytes) {
    auto& a = accounting(); std::lock_guard lock(a.mutex); a.stats.gpu_bytes -= bytes;
}
}

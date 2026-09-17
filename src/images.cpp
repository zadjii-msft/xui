#include "images.hpp"
#include "style_hosts_geometry.hpp"
#include "async.hpp"
#include "drawing.hpp"
#include <wincodec.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <list>
#include <stdexcept>

namespace xui {
std::atomic<void(*)(ImageDecodeStage)> ImageDecodeTestAccess::hook{};
std::atomic<void(*)(ImageDecodeStage)> ImageDecodeTestAccess::shell_hook{};
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
    std::wstring shell_path;
    DWORD volume{}, index_high{}, index_low{};
    std::uint64_t write{}, bytes{};
    ImageSize size{};
    ImageKind kind{};
    bool operator==(const Key&) const = default;
};
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Cancelled {};
void checkpoint(const ImageRequest& r) { if (r.cancelled) throw Cancelled{}; }
void test_boundary(ImageKind kind, ImageDecodeStage stage) {
    const auto hook = kind == ImageKind::shell ? ImageDecodeTestAccess::shell_hook.load() : ImageDecodeTestAccess::hook.load();
    if (hook) hook(stage);
}
void require(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(operation);
}
void shell_require(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) return;
    char message[192]{};
    sprintf_s(message, "%s (HRESULT 0x%08lX).", operation, static_cast<unsigned long>(result));
    throw std::runtime_error(message);
}
struct Bitmap {
    HBITMAP value{};
    ~Bitmap() { if (value) DeleteObject(value); }
};
struct Icon {
    HICON value{};
    ~Icon() { if (value) DestroyIcon(value); }
};
std::wstring shell_path(const std::wstring& path) {
    const auto length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!length || length > 32767) throw std::runtime_error("Cannot resolve the Shell item path.");
    std::wstring result(length, L'\0');
    const auto actual = GetFullPathNameW(path.c_str(), length, result.data(), nullptr);
    if (!actual || actual >= length) throw std::runtime_error("Cannot resolve the Shell item path.");
    result.resize(actual);
    return result;
}
ComPtr<IWICBitmapSource> shell_source(ImageRequest& r, const std::wstring& path, IWICImagingFactory* wic) {
    ComPtr<IShellItemImageFactory> item;
    shell_require(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)),
        "Cannot open the Shell item");
    checkpoint(r);
    Bitmap bitmap;
    const SIZE target{static_cast<LONG>(r.size.width), static_cast<LONG>(r.size.height)};
    const auto thumbnail = item->GetImage(target, SIIGBF_THUMBNAILONLY, &bitmap.value);
    checkpoint(r);
    const bool icon = FAILED(thumbnail);
    if (icon) {
        if (bitmap.value) { DeleteObject(bitmap.value); bitmap.value = nullptr; }
        shell_require(item->GetImage(target, SIIGBF_ICONONLY, &bitmap.value),
            "Cannot obtain a Shell thumbnail or file icon");
    }
    checkpoint(r);
    BITMAP dimensions{};
    if (!bitmap.value || !GetObjectW(bitmap.value, sizeof(dimensions), &dimensions) ||
        dimensions.bmWidth <= 0 || dimensions.bmHeight <= 0 ||
        static_cast<UINT>(dimensions.bmWidth) > r.size.width ||
        static_cast<UINT>(dimensions.bmHeight) > r.size.height)
        throw std::runtime_error("The Shell bitmap exceeds the requested dimensions or is invalid.");
    ComPtr<IWICBitmap> source;
    // Shell HBITMAPs use straight alpha; the decoder converts to PBGRA for Direct2D.
    require(wic->CreateBitmapFromHBITMAP(bitmap.value, nullptr,
        dimensions.bmBitsPixel == 32 ? WICBitmapUseAlpha : WICBitmapIgnoreAlpha, &source),
        "Cannot read the Shell bitmap.");
    if (dimensions.bmBitsPixel == 32) {
        // Some legacy icons have no alpha channel. Preserve their AND mask through HICON.
        bool alpha{};
        BYTE row[ImageLimits::output_dimension * 4]{};
        for (INT y = 0; y < dimensions.bmHeight && !alpha; ++y) {
            WICRect rect{0, y, dimensions.bmWidth, 1};
            require(source->CopyPixels(&rect, dimensions.bmWidth * 4, sizeof(row), row),
                "Cannot read Shell bitmap transparency.");
            for (INT x = 0; x < dimensions.bmWidth; ++x) if (row[x * 4 + 3]) { alpha = true; break; }
        }
        if (!alpha) {
            source.Reset();
            if (icon) {
                SHFILEINFOW info{};
                const auto obtained = SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON |
                    (r.size.width <= 16 ? SHGFI_SMALLICON : SHGFI_LARGEICON));
                Icon handle{info.hIcon};
                if (!obtained || !handle.value) throw std::runtime_error("Cannot read the Shell icon transparency mask.");
                require(wic->CreateBitmapFromHICON(handle.value, &source), "Cannot convert the Shell icon mask.");
            } else {
                require(wic->CreateBitmapFromHBITMAP(bitmap.value, nullptr, WICBitmapIgnoreAlpha, &source),
                    "Cannot read the opaque Shell thumbnail.");
            }
        }
    }
    return source;
}
class Service {
    struct Entry { Key key; std::shared_ptr<const ImagePixels> pixels; };
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::shared_ptr<ImageRequest>> queue_;
    HANDLE shell_changed_{};
    std::once_flag shell_started_, wic_started_;
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
        const bool shell = r.kind == ImageKind::shell;
        Handle file{CreateFileW(r.path.c_str(), shell ? FILE_READ_ATTRIBUTES : GENERIC_READ,
            shell ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE : FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, shell ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open the visual source file.");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.value, &info) || (!shell && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)))
            throw std::runtime_error("Cannot read visual source file information.");
        Key key;
        key.bytes = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (!shell && (!key.bytes || key.bytes > ImageLimits::file_bytes))
            throw std::runtime_error("The image file exceeds the 32 MiB limit or is empty.");
        const auto length = GetFinalPathNameByHandleW(file.value, nullptr, 0, FILE_NAME_NORMALIZED);
        if (!length || length > 32767) throw std::runtime_error("Cannot resolve the image path.");
        key.path.resize(length);
        const auto actual = GetFinalPathNameByHandleW(file.value, key.path.data(), length, FILE_NAME_NORMALIZED);
        if (!actual || actual >= length) throw std::runtime_error("Cannot resolve the image path.");
        key.path.resize(actual);
        // Shell visuals can differ for links to the same resolved file.
        if (shell) key.shell_path = shell_path(r.path);
        key.volume = info.dwVolumeSerialNumber;
        key.index_high = info.nFileIndexHigh;
        key.index_low = info.nFileIndexLow;
        key.write = (std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
        key.size = r.size;
        key.kind = r.kind;
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
        ComPtr<IWICBitmapSource> frame;
        if (shell) {
            frame = shell_source(r, key.shell_path, factory);
        } else {
            require(factory->CreateDecoderFromFileHandle(reinterpret_cast<ULONG_PTR>(file.value), nullptr,
                WICDecodeMetadataCacheOnDemand, &decoder), "Cannot decode the image file.");
            checkpoint(r);
            ComPtr<IWICBitmapFrameDecode> first;
            require(decoder->GetFrame(0, &first), "Cannot decode the first image frame.");
            frame = first;
        }
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
        test_boundary(r.kind, ImageDecodeStage::reserved);
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
    void run(ImageKind kind) {
        const bool shell = kind == ImageKind::shell;
        const HRESULT com = CoInitializeEx(nullptr, shell ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
        HRESULT wait_result = S_OK;
        {
            ComPtr<IWICImagingFactory> factory;
            const auto initialized = SUCCEEDED(com) ? CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)) : com;
            for (;;) {
                if (shell && SUCCEEDED(wait_result)) {
                    // Pump the STA between requests, including while the bounded queue is empty.
                    const auto wait = MsgWaitForMultipleObjectsEx(1, &shell_changed_, INFINITE,
                        QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    if (wait == WAIT_FAILED) {
                        wait_result = HRESULT_FROM_WIN32(GetLastError());
                    }
                    MSG message{};
                    for (int i = 0; i < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i) {
                        TranslateMessage(&message); DispatchMessageW(&message);
                    }
                }
                std::shared_ptr<ImageRequest> request;
                {
                    std::unique_lock lock(mutex_);
                    const auto belongs = [kind](const auto& r) { return r->kind == kind; };
                    if (!shell || FAILED(wait_result)) changed_.wait(lock, [&] {
                        return std::any_of(queue_.begin(), queue_.end(), belongs);
                    });
                    const auto found = std::find_if(queue_.begin(), queue_.end(), belongs);
                    if (found == queue_.end()) continue;
                    request = std::move(*found);
                    queue_.erase(found);
                    if (shell && std::any_of(queue_.begin(), queue_.end(), belongs)) SetEvent(shell_changed_);
                    auto& a = accounting();
                    std::lock_guard guard(a.mutex);
                    ++a.stats.active;
                }
                auto& a = accounting();
                const auto start = Clock::now();
                std::shared_ptr<const ImagePixels> pixels;
                std::wstring error;
                try {
                    test_boundary(kind, ImageDecodeStage::before_decode);
                    checkpoint(*request);
                    shell_require(wait_result, "Cannot wait for Shell worker messages");
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
                    test_boundary(kind, ImageDecodeStage::before_delivery);
                    std::lock_guard lock(request->mutex);
                    if (!request->cancelled) {
                        request->pixels = std::move(pixels);
                        request->error = std::move(error);
                        request->done = true;
                        if (request->wake) request->wake->signal();
                    } else {
                        std::lock_guard guard(a.mutex);
                        ++a.stats.cancelled;
                    }
                }
                pixels.reset();
                request.reset();
                { std::lock_guard lock(a.mutex); --a.stats.active; }
            }
        }
        if (SUCCEEDED(com)) CoUninitialize();
    }
public:
    void add(const std::shared_ptr<ImageRequest>& request) {
        try {
            if (request->kind == ImageKind::shell) std::call_once(shell_started_, [this] {
                shell_changed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (!shell_changed_) throw std::runtime_error("Cannot create the Shell worker event.");
                try { std::thread([this] { run(ImageKind::shell); }).detach(); }
                catch (...) { CloseHandle(shell_changed_); shell_changed_ = nullptr; throw; }
            });
            else std::call_once(wic_started_, [this] { std::thread([this] { run(ImageKind::wic); }).detach(); });
        } catch (const std::exception& failure) {
            const std::string text(failure.what());
            request->error.assign(text.begin(), text.end());
            request->done = true;
            auto& a = accounting(); std::lock_guard guard(a.mutex); ++a.stats.rejected;
            return;
        }
        std::lock_guard lock(mutex_);
        prune();
        if (queue_.size() >= ImageLimits::queue) {
            request->done = true;
            request->error = L"The image queue is full. Unload other images, then reload this image.";
            auto& a = accounting(); std::lock_guard guard(a.mutex); ++a.stats.rejected;
            return;
        }
        queue_.push_back(request);
        if (request->kind == ImageKind::shell) SetEvent(shell_changed_);
        changed_.notify_all();
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
// Each worker starts only after the first request for its source kind.
std::atomic<Service*> live_service{};
Service& service() {
    (void)accounting();
    // At most two process-lifetime workers. Neither close nor exit waits for a codec or Shell handler.
    // Windows reclaims this bounded service at process exit. No per-window worker is created.
    static auto* value = new Service;
    live_service = value;
    return *value;
}
}

Image::Image(std::wstring name) : Control(ControlRole::image, std::move(name), {192, 144}) {}
StyleStateMask Image::control_style_state_bits() const {
    constexpr StyleStateMask states[]{style_states::empty, style_states::loading, style_states::ready, style_states::error};
    return Control::control_style_state_bits() | states[static_cast<unsigned>(status_)];
}
Rect Image::content_bounds() const {
    const auto b = bounds();
    const auto* root = effective_control_style_values(StylePart::root);
    return host_content_rect({0, 0, b.width, b.height}, root, {2, 2, 2, 2},
        root && visual_style() == VisualStyle::winui ? Insets{1, 1, 1, 1} : Insets{});
}
void Image::set_source(std::wstring path, ImageSize size) {
    set_source_kind(std::move(path), size, ImageKind::wic);
}
void Image::set_shell_source(std::wstring path, ImageSize size) {
    set_source_kind(std::move(path), size, ImageKind::shell);
}
void Image::set_source_kind(std::wstring path, ImageSize size, ImageKind kind) {
    if (path.size() > 32767 || path.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("The image path is invalid or too long.");
    if (!size.width || !size.height || size.width > ImageLimits::output_dimension || size.height > ImageLimits::output_dimension)
        throw std::invalid_argument("Image display dimensions must be between 1 and 1024 pixels.");
    if (source_ == path && size_ == size && kind_ == kind) return;
    source_ = std::move(path);
    size_ = size;
    kind_ = kind;
    reload();
}
void Image::reload() { ++revision_; publish(source_.empty() ? ImageStatus::empty : ImageStatus::loading); invalidate(Invalidation::paint); }
void Image::unload() { source_.clear(); reload(); }
void Image::publish(ImageStatus status, std::wstring error) {
    if (status_ == status && error_ == error) return;
    status_ = status;
    error_ = std::move(error);
    invalidate_state();
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
ImageKind thumbnail_kind(std::wstring_view path, bool directory) {
    if (directory) return ImageKind::shell;
    const auto dot = path.find_last_of(L'.');
    if (dot != path.npos) {
        const auto extension = path.substr(dot);
        for (const auto supported : {L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif", L".tiff", L".webp"})
            if (CompareStringOrdinal(extension.data(), static_cast<int>(extension.size()), supported, -1, TRUE) == CSTR_EQUAL)
                return ImageKind::wic;
    }
    return ImageKind::shell;
}
void RowImages::clear() { slots_.clear(); rows_.clear(); source_.reset(); pixels_ = 0; }
ItemVisual RowImages::visual(ItemKey key) const {
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const auto& row) { return row.key == key; });
    return found == rows_.end() ? ItemVisual{} : found->visual;
}
std::shared_ptr<const ImagePixels> RowImages::pixels(ItemKey key) const {
    const auto found = std::find_if(slots_.begin(), slots_.end(), [&](const auto& slot) { return slot->key == key; });
    return found == slots_.end() ? nullptr : (*found)->pixels;
}
static void validate_row_visuals(const std::vector<RowVisual>& rows) {
    if (rows.size() > RowImages::maximum_rows) throw std::length_error("Too many visible row visuals");
    for (const auto& row : rows) {
        if (row.visual.icon < ButtonIcon::none || row.visual.icon > ButtonIcon::chevron_down ||
            row.visual.image_path.size() > 32767 || row.visual.image_path.find(L'\0') != std::wstring::npos)
            throw std::invalid_argument("Invalid row visual icon or image path");
    }
}
bool RowImages::sync(std::shared_ptr<const CollectionIndex> source, std::vector<RowVisual> rows, UINT dpi,
    const std::shared_ptr<TaskWake>& wake, std::vector<std::uint64_t>& retained, std::size_t& remaining,
    bool retain_on_source_change) {
    if (!source || rows.empty()) { const bool changed = !slots_.empty(); clear(); return changed; }
    validate_row_visuals(rows);
    bool changed{};
    if (!retain_on_source_change && source_.lock() != source) {
        changed = !slots_.empty(); clear();
    }
    source_ = source;
    return sync_visuals(std::move(rows), dpi, wake, retained, remaining) || changed;
}
bool RowImages::sync_visuals(std::vector<RowVisual> rows, UINT dpi, const std::shared_ptr<TaskWake>& wake,
    std::vector<std::uint64_t>& retained, std::size_t& remaining, float image_dips) {
    if (rows.empty()) { const bool changed = !slots_.empty(); clear(); return changed; }
    validate_row_visuals(rows);
    if (!std::isfinite(image_dips) || image_dips <= 0 || image_dips > ImageLimits::output_dimension)
        throw std::invalid_argument("Invalid visual image size");
    const auto pixels = std::clamp(static_cast<UINT>(std::lround(image_dips * dpi / 96.0)), 1u, ImageLimits::output_dimension);
    bool changed{};
    if (pixels_ != pixels) {
        changed = !slots_.empty(); slots_.clear(); pixels_ = pixels;
    }
    rows_ = std::move(rows);
    std::vector<const RowVisual*> wanted;
    const auto limit = std::min(maximum_images, remaining);
    for (const auto& row : rows_) {
        if (wanted.size() == limit) break;
        if (!row.visual.image_path.empty()) wanted.push_back(&row);
    }
    const auto kind = [](const RowVisual& row) {
        return thumbnail_kind(row.visual.image_path, row.directory || row.visual.icon == ButtonIcon::folder);
    };
    const auto matches = [&](const Slot& slot, const RowVisual& row) {
        return slot.key == row.key && slot.path == row.visual.image_path && slot.kind == kind(row);
    };
    // Navigation and tabs retain visual identity; ordinary source refreshes reload changed files.
    const auto removed = std::erase_if(slots_, [&](const auto& slot) {
        return std::none_of(wanted.begin(), wanted.end(), [&](const auto* row) { return matches(*slot, *row); });
    });
    changed = removed != 0 || changed;
    for (const auto* row : wanted) {
        auto found = std::find_if(slots_.begin(), slots_.end(), [&](const auto& slot) { return matches(*slot, *row); });
        if (found == slots_.end()) {
            auto slot = std::make_unique<Slot>();
            slot->key = row->key; slot->path = row->visual.image_path; slot->kind = kind(*row);
            slot->request = request_image(slot->path, {pixels, pixels}, wake, slot->kind);
            slots_.push_back(std::move(slot)); found = std::prev(slots_.end());
        }
        auto& slot = **found;
        if (const auto request = slot.request) {
            std::lock_guard lock(request->mutex);
            if (request->done && !request->cancelled) {
                slot.pixels = request->pixels;
                if (!slot.pixels) {
                    const auto message = L"XUI thumbnail: " + slot.path + L": " +
                        (request->error.empty() ? L"Decoding failed." : request->error) + L"\n";
                    OutputDebugStringW(message.c_str());
                }
                slot.request.reset(); changed = true;
            }
        }
        if (slot.pixels) retained.push_back(slot.pixels->id);
    }
    remaining -= slots_.size();
    return changed;
}
std::shared_ptr<ImageRequest> request_image(std::wstring path, ImageSize size, std::shared_ptr<TaskWake> wake, ImageKind kind) {
    auto request = std::make_shared<ImageRequest>();
    request->path = std::move(path); request->size = size; request->wake = std::move(wake); request->kind = kind;
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
        request = request_image(control.source(), control.display_pixels(), wake, control.source_kind());
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

std::vector<std::byte> memory_jpeg_fixture() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    image_fixture::hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IStream> stream; image_fixture::hr(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    ComPtr<IWICBitmapEncoder> encoder; image_fixture::hr(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder));
    image_fixture::hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame; image_fixture::hr(encoder->CreateNewFrame(&frame, nullptr));
    image_fixture::hr(frame->Initialize(nullptr)); image_fixture::hr(frame->SetSize(3, 2));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR; image_fixture::hr(frame->SetPixelFormat(&format));
    check(format == GUID_WICPixelFormat24bppBGR, "Encode owned JPEG without a format fallback");
    BYTE pixels[]{0,0,255, 0,255,0, 255,0,0, 255,255,255, 0,0,0, 32,64,128};
    image_fixture::hr(frame->WritePixels(2, 9, sizeof(pixels), pixels));
    image_fixture::hr(frame->Commit()); image_fixture::hr(encoder->Commit());
    STATSTG state{}; image_fixture::hr(stream->Stat(&state, STATFLAG_NONAME));
    check(state.cbSize.QuadPart > 0 && state.cbSize.QuadPart <= ImageLimits::file_bytes, "Owned JPEG fixture is bounded");
    std::vector<std::byte> bytes(static_cast<std::size_t>(state.cbSize.QuadPart));
    image_fixture::hr(stream->Seek({}, STREAM_SEEK_SET, nullptr));
    ULONG read{}; image_fixture::hr(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read));
    check(read == bytes.size(), "Read complete in-memory JPEG fixture");
    return bytes;
}
std::vector<std::byte> memory_png_chunk(std::string_view type, std::span<const uint8_t> payload) {
    std::vector<std::byte> result(image_memory_fixture::bytes().begin(), image_memory_fixture::bytes().begin() + 33);
    const auto append_u32 = [&](uint32_t value) {
        for (int shift : {24, 16, 8, 0}) result.push_back(std::byte((value >> shift) & 255));
    };
    append_u32(static_cast<uint32_t>(payload.size()));
    uint32_t crc = UINT32_MAX;
    const auto append = [&](uint8_t value) {
        result.push_back(std::byte(value)); crc ^= value;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    };
    for (char value : type) append(static_cast<uint8_t>(value));
    for (auto value : payload) append(value);
    append_u32(~crc);
    result.insert(result.end(), image_memory_fixture::bytes().begin() + 33, image_memory_fixture::bytes().end());
    return result;
}
void memory_tests() {
    empty();
    const auto plan = [](uint64_t generation, ImageSize hint = {64, 64}, ImageSize output = {3, 2}) {
        return ImageMemoryPlan{generation, ImageMemoryFormat::png, {3, 2}, output, hint};
    };
    const auto wake = std::make_shared<TaskWake>();
    {
        const std::array<uint8_t, 8> animation{0, 0, 0, 1, 0, 0, 0, 0};
        const std::array<uint8_t, 26> exif{'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0,
            0x12, 1, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0};
        for (const auto& bytes : {memory_png_chunk("acTL", animation), memory_png_chunk("eXIf", exif)}) {
            Image image; image.set_memory_source(bytes, plan(1));
            wait([&] { image.sync_memory(wake); return image.status() == ImageStatus::error; });
            check(!image.memory_pixels() && !image.error().empty() && !ImageResources::statistics().cpu_reserved,
                "Valid-checksum APNG and rotated EXIF reject before native pixel allocation");
            image.cancel_memory();
        }
    }
    empty();
    {
        Image image(L"Descriptive alt text is not image size");
        image.cancel_memory();
        const auto natural = image.measure({1000, 1000});
        check(natural.width == 192 && natural.height == 144, "Empty memory image has stable logical desired size");
        auto copy = image_memory_fixture::png;
        image.set_memory_source(std::as_bytes(std::span(copy)), plan(1));
        copy.fill(0);
        wait([&] { image.sync_memory(wake); return bool(image.memory_pixels()) || image.status() == ImageStatus::error; });
        check(image.memory_pixels() && image.memory_pixels()->size == ImageSize{3, 2} &&
            image.memory_pixels()->source_size == ImageSize{3, 2}, "Copied PNG decodes after caller bytes are overwritten");
        const auto& pixels = image.memory_pixels()->pixels;
        check(pixels.size() == 24 && std::to_integer<unsigned>(pixels[2]) == 255 &&
            std::to_integer<unsigned>(pixels[4 + 1]) == 255 && std::to_integer<unsigned>(pixels[8]) == 255 &&
            std::to_integer<unsigned>(pixels[20]) == 32 && std::to_integer<unsigned>(pixels[21]) == 64 &&
            std::to_integer<unsigned>(pixels[22]) == 128, "Owned PNG pixels match the original RGBA fixture");
        check(image.status() == ImageStatus::loading, "Decoded pixels alone do not report native surface readiness");
        image.memory_uploaded();
        check(image.status() == ImageStatus::ready && image.measure({1000, 1000}).width == 192 &&
            image.measure({1000, 1000}).height == 144, "Ready memory image keeps logical default size");
        image.set_memory_source(image_memory_fixture::bytes(), plan(2, {2, 2}, {2, 1}));
        wait([&] { image.sync_memory(wake); return bool(image.memory_pixels()); });
        check(image.memory_pixels()->size == ImageSize{2, 1} && image.memory_pixels()->source_size == ImageSize{3, 2},
            "Integer-floor decode preserves original source aspect metadata");
        image.set_axis_constraints(AxisConstraints{90.0f}, AxisConstraints{});
        check(image.measure({1000, 1000}).width == 90 && image.measure({1000, 1000}).height == 144,
            "Image per-axis sizing uses logical viewport policy rather than decoded pixels or alt text");
        image.cancel_memory(); image.cancel_memory();
        check(image.memory_state().state == ImageStatus::empty && !image.memory_pixels(), "Cancellation clears owned presentation");
        bool stale{};
        try { image.set_memory_source(image_memory_fixture::bytes(), plan(2)); } catch (const std::invalid_argument&) { stale = true; }
        check(stale, "Canceled generations cannot resurrect");
        image.set_memory_source(image_memory_fixture::bytes(), plan(3));
        image.cancel_memory();
    }
    empty();
    check(!ImageResources::statistics().encoded_bytes, "Completed requests release encoded copies");
    {
        const auto bytes = memory_jpeg_fixture();
        Image image;
        auto value = plan(1, {2, 2}, {2, 1}); value.format = ImageMemoryFormat::jpeg;
        image.set_memory_source(bytes, value);
        wait([&] { image.sync_memory(wake); return bool(image.memory_pixels()) || image.status() == ImageStatus::error; });
        check(image.memory_pixels() && image.memory_pixels()->size == ImageSize{2, 1} &&
            image.memory_pixels()->source_size == ImageSize{3, 2}, "Actual WIC memory JPEG decode matches source and floor-output plan");
        image.cancel_memory();
    }
    empty();
    for (unsigned kind = 0; kind < 3; ++kind) {
        Image image;
        auto value = plan(1);
        auto data = image_memory_fixture::png;
        if (kind == 0) data[50] ^= 1;
        if (kind == 1) { value.source = {4, 2}; value.output = {4, 2}; }
        if (kind == 2) value.format = ImageMemoryFormat::jpeg;
        image.set_memory_source(std::as_bytes(std::span(data)), value);
        wait([&] { image.sync_memory(wake); return image.status() == ImageStatus::error; });
        check(!image.error().empty() && !ImageResources::statistics().cpu_reserved && !image.memory_pixels(),
            "Malformed checksum, source mismatch, and wrong format fail before pixel ownership");
        image.cancel_memory();
    }
    empty();
    for (auto stage : {ImageDecodeStage::before_decode, ImageDecodeStage::reserved, ImageDecodeStage::before_delivery}) {
        Gate gate(stage);
        Image image; image.set_memory_source(image_memory_fixture::bytes(), plan(1)); image.sync_memory(wake);
        gate.await();
        image.cancel_memory();
        check(image.status() == ImageStatus::empty && ImageResources::statistics().encoded_bytes == image_memory_fixture::png.size(),
            "Cancellation revokes the image but the blocked worker retains its owned bytes");
        gate.release();
        wait([] { return !ImageResources::statistics().active && !ImageResources::statistics().queued; });
        image.sync_memory(wake);
        check(image.status() == ImageStatus::empty && !ImageResources::statistics().encoded_bytes &&
            !ImageResources::statistics().cpu_bytes && !ImageResources::statistics().cpu_reserved,
            "Canceled decoder work retires without stale presentation or reservations");
    }
    {
        std::vector<std::byte> bytes(ImageLimits::file_bytes);
        Image first, second, rejected;
        first.set_memory_source(bytes, plan(1)); second.set_memory_source(bytes, plan(1));
        check(ImageResources::statistics().encoded_bytes == ImageLimits::encoded_bytes, "Encoded copies have an independent global budget");
        bool full{};
        try { rejected.set_memory_source(image_memory_fixture::bytes(), plan(1)); } catch (const std::length_error&) { full = true; }
        check(full && rejected.memory_state().generation == 0, "Encoded exhaustion rejects before replacing request state");
        first.cancel_memory(); second.cancel_memory();
    }
    empty();
    check(!ImageResources::statistics().encoded_bytes, "Never-presented encoded buffers release on cancellation");
    std::cout << "Memory image decoding, plan bounds, byte ownership and cancellation passed\n";
}

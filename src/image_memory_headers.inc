using ImageBytes = std::span<const std::byte>;
uint8_t image_byte(ImageBytes bytes, std::size_t at) { return std::to_integer<uint8_t>(bytes[at]); }
uint32_t image_u32(ImageBytes bytes, std::size_t at, bool little = false) {
    uint32_t value{};
    for (unsigned i = 0; i < 4; ++i) value = (value << 8) | image_byte(bytes, at + (little ? 3 - i : i));
    return value;
}
uint16_t image_u16(ImageBytes bytes, std::size_t at, bool little = false) {
    return little ? uint16_t(image_byte(bytes, at) | (image_byte(bytes, at + 1) << 8)) :
        uint16_t((image_byte(bytes, at) << 8) | image_byte(bytes, at + 1));
}
bool image_marker(ImageBytes bytes, std::string_view marker) {
    return bytes.size() == marker.size() && std::equal(bytes.begin(), bytes.end(), marker.begin(),
        [](std::byte left, char right) { return std::to_integer<uint8_t>(left) == static_cast<uint8_t>(right); });
}
void image_exif(ImageBytes bytes) {
    if (bytes.size() < 8) throw std::runtime_error("Truncated image EXIF header.");
    const bool little = image_marker(bytes.first(2), "II");
    if ((!little && !image_marker(bytes.first(2), "MM")) || image_u16(bytes, 2, little) != 42)
        throw std::runtime_error("Invalid image EXIF header.");
    const auto offset = image_u32(bytes, 4, little);
    if (!offset) return;
    if (offset < 8 || offset > bytes.size() - 2) throw std::runtime_error("Invalid image EXIF directory.");
    const auto count = image_u16(bytes, offset, little);
    if (uint64_t(offset) + 2 + uint64_t(count) * 12 + 4 > bytes.size())
        throw std::runtime_error("Truncated image EXIF directory.");
    bool orientation{};
    for (uint32_t i = 0; i < count; ++i) {
        const auto entry = offset + 2 + i * 12;
        if (image_u16(bytes, entry, little) != 0x112) continue;
        if (orientation || image_u16(bytes, entry + 2, little) != 3 || image_u32(bytes, entry + 4, little) != 1)
            throw std::runtime_error("Invalid image EXIF orientation.");
        orientation = true;
        if (image_u16(bytes, entry + 8, little) != 1)
            throw std::runtime_error("Rotated or reflected image EXIF is unsupported.");
    }
}
ImageSize memory_output(ImageSize source, ImageSize hint) {
    if (source.width <= hint.width && source.height <= hint.height) return source;
    if (uint64_t(source.width) * hint.height >= uint64_t(source.height) * hint.width)
        return {hint.width, std::max(1u, static_cast<uint32_t>(uint64_t(source.height) * hint.width / source.width))};
    return {std::max(1u, static_cast<uint32_t>(uint64_t(source.width) * hint.height / source.height)), hint.height};
}
void validate_memory_plan(const ImageMemoryPlan& plan) {
    if (!plan.generation || plan.generation > INT64_MAX ||
        (plan.format != ImageMemoryFormat::png && plan.format != ImageMemoryFormat::jpeg) ||
        !plan.source.width || !plan.source.height ||
        plan.source.width > ImageLimits::source_dimension || plan.source.height > ImageLimits::source_dimension ||
        uint64_t(plan.source.width) * plan.source.height > ImageLimits::source_pixels ||
        !plan.hint.width || !plan.hint.height ||
        plan.hint.width > ImageLimits::output_dimension || plan.hint.height > ImageLimits::output_dimension)
        throw std::invalid_argument("Invalid native memory image plan.");
    if (memory_output(plan.source, plan.hint) != plan.output)
        throw std::invalid_argument("Memory image output does not match its source and decode hints.");
}
uint32_t image_crc(ImageBytes bytes, const ImageRequest& request) {
    static constexpr auto table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < values.size(); ++i) {
            auto value = i;
            for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0);
            values[i] = value;
        }
        return values;
    }();
    uint32_t crc = UINT32_MAX;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (!(i & 4095)) checkpoint(request);
        crc = (crc >> 8) ^ table[(crc ^ image_byte(bytes, i)) & 255];
    }
    return ~crc;
}
ImageSize memory_png_header(ImageBytes bytes, const ImageRequest& request) {
    static constexpr uint8_t signature[]{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 8 || !std::equal(std::begin(signature), std::end(signature), bytes.begin(),
        [](uint8_t a, std::byte b) { return a == std::to_integer<uint8_t>(b); }))
        throw std::runtime_error("Memory image does not match its PNG format.");
    ImageSize dimensions{0, 0};
    std::size_t offset = 8;
    unsigned chunks{};
    bool header{}, data{}, ended_data{}, palette{};
    uint8_t color_type{};
    while (offset < bytes.size()) {
        checkpoint(request);
        if (++chunks > 65536 || bytes.size() - offset < 12) throw std::runtime_error("Invalid PNG chunk sequence.");
        const auto length = image_u32(bytes, offset);
        if (length > bytes.size() - offset - 12) throw std::runtime_error("Invalid PNG chunk length.");
        const auto type = bytes.subspan(offset + 4, 4), payload = bytes.subspan(offset + 8, length);
        for (auto value : type) {
            const auto letter = std::to_integer<uint8_t>(value);
            if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z')))
                throw std::runtime_error("Invalid PNG chunk type.");
        }
        if (image_byte(type, 2) & 32) throw std::runtime_error("Invalid PNG chunk type.");
        if (image_crc(bytes.subspan(offset + 4, std::size_t(length) + 4), request) != image_u32(bytes, offset + 8 + length))
            throw std::runtime_error("PNG chunk checksum mismatch.");
        if (!header && !image_marker(type, "IHDR")) throw std::runtime_error("PNG must begin with IHDR.");
        if (image_marker(type, "IHDR")) {
            if (header || length != 13) throw std::runtime_error("Invalid PNG image header.");
            header = true; dimensions = {image_u32(payload, 0), image_u32(payload, 4)};
            const auto depth = image_byte(payload, 8); color_type = image_byte(payload, 9);
            const bool valid_depth = color_type == 0 ? depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16 :
                color_type == 2 || color_type == 4 || color_type == 6 ? depth == 8 || depth == 16 :
                color_type == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8);
            if (!valid_depth || image_byte(payload, 10) || image_byte(payload, 11) || image_byte(payload, 12) > 1)
                throw std::runtime_error("Unsupported PNG image header.");
        } else if (image_marker(type, "acTL") || image_marker(type, "fcTL") || image_marker(type, "fdAT"))
            throw std::runtime_error("Animated PNG is unsupported.");
        else if (image_marker(type, "eXIf")) image_exif(payload);
        else if (image_marker(type, "PLTE")) {
            if (palette || data || !length || length > 768 || length % 3 || color_type == 0 || color_type == 4)
                throw std::runtime_error("Invalid PNG palette.");
            palette = true;
        } else if (image_marker(type, "IDAT")) {
            if (ended_data || (color_type == 3 && !palette)) throw std::runtime_error("Invalid PNG data order.");
            data = true;
        } else if (image_marker(type, "IEND")) {
            if (length || !data || offset + 12 != bytes.size()) throw std::runtime_error("Invalid PNG terminator.");
            return dimensions;
        } else if (!(image_byte(type, 0) & 32)) throw std::runtime_error("Unknown critical PNG chunk.");
        if (data && !image_marker(type, "IDAT")) ended_data = true;
        offset += std::size_t(length) + 12;
    }
    throw std::runtime_error("PNG has no complete image terminator.");
}
ImageSize memory_jpeg_header(ImageBytes bytes, const ImageRequest& request) {
    if (bytes.size() < 2 || image_byte(bytes, 0) != 0xff || image_byte(bytes, 1) != 0xd8)
        throw std::runtime_error("Memory image does not match its JPEG format.");
    std::size_t offset = 2;
    unsigned segments{};
    bool entropy{}, frame{}, scan{};
    ImageSize dimensions{0, 0};
    while (offset < bytes.size()) {
        checkpoint(request);
        if (entropy) while (offset < bytes.size() && image_byte(bytes, offset) != 0xff) {
            if (!(offset & 4095)) checkpoint(request);
            ++offset;
        }
        if (offset >= bytes.size() || image_byte(bytes, offset++) != 0xff) throw std::runtime_error("Invalid JPEG marker.");
        while (offset < bytes.size() && image_byte(bytes, offset) == 0xff) ++offset;
        if (offset == bytes.size()) throw std::runtime_error("Truncated JPEG marker.");
        const auto marker = image_byte(bytes, offset++);
        if (entropy && (!marker || (marker >= 0xd0 && marker <= 0xd7))) continue;
        entropy = false;
        if (++segments > 65536) throw std::runtime_error("Too many JPEG segments.");
        if (marker == 0xd9) {
            if (!frame || !scan || offset != bytes.size()) throw std::runtime_error("Invalid JPEG terminator.");
            return dimensions;
        }
        if (!marker || marker == 0xd8 || marker == 1 || (marker >= 0xd0 && marker <= 0xd7) || bytes.size() - offset < 2)
            throw std::runtime_error("Invalid JPEG segment.");
        const auto length = image_u16(bytes, offset);
        if (length < 2 || length > bytes.size() - offset) throw std::runtime_error("Invalid JPEG segment length.");
        const auto payload = bytes.subspan(offset + 2, length - 2);
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if ((marker != 0xc0 && marker != 0xc2) || frame || payload.size() < 6)
                throw std::runtime_error("Unsupported JPEG frame.");
            const auto components = image_byte(payload, 5);
            if (image_byte(payload, 0) != 8 || (components != 1 && components != 3) || payload.size() != 6u + 3u * components)
                throw std::runtime_error("JPEG requires 8-bit grayscale or three-component data.");
            dimensions = {image_u16(payload, 3), image_u16(payload, 1)}; frame = true;
        } else if (marker == 0xe1 && payload.size() >= 6 && image_marker(payload.first(6), std::string_view{"Exif\0\0", 6}))
            image_exif(payload.subspan(6));
        else if (marker == 0xda) {
            if (!frame || payload.size() < 6 || payload.size() != 4u + 2u * image_byte(payload, 0))
                throw std::runtime_error("Invalid JPEG scan.");
            scan = true; entropy = true;
        } else if (marker == 0xdc) throw std::runtime_error("Deferred JPEG dimensions are unsupported.");
        offset += length;
    }
    throw std::runtime_error("JPEG has no complete image terminator.");
}

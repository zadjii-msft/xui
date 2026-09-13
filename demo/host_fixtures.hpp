#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>
#include <stdexcept>

namespace host_fixtures {
class Riff {
public:
    std::vector<std::uint8_t> bytes;
    void u16(std::uint16_t value) { bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8)); }
    void u32(std::uint32_t value) { u16(static_cast<std::uint16_t>(value)); u16(static_cast<std::uint16_t>(value >> 16)); }
    void tag(const char* value) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value[i])); }
    std::size_t begin(const char* type) { tag(type); const auto offset = bytes.size(); u32(0); return offset; }
    void end(std::size_t offset) {
        const auto size = static_cast<std::uint32_t>(bytes.size() - offset - 4);
        for (int i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(size >> (8 * i));
        if (size & 1) bytes.push_back(0);
    }
    void save(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("Cannot write owned media fixture");
    }
};
inline void wave(const std::filesystem::path& path) {
    Riff r; const auto file = r.begin("RIFF"); r.tag("WAVE");
    const auto format = r.begin("fmt "); r.u16(1); r.u16(1); r.u32(8000); r.u32(16000); r.u16(2); r.u16(16); r.end(format);
    const auto data = r.begin("data");
    for (int i = 0; i < 24000; ++i) r.u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::sin(i * 440 * 6.283185307179586 / 8000) * 2000)));
    r.end(data); r.end(file); r.save(path);
}
inline void video(const std::filesystem::path& path) {
    Riff r; const auto file = r.begin("RIFF"); r.tag("AVI ");
    const auto headers = r.begin("LIST"); r.tag("hdrl");
    const auto main = r.begin("avih");
    r.u32(100000); r.u32(64 * 64 * 3 * 10); r.u32(0); r.u32(0x10);
    r.u32(60); r.u32(0); r.u32(1); r.u32(64 * 64 * 3); r.u32(64); r.u32(64);
    for (int i = 0; i < 4; ++i) r.u32(0);
    r.end(main);
    const auto stream = r.begin("LIST"); r.tag("strl");
    const auto sh = r.begin("strh"); r.tag("vids"); r.tag("DIB "); r.u32(0); r.u16(0); r.u16(0);
    r.u32(0); r.u32(1); r.u32(10); r.u32(0); r.u32(60); r.u32(64 * 64 * 3); r.u32(0xffffffff); r.u32(0);
    r.u16(0); r.u16(0); r.u16(64); r.u16(64); r.end(sh);
    const auto sf = r.begin("strf"); r.u32(40); r.u32(64); r.u32(64); r.u16(1); r.u16(24);
    r.u32(0); r.u32(64 * 64 * 3); r.u32(0); r.u32(0); r.u32(0); r.u32(0); r.end(sf); r.end(stream); r.end(headers);
    const auto movie = r.begin("LIST"); r.tag("movi"); std::vector<std::uint32_t> offsets;
    for (int frame = 0; frame < 60; ++frame) {
        offsets.push_back(static_cast<std::uint32_t>(r.bytes.size() - movie - 4));
        const auto data = r.begin("00db");
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const bool stripe = (x + frame * 2) % 32 < 16;
            r.bytes.push_back(stripe ? 210 : 20); r.bytes.push_back(30); r.bytes.push_back(stripe ? 20 : 230);
        }
        r.end(data);
    }
    r.end(movie); const auto index = r.begin("idx1");
    for (auto offset : offsets) { r.tag("00db"); r.u32(0x10); r.u32(offset); r.u32(64 * 64 * 3); }
    r.end(index); r.end(file); r.save(path);
}
}

#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace image_fixture {
inline void hr(HRESULT result) { if (FAILED(result)) throw std::runtime_error("Create WIC fixture"); }
inline void jpeg(const std::filesystem::path& path, unsigned width, unsigned height, unsigned color) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    hr(factory->CreateStream(&stream));
    hr(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    hr(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder));
    hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    hr(encoder->CreateNewFrame(&frame, nullptr));
    hr(frame->Initialize(nullptr)); hr(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat24bppBGR;
    hr(frame->SetPixelFormat(&format));
    std::vector<BYTE> pixels(std::size_t(width) * height * 3);
    for (std::size_t i = 0; i < pixels.size(); i += 3) {
        pixels[i] = static_cast<BYTE>(color);
        pixels[i + 1] = static_cast<BYTE>(color >> 8);
        pixels[i + 2] = static_cast<BYTE>(color >> 16);
    }
    hr(frame->WritePixels(height, width * 3, static_cast<UINT>(pixels.size()), pixels.data()));
    hr(frame->Commit()); hr(encoder->Commit());
}
inline void png(const std::filesystem::path& path, unsigned width, unsigned height, unsigned color) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    hr(factory->CreateStream(&stream));
    hr(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    hr(encoder->CreateNewFrame(&frame, nullptr));
    hr(frame->Initialize(nullptr));
    hr(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    hr(frame->SetPixelFormat(&format));
    std::vector<unsigned> pixels(std::size_t(width) * height, color);
    hr(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size() * 4),
        reinterpret_cast<BYTE*>(pixels.data())));
    hr(frame->Commit()); hr(encoder->Commit());
}
inline void create(const std::filesystem::path& directory, std::size_t count = 160) {
    std::filesystem::create_directories(directory);
    for (std::size_t i = 0; i < count; ++i)
        png(directory / (L"image-" + std::to_wstring(i) + L".png"), 1024, 1024,
            0xff000000u | (static_cast<unsigned>(i) * 193u + 0x2040c0u));
    std::ofstream(directory / L"corrupt.png", std::ios::binary) << "Not an image.";
    BITMAPFILEHEADER file{0x4d42, 58, 0, 0, 54};
    BITMAPINFOHEADER info{sizeof(info), 0x7fffffff, 0x7fffffff, 1, 32, BI_RGB};
    std::ofstream huge(directory / L"huge.bmp", std::ios::binary);
    huge.write(reinterpret_cast<const char*>(&file), sizeof(file));
    huge.write(reinterpret_cast<const char*>(&info), sizeof(info));
    huge << "xxxx";
}
}

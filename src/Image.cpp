// Image.cpp — optional image helpers for Display (showRgb / showJpegFile / clear).
//
// This is the ONE translation unit that instantiates the bundled baseline-JPEG encoder.
// It is compiled only when the library is built with image support (SPARTACUS_WITH_IMAGE).
// When that macro is undefined the whole file is empty, so a core-only build carries no
// JPEG code and Display::showJpeg() still works.
#ifdef SPARTACUS_WITH_IMAGE

#define SPARTACUS_JPEG_IMPLEMENTATION
#include "spartacus_jpeg.h"

#include <climits>
#include <fstream>
#include <iterator>
#include <vector>

#include "spartacus/Display.hpp"
#include "spartacus/Error.hpp"

namespace spartacus {

namespace {
constexpr int kPanel = 480;
}

void Display::showRgb(const std::uint8_t* pixels, int w, int h, int comp, int quality) {
    if (pixels == nullptr || w <= 0 || h <= 0 || (comp != 3 && comp != 4)) {
        throw Error("showRgb: invalid pixel buffer (need comp 3 or 4, positive dimensions)");
    }
    // The bundled C encoder/resizer indexes the source buffer with int arithmetic; refuse
    // dimensions whose byte size would overflow it.
    if (static_cast<long long>(w) * h * comp > INT_MAX) {
        throw Error("showRgb: image dimensions too large");
    }

    const std::uint8_t* src = pixels;
    unsigned char* resized = nullptr;
    if (w != kPanel || h != kPanel) {
        resized = spartacus_jpeg_resize_rgb(pixels, w, h, comp, kPanel, kPanel);
        if (resized == nullptr) throw Error("showRgb: resize to 480x480 failed");
        src = resized;
        w = kPanel;
        h = kPanel;
    }

    int outSize = 0;
    unsigned char* jpeg = spartacus_jpeg_encode_alloc(w, h, comp, src, quality, &outSize);
    if (resized != nullptr) spartacus_jpeg_free(resized);
    if (jpeg == nullptr) throw Error("showRgb: baseline JPEG encode failed");

    try {
        showJpeg(jpeg, static_cast<std::size_t>(outSize));
    } catch (...) {
        spartacus_jpeg_free(jpeg);
        throw;
    }
    spartacus_jpeg_free(jpeg);
}

void Display::showJpegFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw Error("showJpegFile: cannot open '" + path + "'");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    if (bytes.empty()) throw Error("showJpegFile: empty file '" + path + "'");
    showJpeg(bytes.data(), bytes.size());
}

void Display::clear(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kPanel) * kPanel * 3);
    for (std::size_t i = 0; i < px.size(); i += 3) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
    showRgb(px.data(), kPanel, kPanel, 3, 90);
}

}  // namespace spartacus

#endif  // SPARTACUS_WITH_IMAGE

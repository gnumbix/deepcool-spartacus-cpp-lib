// image.cpp — generate a 480x480 RGB test pattern and push it to the LCD via showRgb().
//
// The pattern is produced in memory (no files, no OS sensors); showRgb() encodes a baseline
// JPEG and streams it. Requires the library to be built with image support (default).
#include <spartacus/Spartacus.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    constexpr int N = 480;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(N) * N * 3);

    // A colourful pattern: red ramps with x, green with y, blue forms concentric rings.
    const double cx = (N - 1) / 2.0, cy = (N - 1) / 2.0;
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * N + x) * 3;
            const double dx = x - cx, dy = y - cy;
            const double r = dx * dx + dy * dy;
            px[i + 0] = static_cast<std::uint8_t>(x * 255 / (N - 1));
            px[i + 1] = static_cast<std::uint8_t>(y * 255 / (N - 1));
            px[i + 2] = static_cast<std::uint8_t>((static_cast<int>(r / 200.0) % 2) ? 220 : 40);
        }
    }

    try {
        auto display = spartacus::Display::open();
        display.sessionStart();
        std::printf("Uploading 480x480 test pattern...\n");
        display.showRgb(px.data(), N, N, 3, 90);
        std::printf("Done.\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

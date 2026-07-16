// clear.cpp — blank the panel with a solid-colour 480x480 frame.
//
//   clear [r g b]    0..255 each (default 0 0 0 = black)
//
// There is no hardware "clear" command; the library uploads a solid JPEG frame. (Brightness 0
// also blanks the panel, but writes persistent memory, so a black frame is preferred here.)
#include <spartacus/Spartacus.hpp>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    int r = 0, g = 0, b = 0;
    if (argc > 3) {
        r = std::atoi(argv[1]);
        g = std::atoi(argv[2]);
        b = std::atoi(argv[3]);
    }
    try {
        auto display = spartacus::Display::open();
        display.sessionStart();
        std::printf("Clearing panel to RGB(%d, %d, %d)\n", r, g, b);
        display.clear(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                      static_cast<std::uint8_t>(b));
        std::printf("Done.\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

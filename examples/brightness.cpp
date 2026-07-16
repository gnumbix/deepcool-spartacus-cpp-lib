// brightness.cpp — set the LCD backlight level.
//
//   brightness [level] [--sweep]
//
// level is 0..100 (default 50); 0 = a valid black screen. With --sweep, additionally
// sweeps 0 -> 100 -> 0 to demonstrate the range.
//
// The panel stores brightness in NON-VOLATILE memory, so every write wears it a little;
// set it only when it changes. The sweep (~23 writes) is therefore opt-in — fine to try,
// just not something to run in a loop.
#include <spartacus/Spartacus.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char** argv) {
    int level = 50;
    bool sweep = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sweep") == 0) {
            sweep = true;
        } else {
            level = std::atoi(argv[i]);
        }
    }

    try {
        auto display = spartacus::Display::open();

        if (sweep) {
            std::printf("Sweeping 0%% -> 100%% -> 0%% (writes panel NVM; demo only)\n");
            for (int b = 0; b <= 100; b += 10) {
                display.setBrightness(b);
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
            for (int b = 100; b >= 0; b -= 10) {
                display.setBrightness(b);
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
        }

        std::printf("Setting brightness to %d%%\n", level);
        display.setBrightness(level);
        std::printf("Done (brightness is now %d%%; the panel remembers it).\n",
                    display.brightness());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

// orientation.cpp — set the panel orientation.
//
//   orientation [value] [--cycle]
//
// value is 0..3 (upright=1, left=2, inverted=3, right=0; default 1). With --cycle, first
// steps through all four orientations, then applies the requested one.
//
// Orientation and brightness always travel together in one command; the library retains
// the current brightness and resends it automatically. The panel stores orientation in
// NON-VOLATILE memory, so set it only when it changes — the cycle (5 writes) is opt-in.
#include <spartacus/Spartacus.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
const char* name(int orientation) {
    switch (orientation & 0x03) {
        case spartacus::protocol::kOrientUpright:  return "upright (0deg)";
        case spartacus::protocol::kOrientLeft:     return "left (90deg)";
        case spartacus::protocol::kOrientInverted: return "inverted (180deg)";
        default:                                   return "right (270deg)";
    }
}
}  // namespace

int main(int argc, char** argv) {
    using namespace spartacus::protocol;
    int target = kOrientUpright;
    bool cycle = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cycle") == 0) {
            cycle = true;
        } else {
            target = std::atoi(argv[i]);
        }
    }

    try {
        auto display = spartacus::Display::open();

        if (cycle) {
            std::printf("Cycling through all four orientations (writes panel NVM; demo only)\n");
            const int order[] = {kOrientUpright, kOrientLeft, kOrientInverted, kOrientRight};
            for (int o : order) {
                std::printf("  -> %s\n", name(o));
                display.setOrientation(o);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        std::printf("Setting orientation to %s\n", name(target));
        display.setOrientation(target);
        std::printf("Done (the panel remembers it).\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

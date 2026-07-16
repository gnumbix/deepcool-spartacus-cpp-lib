// linker_argb.cpp — demonstrate all four ARGB lighting effects in turn.
//
// Cycles: rainbow -> breathing (red) -> always-on (green) -> hand lighting to the
// motherboard. Fan settings are preserved across effect changes (the report is stateful).
#include <spartacus/Spartacus.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    try {
        auto linker = spartacus::Linker::open();

        std::printf("Rainbow\n");
        linker.setRainbow(/*speed=*/0xFA, /*saturation=*/0x0A);
        std::this_thread::sleep_for(2s);

        std::printf("Breathing (red)\n");
        linker.setBreathing({0xFF, 0x00, 0x00}, /*speed=*/0x0A);
        std::this_thread::sleep_for(2s);

        std::printf("Always-on (green)\n");
        linker.setAlwaysOn({0x00, 0xFF, 0x00});
        std::this_thread::sleep_for(2s);

        std::printf("Lighting -> motherboard control\n");
        linker.lightingToMotherboard();

        std::printf("Done.\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

// native_telemetry.cpp — drive the panel's built-in temperature + usage readout.
//
//   native_telemetry [seconds]   how long to run the loop (default 15)
//
// Values here are SYNTHETIC (a smooth synthetic waveform), not real sensors — this only
// demonstrates the wire protocol. Read your CPU's temperature/usage from the OS in a real app.
#include <spartacus/Spartacus.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char** argv) {
    const int seconds = (argc > 1) ? std::atoi(argv[1]) : 15;
    try {
        auto display = spartacus::Display::open();

        // Leave streamed-image mode so the device renders its own readout.
        display.sessionStop();
        std::printf("Pushing synthetic telemetry for %d s (Ctrl-C to stop)...\n", seconds);

        for (int t = 0; t < seconds; ++t) {
            const int tempC = 45 + static_cast<int>(20 * std::sin(t * 0.4) + 20);  // ~45..85
            const int usage = 30 + static_cast<int>(35 * std::sin(t * 0.7) + 35);  // ~30..100
            display.pushTelemetry(tempC, usage);
            std::printf("  t=%2ds  temp=%3d C  usage=%3d %%\n", t, tempC, usage);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Return to normal streamed-display mode.
        display.sessionStart();
        std::printf("Done (returned to streamed display).\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

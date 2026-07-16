// linker_set_speeds.cpp — set pump / AIO / EXT fan duty cycles.
//
//   linker_set_speeds <pump> <aio> <ext1> <ext2> [--force]
//
// Each value is a duty percentage 0..100. The pump cools the CPU: this example REFUSES a
// pump duty below 40 % unless --force is given (safety guard; the library itself does not
// enforce it). Use '-' for any channel to leave it unchanged.
#include <spartacus/Spartacus.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace {
// Parse a duty argument: "-" means "leave unchanged" (std::nullopt).
std::optional<int> parseDuty(const char* arg) {
    if (std::strcmp(arg, "-") == 0) return std::nullopt;
    return std::atoi(arg);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: %s <pump> <aio> <ext1> <ext2> [--force]\n"
                     "       duty 0..100, or '-' to leave a channel unchanged\n",
                     argv[0]);
        return 2;
    }

    bool force = false;
    for (int i = 5; i < argc; ++i) {
        if (std::strcmp(argv[i], "--force") == 0) force = true;
    }

    const std::optional<int> pump = parseDuty(argv[1]);
    const std::optional<int> aio = parseDuty(argv[2]);
    const std::optional<int> ext1 = parseDuty(argv[3]);
    const std::optional<int> ext2 = parseDuty(argv[4]);

    if (pump && *pump < spartacus::protocol::kPumpDutyFloor && !force) {
        std::fprintf(stderr,
                     "refusing pump duty %d%% (< %d%%): the pump cools the CPU.\n"
                     "Re-run with --force if you really mean it.\n",
                     *pump, spartacus::protocol::kPumpDutyFloor);
        return 3;
    }

    try {
        auto linker = spartacus::Linker::open();
        linker.setFans(pump, aio, ext1, ext2, /*ramp=*/std::nullopt);
        std::printf("Fan duties updated.\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

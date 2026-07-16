// linker_motherboard_sync.cpp — hand fan + lighting control to the motherboard, or reclaim it.
//
//   linker_motherboard_sync [on|off]     default: on
//
// on : effect -> motherboard, fan sync -> motherboard, channel sources -> motherboard
//      (EXT2 keeps its software flag, matching the reference/captured behaviour).
// off: effect -> rainbow, fan sync -> software, all channel sources -> software.
#include <spartacus/Spartacus.hpp>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    bool enable = true;
    if (argc > 1 && std::strcmp(argv[1], "off") == 0) enable = false;

    try {
        auto linker = spartacus::Linker::open();
        linker.motherboardSync(enable);
        std::printf("Motherboard sync %s.\n", enable ? "ENABLED" : "disabled (software control)");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

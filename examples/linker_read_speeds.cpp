// linker_read_speeds.cpp — poll the Linker and print pump / AIO / EXT fan tachometry.
//
// Uses readStatusPassive(): the solicit report asserts no software control, so reading
// RPMs never changes pump/fan speeds or lighting (readStatus() would apply the retained
// control state — the library defaults, on a fresh handle like this one).
#include <spartacus/Spartacus.hpp>

#include <cstdio>

int main() {
    try {
        auto linker = spartacus::Linker::open();
        auto s = linker.readStatusPassive();
        std::printf("Fan tachometry (RPM)%s:\n", s.checksumOk ? "" : "  [checksum mismatch!]");
        std::printf("  pump : %d\n", s.pump);
        std::printf("  aio  : %d\n", s.aio);
        std::printf("  ext1 : %d%s\n", s.ext1, s.ext1 == 0 ? "  (no fan connected)" : "");
        std::printf("  ext2 : %d%s\n", s.ext2, s.ext2 == 0 ? "  (no fan connected)" : "");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

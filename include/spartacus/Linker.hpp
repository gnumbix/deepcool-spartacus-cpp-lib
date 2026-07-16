// Linker.hpp — RAII handle for the SPARTACUS 360 / 420 fan/ARGB controller
// (USB 3633:002D, HID interrupt).
//
// Open with the static factory Linker::open(); the destructor closes the device. The
// object is move-only. It retains the full 64-byte control report between calls so that,
// e.g., changing fan speeds preserves the current lighting settings — every transmission
// carries the complete state, as the protocol requires.
//
// Thread safety: an instance is not internally synchronized — use it from one thread at a
// time (or guard it externally). Distinct instances are independent; each owns a private
// libusb context.
#ifndef SPARTACUS_LINKER_HPP
#define SPARTACUS_LINKER_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include "spartacus/Error.hpp"
#include "spartacus/Protocol.hpp"  // spartacus::LinkerStatus

namespace spartacus {

namespace detail {
class UsbDevice;  // opaque libusb wrapper; defined privately in src/UsbDevice.hpp
}

class Linker {
public:
    // Open the Linker by VID:PID (claims interface 0). Throws DeviceNotFoundError if
    // absent, UsbError on a libusb failure.
    static Linker open();

    ~Linker();
    Linker(Linker&&) noexcept;
    Linker& operator=(Linker&&) noexcept;
    Linker(const Linker&) = delete;
    Linker& operator=(const Linker&) = delete;

    // ── fans / pump ──────────────────────────────────────────────────────────────
    // Set one or more channel duties (0..100 %). ramp (0..255) smooths speed changes;
    // 0 = immediate. Channels left as std::nullopt keep their current value.
    void setFans(std::optional<int> pump, std::optional<int> aio, std::optional<int> ext1,
                 std::optional<int> ext2, std::optional<int> ramp = std::nullopt);

    // Poll and return the tachometer readings (RPM; big-endian on the wire). The device
    // only replies to a host poll, and the protocol has no read-only poll: this solicits
    // the reply by transmitting the full retained control report, (re)applying it to the
    // device. On a fresh handle the retained report is the library default (rainbow,
    // pump 60 % / fans 40 %) — monitoring tools that must not take over fan or lighting
    // control should call readStatusPassive() instead.
    LinkerStatus readStatus();

    // Poll tachometry WITHOUT asserting software control: solicits the reply with a
    // neutral copy of the retained report (motherboard effect, every channel source =
    // motherboard), which the device answers while ignoring all duty bytes. Safe by
    // construction for monitors — it can never change pump or fan speed. Note that it
    // hands fans/lighting to the motherboard: if this handle was actively controlling
    // them, the next setter or send() re-asserts the retained software state.
    LinkerStatus readStatusPassive();

    // ── ARGB lighting ────────────────────────────────────────────────────────────
    void setRainbow(int speed = 0xFA, int saturation = 0x0A);
    void setBreathing(std::array<std::uint8_t, 3> rgb, int speed = 0x0A);
    void setAlwaysOn(std::array<std::uint8_t, 3> rgb);
    void lightingToMotherboard();

    // ── motherboard synchronization ──────────────────────────────────────────────
    // Hand fan (and lighting) control to the motherboard, or take it back in software.
    void motherboardSync(bool enable = true);

    // Recompute the checksum and transmit the full retained control report.
    void send();

    // Read-only view of the retained report (useful for inspection / tests).
    const std::array<std::uint8_t, 64>& report() const noexcept { return report_; }

private:
    explicit Linker(std::unique_ptr<detail::UsbDevice> dev);

    std::unique_ptr<detail::UsbDevice> dev_;
    std::array<std::uint8_t, 64> report_;
};

}  // namespace spartacus

#endif  // SPARTACUS_LINKER_HPP

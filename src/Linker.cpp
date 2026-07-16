// Linker.cpp — fan/ARGB controller: retained 64-byte report, fan control, lighting, tach.
#include "spartacus/Linker.hpp"

#include <array>
#include <utility>

#include "spartacus/Protocol.hpp"
#include "UsbDevice.hpp"

namespace spartacus {

Linker::Linker(std::unique_ptr<detail::UsbDevice> dev)
    : dev_(std::move(dev)), report_(protocol::default_linker_report()) {}

Linker Linker::open() {
    auto dev = std::make_unique<detail::UsbDevice>(protocol::kVendorId, protocol::kPidLinker,
                                                   "SPARTACUS Linker");
    // The Linker is a HID device; after usbhid is detached its interrupt endpoints can be
    // left halted, failing the first poll/read with EIO. Clear both so a fresh claim —
    // including every reconnect — transfers cleanly (best effort, harmless when not halted).
    dev->clearHalt(protocol::kEpLinkerOut);
    dev->clearHalt(protocol::kEpLinkerIn);
    return Linker(std::move(dev));
}

Linker::~Linker() = default;
Linker::Linker(Linker&&) noexcept = default;
Linker& Linker::operator=(Linker&&) noexcept = default;

void Linker::send() {
    protocol::finalize_linker_report(report_);
    dev_->interruptOut(protocol::kEpLinkerOut, report_.data(), report_.size());
}

void Linker::setFans(std::optional<int> pump, std::optional<int> aio, std::optional<int> ext1,
                     std::optional<int> ext2, std::optional<int> ramp) {
    protocol::report_set_fans(report_, pump, aio, ext1, ext2, ramp);
    send();
}

LinkerStatus Linker::readStatus() {
    send();  // solicit a status report by resending the current control report
    std::array<std::uint8_t, 64> buf{};
    int n = dev_->interruptIn(protocol::kEpLinkerIn, buf.data(), buf.size());
    return protocol::decode_status(buf.data(), static_cast<std::size_t>(n));
}

LinkerStatus Linker::readStatusPassive() {
    // Solicit with a neutral all-motherboard poll built from a copy; the retained report
    // is left untouched so a later setter/send() re-asserts the caller's software state.
    auto poll = report_;
    protocol::report_telemetry_poll(poll);
    protocol::finalize_linker_report(poll);
    dev_->interruptOut(protocol::kEpLinkerOut, poll.data(), poll.size());
    std::array<std::uint8_t, 64> buf{};
    int n = dev_->interruptIn(protocol::kEpLinkerIn, buf.data(), buf.size());
    return protocol::decode_status(buf.data(), static_cast<std::size_t>(n));
}

void Linker::setRainbow(int speed, int saturation) {
    protocol::report_set_rainbow(report_, speed, saturation);
    send();
}

void Linker::setBreathing(std::array<std::uint8_t, 3> rgb, int speed) {
    protocol::report_set_breathing(report_, rgb, speed);
    send();
}

void Linker::setAlwaysOn(std::array<std::uint8_t, 3> rgb) {
    protocol::report_set_always_on(report_, rgb);
    send();
}

void Linker::lightingToMotherboard() {
    protocol::report_lighting_to_motherboard(report_);
    send();
}

void Linker::motherboardSync(bool enable) {
    protocol::report_motherboard_sync(report_, enable);
    send();
}

}  // namespace spartacus

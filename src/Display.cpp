// Display.cpp — LCD device: session, orientation/brightness, telemetry, raw JPEG upload.
//
// Image helpers (showRgb / showJpegFile / clear) live in src/Image.cpp so the JPEG encoder
// is only pulled in when image support is enabled.
#include "spartacus/Display.hpp"

#include <utility>

#include "spartacus/Protocol.hpp"
#include "UsbDevice.hpp"

namespace spartacus {

Display::Display(std::unique_ptr<detail::UsbDevice> dev)
    : dev_(std::move(dev)),
      orientation_(protocol::kOrientUpright),
      brightness_(protocol::kBrightnessMax) {}

Display Display::open() {
    auto dev = std::make_unique<detail::UsbDevice>(protocol::kVendorId, protocol::kPidDisplay,
                                                   "SPARTACUS display");
    return Display(std::move(dev));
}

Display::~Display() = default;
Display::Display(Display&&) noexcept = default;
Display& Display::operator=(Display&&) noexcept = default;

void Display::sendControl(const std::uint8_t* data, std::size_t len) {
    dev_->bulkOut(protocol::kEpCtrl, data, len);
}

void Display::sendImage(const std::uint8_t* data, std::size_t len) {
    dev_->bulkOut(protocol::kEpImage, data, len);
}

void Display::sessionStart() {
    auto pkt = protocol::build_session(true);
    sendControl(pkt.data(), pkt.size());
}

void Display::sessionStop() {
    auto pkt = protocol::build_session(false);
    sendControl(pkt.data(), pkt.size());
}

void Display::setDisplay(std::optional<int> orientation, std::optional<int> brightness) {
    if (orientation) orientation_ = *orientation & 0x03;
    if (brightness) {
        int b = *brightness;
        brightness_ = b < protocol::kBrightnessMin
                          ? protocol::kBrightnessMin
                          : (b > protocol::kBrightnessMax ? protocol::kBrightnessMax : b);
    }
    auto pkt = protocol::build_display_config(orientation_, brightness_);
    sendControl(pkt.data(), pkt.size());
}

void Display::setOrientation(int orientation) { setDisplay(orientation, std::nullopt); }

void Display::setBrightness(int percent) { setDisplay(std::nullopt, percent); }

void Display::pushTelemetry(int tempC, int usagePct) {
    auto pkt = protocol::build_telemetry(tempC, usagePct);
    sendControl(pkt.data(), pkt.size());
}

void Display::showJpeg(const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0) {
        throw Error("showJpeg: empty image");
    }
    if (len < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        throw Error("showJpeg: not a JPEG (missing FF D8 start-of-image marker)");
    }
    // The START packet carries the DATA-packet count as a u16.
    if (protocol::image_chunk_count(len) > 0xFFFF) {
        throw Error("showJpeg: image too large for the chunked transfer format");
    }

    auto start = protocol::build_image_start(data, len);
    sendImage(start.data(), start.size());

    const std::size_t n = protocol::image_chunk_count(len);
    for (std::size_t i = 1; i <= n; ++i) {
        auto packet = protocol::build_image_data(data, len, static_cast<std::uint16_t>(i));
        sendImage(packet.data(), packet.size());
    }

    auto finish = protocol::build_image_finish();
    sendImage(finish.data(), finish.size());
}

}  // namespace spartacus

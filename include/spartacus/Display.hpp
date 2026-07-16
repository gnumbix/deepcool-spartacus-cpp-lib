// Display.hpp — RAII handle for the SPARTACUS 360 / 420 LCD (USB 3633:0027, vendor bulk).
//
// Open with the static factory Display::open(); the destructor closes the device. The
// object is move-only. All methods throw spartacus::Error (or a subclass) on failure.
//
// Thread safety: an instance is not internally synchronized — use it from one thread at a
// time (or guard it externally). Distinct instances are independent; each owns a private
// libusb context.
#ifndef SPARTACUS_DISPLAY_HPP
#define SPARTACUS_DISPLAY_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "spartacus/Error.hpp"

namespace spartacus {

namespace detail {
class UsbDevice;  // opaque libusb wrapper; defined privately in src/UsbDevice.hpp
}

class Display {
public:
    // Open the LCD by VID:PID (claims interface 0). Throws DeviceNotFoundError if absent,
    // UsbError on a libusb failure.
    static Display open();

    ~Display();
    Display(Display&&) noexcept;
    Display& operator=(Display&&) noexcept;
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    // ── session control ──────────────────────────────────────────────────────────
    void sessionStart();  // enable/resume the image stream (send once after connecting)
    void sessionStop();   // stop the image stream (before native telemetry mode)

    // ── orientation / brightness (sent together in one packet) ───────────────────
    // Updates whichever fields are provided, retaining the other, then transmits both.
    // Brightness is clamped to [0,100] (0 = black); orientation is masked to [0x00,0x03].
    // The panel persists both in non-volatile memory, so call this only on a change.
    void setDisplay(std::optional<int> orientation, std::optional<int> brightness);
    void setOrientation(int orientation);
    void setBrightness(int percent);

    // ── native "always-on" readout ───────────────────────────────────────────────
    // Push CPU temperature (°C) and usage (%). Call sessionStop() first, then ~1 Hz.
    void pushTelemetry(int tempC, int usagePct);

    // ── image transfer ───────────────────────────────────────────────────────────
    // Upload a raw baseline 480×480 JPEG byte stream (START / trans / DCLdfinish).
    // Throws Error if the buffer is empty, lacks the JPEG SOI marker (FF D8), or is too
    // large for the chunked transfer format; the bytes are otherwise sent unchanged.
    void showJpeg(const std::uint8_t* data, std::size_t len);

    // Image helpers. These are compiled only when the library is built with image
    // support (SPARTACUS_WITH_IMAGE, on by default). Calling them against a core-only
    // build is a link error. showJpeg() above needs no image support.
    //
    // Resize (if needed) to 480×480, encode a baseline JPEG, and upload it.
    // comp is 3 (RGB) or 4 (RGBA, alpha ignored); quality is 1..100.
    void showRgb(const std::uint8_t* pixels, int w, int h, int comp, int quality = 90);
    // Read a baseline .jpg from disk and upload its bytes unchanged (no decode).
    void showJpegFile(const std::string& path);
    // Blank the panel with a solid-colour 480×480 frame (never lowers brightness).
    void clear(std::uint8_t r, std::uint8_t g, std::uint8_t b);

    // Current retained orientation / brightness.
    int orientation() const noexcept { return orientation_; }
    int brightness() const noexcept { return brightness_; }

private:
    explicit Display(std::unique_ptr<detail::UsbDevice> dev);
    void sendControl(const std::uint8_t* data, std::size_t len);
    void sendImage(const std::uint8_t* data, std::size_t len);

    std::unique_ptr<detail::UsbDevice> dev_;
    int orientation_;
    int brightness_;
};

}  // namespace spartacus

#endif  // SPARTACUS_DISPLAY_HPP

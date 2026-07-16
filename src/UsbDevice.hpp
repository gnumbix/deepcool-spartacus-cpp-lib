// UsbDevice.hpp — private RAII wrapper over a libusb device handle.
//
// This header is NOT installed and is not part of the public API: it keeps libusb out of
// the public headers so consumers need not have libusb-1.0 headers on their include path.
// Only src/*.cpp include it.
#ifndef SPARTACUS_DETAIL_USBDEVICE_HPP
#define SPARTACUS_DETAIL_USBDEVICE_HPP

#include <libusb.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

#include "spartacus/Error.hpp"

namespace spartacus {
namespace detail {

// Opens one device by VID:PID, detaches any kernel driver (and leaves it detached), sets
// configuration 1, and claims interface 0. Releases and closes on destruction. Move-only.
//
// The bring-up sequence below (explicit detach, unconditional SET_CONFIGURATION, callers
// clearing endpoint halts) mirrors the upstream reference driver, where it was verified on
// real hardware to make first contact and reconnects reliable.
class UsbDevice {
public:
    UsbDevice(std::uint16_t vid, std::uint16_t pid, const char* humanName) {
        int rc = libusb_init(&ctx_);
        if (rc < 0) {
            ctx_ = nullptr;
            throw UsbError(std::string("libusb_init failed: ") + libusb_error_name(rc), rc);
        }

        handle_ = libusb_open_device_with_vid_pid(ctx_, vid, pid);
        if (handle_ == nullptr) {
            libusb_exit(ctx_);
            ctx_ = nullptr;
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                          "%s (%04X:%04X) not found — is it plugged in and not held by the "
                          "DeepCool app?",
                          humanName, vid, pid);
            throw DeviceNotFoundError(msg);
        }

        // Detach any kernel driver (usbhid on the Linker) explicitly, and leave it detached
        // on close. Deliberately NOT libusb_set_auto_detach_kernel_driver(): auto-detach
        // re-attaches the kernel driver when the interface is released — i.e. on every
        // close — which races usbhid re-binding on the next open and can leave reconnects
        // unable to reclaim the HID Linker. The check/detach pair is Linux-only; anywhere
        // it is unsupported there is nothing to detach.
        if (libusb_kernel_driver_active(handle_, iface_) == 1) {
            rc = libusb_detach_kernel_driver(handle_, iface_);
            if (rc < 0) {
                libusb_close(handle_);
                handle_ = nullptr;
                libusb_exit(ctx_);
                ctx_ = nullptr;
                throw UsbError(
                    std::string("detach_kernel_driver failed: ") + libusb_error_name(rc), rc);
            }
        }

        // Always (re)issue SET_CONFIGURATION, even when configuration 1 is already active.
        // On an already-configured device libusb treats this as a lightweight reset — it
        // clears endpoint halts and stale data toggles — which the HID Linker needs after
        // its kernel driver is detached (otherwise its interrupt endpoints can stay halted
        // and every transfer fails with EIO). Best effort by design.
        libusb_set_configuration(handle_, 1);

        rc = libusb_claim_interface(handle_, iface_);
        if (rc < 0) {
            libusb_close(handle_);
            handle_ = nullptr;
            libusb_exit(ctx_);
            ctx_ = nullptr;
            throw UsbError(std::string("claim_interface(0) failed: ") + libusb_error_name(rc) +
                               " — is the DeepCool app or another process holding the device?",
                           rc);
        }
    }

    ~UsbDevice() { close(); }

    UsbDevice(const UsbDevice&) = delete;
    UsbDevice& operator=(const UsbDevice&) = delete;

    UsbDevice(UsbDevice&& other) noexcept
        : ctx_(other.ctx_), handle_(other.handle_), iface_(other.iface_) {
        other.ctx_ = nullptr;
        other.handle_ = nullptr;
    }

    UsbDevice& operator=(UsbDevice&& other) noexcept {
        if (this != &other) {
            close();
            ctx_ = other.ctx_;
            handle_ = other.handle_;
            iface_ = other.iface_;
            other.ctx_ = nullptr;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void bulkOut(std::uint8_t endpoint, const std::uint8_t* data, std::size_t len,
                 unsigned timeoutMs = 2000) {
        transferOut(endpoint, data, len, timeoutMs, /*interrupt=*/false, "bulk OUT");
    }

    void interruptOut(std::uint8_t endpoint, const std::uint8_t* data, std::size_t len,
                      unsigned timeoutMs = 2000) {
        transferOut(endpoint, data, len, timeoutMs, /*interrupt=*/true, "interrupt OUT");
    }

    // Best-effort clear of an endpoint halt. After the kernel HID driver is detached the
    // Linker's interrupt endpoints can be left halted with a stale data toggle, surfacing
    // as EIO on the first transfer; clearing a non-halted endpoint is harmless. Callers
    // invoke this once per endpoint right after open.
    void clearHalt(std::uint8_t endpoint) noexcept { libusb_clear_halt(handle_, endpoint); }

    // Reads up to len bytes; returns the number actually transferred.
    int interruptIn(std::uint8_t endpoint, std::uint8_t* data, std::size_t len,
                    unsigned timeoutMs = 2000) {
        int transferred = 0;
        int rc = libusb_interrupt_transfer(handle_, endpoint, data, static_cast<int>(len),
                                           &transferred, timeoutMs);
        if (rc < 0) {
            throw UsbError(std::string("interrupt IN failed: ") + libusb_error_name(rc), rc);
        }
        return transferred;
    }

private:
    void transferOut(std::uint8_t endpoint, const std::uint8_t* data, std::size_t len,
                     unsigned timeoutMs, bool interrupt, const char* what) {
        int transferred = 0;
        // libusb takes a non-const buffer even for OUT transfers; the cast is safe.
        auto* buf = const_cast<unsigned char*>(data);
        int rc = interrupt ? libusb_interrupt_transfer(handle_, endpoint, buf,
                                                        static_cast<int>(len), &transferred,
                                                        timeoutMs)
                           : libusb_bulk_transfer(handle_, endpoint, buf, static_cast<int>(len),
                                                  &transferred, timeoutMs);
        if (rc < 0) {
            throw UsbError(std::string(what) + " failed: " + libusb_error_name(rc), rc);
        }
    }

    void close() {
        if (handle_ != nullptr) {
            libusb_release_interface(handle_, iface_);
            libusb_close(handle_);
            handle_ = nullptr;
        }
        if (ctx_ != nullptr) {
            libusb_exit(ctx_);
            ctx_ = nullptr;
        }
    }

    libusb_context* ctx_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
    int iface_ = 0;
};

}  // namespace detail
}  // namespace spartacus

#endif  // SPARTACUS_DETAIL_USBDEVICE_HPP

// Error.hpp — exception hierarchy for libspartacus.
//
//   spartacus::Error               base for everything the library throws
//     ├─ spartacus::DeviceNotFoundError   the USB device is absent / not enumerable
//     └─ spartacus::UsbError              a libusb transfer or setup call failed
#ifndef SPARTACUS_ERROR_HPP
#define SPARTACUS_ERROR_HPP

#include <stdexcept>
#include <string>

namespace spartacus {

// Base class for all library errors; derives from std::runtime_error so callers may catch
// std::exception generically.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Thrown when the requested SPARTACUS device (by VID:PID) cannot be opened — usually not
// plugged in, or still held by the official DeepCool application.
class DeviceNotFoundError : public Error {
public:
    using Error::Error;
};

// Thrown on any libusb failure (init, claim, transfer). code() carries the libusb error code.
class UsbError : public Error {
public:
    UsbError(const std::string& message, int code) : Error(message), code_(code) {}
    int code() const noexcept { return code_; }

private:
    int code_;
};

}  // namespace spartacus

#endif  // SPARTACUS_ERROR_HPP

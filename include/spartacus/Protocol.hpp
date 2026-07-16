// Protocol.hpp — pure, hardware-free wire-format layer for the DeepCool SPARTACUS 360.
//
// Everything here is a free function or a compile-time constant: no USB, no I/O, no
// global state. The Display and Linker classes call these builders and then transmit
// the bytes. Because the layer is pure it is exhaustively covered by the golden-vector
// unit tests (tests/test_vectors.cpp) with no device attached.
//
// See docs/protocol-specification.md (in this repository) for the authoritative wire spec.
#ifndef SPARTACUS_PROTOCOL_HPP
#define SPARTACUS_PROTOCOL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace spartacus {

// Tachometer readings decoded from a Linker status report (RPM; 0 = no fan connected).
struct LinkerStatus {
    int pump = 0;
    int aio = 0;
    int ext1 = 0;
    int ext2 = 0;
    // True when the report's stored SUM8 checksum (byte [37], over bytes [1:37]) matched a
    // freshly computed one. False also whenever the buffer was too short to carry it.
    bool checksumOk = false;
};

namespace protocol {

// ── USB identity ────────────────────────────────────────────────────────────────
constexpr std::uint16_t kVendorId = 0x3633;
constexpr std::uint16_t kPidDisplay = 0x0027;  // "SPARTACUS"        — LCD, vendor bulk
constexpr std::uint16_t kPidLinker = 0x002D;   // "SPARTACUS Linker" — fans/ARGB, HID

// ── Display endpoints ───────────────────────────────────────────────────────────
constexpr std::uint8_t kEpImage = 0x02;  // bulk OUT — image frames
constexpr std::uint8_t kEpCtrl = 0x04;   // bulk OUT — control commands

// ── Linker endpoints ────────────────────────────────────────────────────────────
constexpr std::uint8_t kEpLinkerOut = 0x01;  // interrupt OUT — control report
constexpr std::uint8_t kEpLinkerIn = 0x81;   // interrupt IN  — status report

// ── Display control commands (byte [2] of a 46-byte AA 2E frame) ─────────────────
constexpr std::uint8_t kCmdSession = 0x05;
constexpr std::uint8_t kCmdConfig = 0x04;
constexpr std::uint8_t kCmdTelemetry = 0x01;

// ── Display orientation (byte [3] of a config frame). Upright is 0x01, +90° CCW. ──
constexpr int kOrientUpright = 0x01;   // 0°
constexpr int kOrientLeft = 0x02;      // 90°  (rotated left)
constexpr int kOrientInverted = 0x03;  // 180° (upside down)
constexpr int kOrientRight = 0x00;     // 270° (rotated right)

// ── Brightness limits (percent). 0 is valid — it renders a black screen. ─────────
// (An earlier revision set the floor to 10 believing 0 glitched the backlight;
// that was wrong and is retracted.)
constexpr int kBrightnessMin = 0;
constexpr int kBrightnessMax = 100;

// ── Linker ARGB effect modes (report byte [6]) ───────────────────────────────────
constexpr std::uint8_t kEffectMotherboard = 0x01;
constexpr std::uint8_t kEffectBreathing = 0x02;
constexpr std::uint8_t kEffectRainbow = 0x03;
constexpr std::uint8_t kEffectAlwaysOn = 0x04;

// ── Linker source flags (fan-sync + per-channel source) ──────────────────────────
constexpr std::uint8_t kSourceSoftware = 0x01;
constexpr std::uint8_t kSourceMotherboard = 0x00;

// ── Linker report identity ───────────────────────────────────────────────────────
constexpr std::uint8_t kLinkerReportId = 0x10;  // byte [0] of every control/status report

// ── Linker channel base offsets in the 64-byte report ────────────────────────────
constexpr std::size_t kChPump = 17;
constexpr std::size_t kChAio = 20;
constexpr std::size_t kChExt1 = 23;
constexpr std::size_t kChExt2 = 26;

// Reference pump-duty floor. The pump cools the CPU; treat lower as unsafe (the
// example layer refuses it without an explicit override).
constexpr int kPumpDutyFloor = 40;

// Image transfer geometry: every packet is 512 bytes; each DATA packet carries up to
// 505 payload bytes (payload begins at offset 7).
constexpr std::size_t kImagePacketSize = 512;
constexpr std::size_t kImageChunkSize = 505;

// ── Checksums ────────────────────────────────────────────────────────────────────
// SUM16: 16-bit additive sum, little-endian on the wire (display control + image).
std::uint16_t sum16(const std::uint8_t* data, std::size_t len) noexcept;
// SUM8: 8-bit additive sum (Linker report, over report bytes [1:37]).
std::uint8_t sum8(const std::uint8_t* data, std::size_t len) noexcept;

// ── Display control builders (46-byte AA 2E frames, SUM16 over [0:44]) ───────────
std::array<std::uint8_t, 46> build_session(bool start);
// Clamps brightness to [0,100] (0 = black) and masks orientation to [0x00,0x03]; both sent together.
std::array<std::uint8_t, 46> build_display_config(int orientation, int brightness);
// Native "always-on" readout: only temperature [3] and usage [6] are live. tempC is
// clamped to [0,255] and usagePct to [0,100].
std::array<std::uint8_t, 46> build_telemetry(int tempC, int usagePct);

// ── Display image builders (512-byte packets) ────────────────────────────────────
// Number of DATA packets: ceil(len / 505).
std::size_t image_chunk_count(std::size_t jpegLen) noexcept;
std::array<std::uint8_t, 512> build_image_start(const std::uint8_t* jpeg, std::size_t len);
// seq is 1-based (1..N); copies the seq-th 505-byte chunk to offset 7.
std::array<std::uint8_t, 512> build_image_data(const std::uint8_t* jpeg, std::size_t len,
                                               std::uint16_t seq);
std::array<std::uint8_t, 512> build_image_finish();

// ── Linker report builder (64-byte report, id 0x10) ──────────────────────────────
// A fully-populated default report matching the reference driver's initial state.
std::array<std::uint8_t, 64> default_linker_report();

// The following mutate a report in place but do NOT recompute the checksum — call
// finalize_linker_report() (or Linker::send()) once before transmitting, mirroring the
// reference driver where each setter ends with send().
void report_set_fans(std::array<std::uint8_t, 64>& report, std::optional<int> pump,
                     std::optional<int> aio, std::optional<int> ext1, std::optional<int> ext2,
                     std::optional<int> ramp);
void report_set_rainbow(std::array<std::uint8_t, 64>& report, int speed, int saturation);
void report_set_breathing(std::array<std::uint8_t, 64>& report, std::array<std::uint8_t, 3> rgb,
                          int speed);
void report_set_always_on(std::array<std::uint8_t, 64>& report, std::array<std::uint8_t, 3> rgb);
void report_lighting_to_motherboard(std::array<std::uint8_t, 64>& report);
void report_motherboard_sync(std::array<std::uint8_t, 64>& report, bool enable);
// Turn the report into a neutral telemetry poll: motherboard effect, fan-sync flag off,
// and EVERY channel source = motherboard (unlike report_motherboard_sync, which keeps the
// EXT2 quirk). With no software control asserted the device ignores the duty bytes, so
// soliciting a status reply with this report can never change pump/fan speed or lighting.
void report_telemetry_poll(std::array<std::uint8_t, 64>& report);

// Write the SUM8 checksum into byte [37] (computed over bytes [1:37]).
void finalize_linker_report(std::array<std::uint8_t, 64>& report);

// Decode tachometry from a status report (big-endian on the wire). The buffer must be at
// least 37 bytes and carry the report id 0x10 at [0]; otherwise all-zero readings with
// checksumOk = false are returned. checksumOk reflects the stored SUM8 at byte [37].
LinkerStatus decode_status(const std::uint8_t* data, std::size_t len);

}  // namespace protocol
}  // namespace spartacus

#endif  // SPARTACUS_PROTOCOL_HPP

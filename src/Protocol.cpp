// Protocol.cpp — implementation of the pure wire-format builders.
//
// No USB, no I/O: this translation unit has no external dependencies beyond the standard
// library, so the golden-vector tests link only against it.
#include "spartacus/Protocol.hpp"

#include <algorithm>
#include <cstring>

namespace spartacus {
namespace protocol {
namespace {

// The verified 44-byte native-telemetry template. Bytes [3] and [6] are the live CPU
// temperature / usage; every other byte selects the on-device layout and is preserved.
constexpr std::array<std::uint8_t, 44> kZenTemplate = {{
    0xAA, 0x2E, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x08, 0x00, 0xD7, 0x03, 0x00, 0x1E, 0x05,  //
    0x00, 0x07, 0x0C, 0x00, 0x02, 0x04, 0x00, 0x3E,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00,                          //
}};

constexpr int clampi(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

void put_le16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

void put_le32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// Finish a 46-byte control frame: append SUM16 over [0:44] little-endian at [44:46].
void seal_control(std::array<std::uint8_t, 46>& pkt) {
    put_le16(pkt.data() + 44, sum16(pkt.data(), 44));
}

}  // namespace

// ── checksums ────────────────────────────────────────────────────────────────────
std::uint16_t sum16(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t acc = 0;
    for (std::size_t i = 0; i < len; ++i) acc += data[i];
    return static_cast<std::uint16_t>(acc & 0xFFFF);
}

std::uint8_t sum8(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t acc = 0;
    for (std::size_t i = 0; i < len; ++i) acc += data[i];
    return static_cast<std::uint8_t>(acc & 0xFF);
}

// ── display control builders ──────────────────────────────────────────────────────
std::array<std::uint8_t, 46> build_session(bool start) {
    std::array<std::uint8_t, 46> pkt{};
    pkt[0] = 0xAA;
    pkt[1] = 0x2E;
    pkt[2] = kCmdSession;
    pkt[3] = start ? 0x01 : 0x00;
    seal_control(pkt);
    return pkt;
}

std::array<std::uint8_t, 46> build_display_config(int orientation, int brightness) {
    std::array<std::uint8_t, 46> pkt{};
    pkt[0] = 0xAA;
    pkt[1] = 0x2E;
    pkt[2] = kCmdConfig;
    pkt[3] = static_cast<std::uint8_t>(orientation & 0x03);
    pkt[4] = static_cast<std::uint8_t>(clampi(brightness, kBrightnessMin, kBrightnessMax));
    seal_control(pkt);
    return pkt;
}

std::array<std::uint8_t, 46> build_telemetry(int tempC, int usagePct) {
    std::array<std::uint8_t, 46> pkt{};
    std::copy(kZenTemplate.begin(), kZenTemplate.end(), pkt.begin());
    // Clamp rather than mask: a (nonsensical) negative temperature must not alias to a
    // high reading — masking turned -5 °C into 251 °C on the panel.
    pkt[3] = static_cast<std::uint8_t>(clampi(tempC, 0, 255));
    pkt[6] = static_cast<std::uint8_t>(clampi(usagePct, 0, 100));
    seal_control(pkt);
    return pkt;
}

// ── display image builders ────────────────────────────────────────────────────────
std::size_t image_chunk_count(std::size_t jpegLen) noexcept {
    return (jpegLen + kImageChunkSize - 1) / kImageChunkSize;
}

std::array<std::uint8_t, 512> build_image_start(const std::uint8_t* jpeg, std::size_t len) {
    std::array<std::uint8_t, 512> pkt{};
    static const char kTag[5] = {'S', 't', 'a', 'r', 't'};
    std::memcpy(pkt.data(), kTag, 5);
    pkt[5] = 0x01;  // frame type: all content
    put_le32(pkt.data() + 6, static_cast<std::uint32_t>(len));
    put_le16(pkt.data() + 10, sum16(jpeg, len));
    put_le16(pkt.data() + 12, static_cast<std::uint16_t>(image_chunk_count(len)));
    return pkt;
}

std::array<std::uint8_t, 512> build_image_data(const std::uint8_t* jpeg, std::size_t len,
                                               std::uint16_t seq) {
    std::array<std::uint8_t, 512> pkt{};
    static const char kTag[5] = {'t', 'r', 'a', 'n', 's'};
    std::memcpy(pkt.data(), kTag, 5);
    put_le16(pkt.data() + 5, seq);  // 1-based sequence number
    const std::size_t offset = static_cast<std::size_t>(seq - 1) * kImageChunkSize;
    const std::size_t remaining = offset < len ? len - offset : 0;
    const std::size_t chunk = std::min(remaining, kImageChunkSize);
    if (chunk > 0 && jpeg != nullptr) {
        std::memcpy(pkt.data() + 7, jpeg + offset, chunk);  // payload begins at offset 7
    }
    return pkt;
}

std::array<std::uint8_t, 512> build_image_finish() {
    std::array<std::uint8_t, 512> pkt{};
    static const char kTag[10] = {'D', 'C', 'L', 'd', 'f', 'i', 'n', 'i', 's', 'h'};
    std::memcpy(pkt.data(), kTag, 10);
    return pkt;
}

// ── Linker report builder ─────────────────────────────────────────────────────────
std::array<std::uint8_t, 64> default_linker_report() {
    std::array<std::uint8_t, 64> r{};
    r[0] = kLinkerReportId;
    r[1] = 0x68;
    r[2] = 0x05;
    r[3] = 0x02;
    r[4] = 0x20;
    r[5] = 0x08;  // fixed header
    r[6] = kEffectRainbow;  // default effect
    r[7] = 0x0A;            // breathing speed
    r[8] = 0xFF;
    r[9] = 0x00;
    r[10] = 0xFF;  // breathing colour
    r[11] = 0xFA;  // rainbow speed
    r[12] = 0x0A;  // rainbow saturation
    r[13] = 0x00;
    r[14] = 0x00;
    r[15] = 0xFF;              // always-on colour
    r[16] = kSourceSoftware;  // fan-sync flag
    const struct {
        std::size_t base;
        std::uint8_t duty;
    } channels[] = {{kChPump, 60}, {kChAio, 40}, {kChExt1, 40}, {kChExt2, 40}};
    for (const auto& ch : channels) {
        r[ch.base] = ch.duty;           // speed %
        r[ch.base + 1] = 0x00;          // ramp time (0 = immediate)
        r[ch.base + 2] = kSourceSoftware;  // source
    }
    r[38] = 0x16;  // fixed marker
    return r;
}

void report_set_fans(std::array<std::uint8_t, 64>& report, std::optional<int> pump,
                     std::optional<int> aio, std::optional<int> ext1, std::optional<int> ext2,
                     std::optional<int> ramp) {
    const struct {
        std::size_t base;
        std::optional<int> duty;
    } channels[] = {{kChPump, pump}, {kChAio, aio}, {kChExt1, ext1}, {kChExt2, ext2}};
    for (const auto& ch : channels) {
        if (!ch.duty) continue;
        report[ch.base] = static_cast<std::uint8_t>(clampi(*ch.duty, 0, 100));
        report[ch.base + 2] = kSourceSoftware;
        if (ramp) {
            report[ch.base + 1] = static_cast<std::uint8_t>(clampi(*ramp, 0, 255));
        }
    }
}

void report_set_rainbow(std::array<std::uint8_t, 64>& report, int speed, int saturation) {
    report[6] = kEffectRainbow;
    report[11] = static_cast<std::uint8_t>(speed & 0xFF);
    report[12] = static_cast<std::uint8_t>(saturation & 0xFF);
}

void report_set_breathing(std::array<std::uint8_t, 64>& report, std::array<std::uint8_t, 3> rgb,
                          int speed) {
    report[6] = kEffectBreathing;
    report[7] = static_cast<std::uint8_t>(speed & 0xFF);
    report[8] = rgb[0];
    report[9] = rgb[1];
    report[10] = rgb[2];
}

void report_set_always_on(std::array<std::uint8_t, 64>& report, std::array<std::uint8_t, 3> rgb) {
    report[6] = kEffectAlwaysOn;
    report[13] = rgb[0];
    report[14] = rgb[1];
    report[15] = rgb[2];
}

void report_lighting_to_motherboard(std::array<std::uint8_t, 64>& report) {
    report[6] = kEffectMotherboard;
}

void report_motherboard_sync(std::array<std::uint8_t, 64>& report, bool enable) {
    if (enable) {
        report[6] = kEffectMotherboard;
        report[16] = kSourceMotherboard;
        report[kChPump + 2] = kSourceMotherboard;
        report[kChAio + 2] = kSourceMotherboard;
        report[kChExt1 + 2] = kSourceMotherboard;
        report[kChExt2 + 2] = kSourceSoftware;  // channel 3 (EXT2) keeps its software flag
    } else {
        report[6] = kEffectRainbow;
        report[16] = kSourceSoftware;
        report[kChPump + 2] = kSourceSoftware;
        report[kChAio + 2] = kSourceSoftware;
        report[kChExt1 + 2] = kSourceSoftware;
        report[kChExt2 + 2] = kSourceSoftware;
    }
}

void report_telemetry_poll(std::array<std::uint8_t, 64>& report) {
    report[6] = kEffectMotherboard;
    report[16] = kSourceMotherboard;
    report[kChPump + 2] = kSourceMotherboard;
    report[kChAio + 2] = kSourceMotherboard;
    report[kChExt1 + 2] = kSourceMotherboard;
    report[kChExt2 + 2] = kSourceMotherboard;
}

void finalize_linker_report(std::array<std::uint8_t, 64>& report) {
    report[37] = sum8(report.data() + 1, 36);  // SUM8 over bytes [1:37]
}

LinkerStatus decode_status(const std::uint8_t* data, std::size_t len) {
    LinkerStatus status;
    if (data == nullptr || len < 37 || data[0] != kLinkerReportId) return status;
    status.pump = (data[29] << 8) | data[30];
    status.aio = (data[31] << 8) | data[32];
    status.ext1 = (data[33] << 8) | data[34];
    status.ext2 = (data[35] << 8) | data[36];
    status.checksumOk = len >= 38 && data[37] == sum8(data + 1, 36);
    return status;
}

}  // namespace protocol
}  // namespace spartacus

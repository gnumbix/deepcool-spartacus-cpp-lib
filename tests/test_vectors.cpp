// test_vectors.cpp — golden-vector unit tests for the pure protocol builders.
//
// No hardware, no libusb, no image code: this file plus src/Protocol.cpp is the whole
// program. It asserts the byte-exact vectors from the protocol specification (§6) and exits
// non-zero on any mismatch. Run via `make test`.
//
// Note on the Linker report vectors: the ASCII block in spec §6 is pretty-printed with one
// too few zero bytes before the checksum. The authoritative field layout (§5) and the stated
// values ("[37] = 0x41 / 0x3B") place the SUM8 checksum at byte [37] and the fixed marker
// 0x16 at byte [38]; the checksum arithmetic confirms it. The expected reports below are
// built from that layout.
#include "spartacus/Protocol.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace spartacus;
using namespace spartacus::protocol;

namespace {

int g_checks = 0;
int g_failures = 0;

std::string hex(const std::uint8_t* d, std::size_t n) {
    std::string s;
    char b[4];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02X ", d[i]);
        s += b;
    }
    if (!s.empty()) s.pop_back();
    return s;
}

void expect_bytes(const char* name, const std::uint8_t* got, std::size_t gotLen,
                  const std::vector<std::uint8_t>& expected) {
    ++g_checks;
    bool ok = (gotLen == expected.size());
    if (ok) {
        for (std::size_t i = 0; i < gotLen; ++i) {
            if (got[i] != expected[i]) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        std::printf("ok   %s\n", name);
    } else {
        ++g_failures;
        std::printf("FAIL %s\n  expected (%zu): %s\n  got      (%zu): %s\n", name, expected.size(),
                    hex(expected.data(), expected.size()).c_str(), gotLen,
                    hex(got, gotLen).c_str());
    }
}

void expect_true(const char* name, bool cond) {
    ++g_checks;
    if (cond) {
        std::printf("ok   %s\n", name);
    } else {
        ++g_failures;
        std::printf("FAIL %s\n", name);
    }
}

void expect_eq(const char* name, long got, long expected) {
    ++g_checks;
    if (got == expected) {
        std::printf("ok   %s (%ld)\n", name, got);
    } else {
        ++g_failures;
        std::printf("FAIL %s: expected %ld, got %ld\n", name, expected, got);
    }
}

// Convenience: build a zero-filled vector of length n.
std::vector<std::uint8_t> zeros(std::size_t n) { return std::vector<std::uint8_t>(n, 0); }

// ── display control ────────────────────────────────────────────────────────────────
void test_display_control() {
    {
        auto pkt = build_session(true);
        auto e = zeros(46);
        e[0] = 0xAA; e[1] = 0x2E; e[2] = 0x05; e[3] = 0x01; e[44] = 0xDE; e[45] = 0x00;
        expect_bytes("session start = AA 2E 05 01 ... DE 00", pkt.data(), pkt.size(), e);
    }
    {
        auto pkt = build_session(false);
        auto e = zeros(46);
        e[0] = 0xAA; e[1] = 0x2E; e[2] = 0x05; e[3] = 0x00; e[44] = 0xDD; e[45] = 0x00;
        expect_bytes("session stop  = AA 2E 05 00 ... DD 00", pkt.data(), pkt.size(), e);
    }
    {
        auto pkt = build_display_config(kOrientUpright, 100);
        auto e = zeros(46);
        e[0] = 0xAA; e[1] = 0x2E; e[2] = 0x04; e[3] = 0x01; e[4] = 0x64; e[44] = 0x41; e[45] = 0x01;
        expect_bytes("config upright/100% = ... 41 01", pkt.data(), pkt.size(), e);
    }
    {
        // brightness 0 is valid (black screen); it passes through, checksum DD 00.
        auto pkt = build_display_config(kOrientUpright, 0);
        auto e = zeros(46);
        e[0] = 0xAA; e[1] = 0x2E; e[2] = 0x04; e[3] = 0x01; e[4] = 0x00; e[44] = 0xDD; e[45] = 0x00;
        expect_bytes("config upright/brightness0 (black) = ... DD 00", pkt.data(), pkt.size(), e);
    }
    {
        // Native telemetry 86 °C / 100 %, full 46-byte frame ending EF 02.
        auto pkt = build_telemetry(86, 100);
        std::vector<std::uint8_t> e = {
            0xAA, 0x2E, 0x01, 0x56, 0x00, 0x00, 0x64, 0x00, 0x00, 0x08, 0x00, 0xD7, 0x03, 0x00,
            0x1E, 0x05, 0x00, 0x07, 0x0C, 0x00, 0x02, 0x04, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0xEF, 0x02};
        expect_bytes("telemetry 86C/100% = ... EF 02", pkt.data(), pkt.size(), e);
    }
}

// ── input clamping / masking (safety invariants §7.1, §7.2) ────────────────────────
void test_display_clamping() {
    {
        auto pkt = build_display_config(kOrientUpright, 150);
        expect_true("brightness 150 clamps to 100", pkt[4] == 0x64);
    }
    {
        auto pkt = build_display_config(kOrientUpright, -5);
        expect_true("brightness -5 clamps to 0", pkt[4] == 0x00);
    }
    {
        auto pkt = build_display_config(5, 50);  // 5 & 0x03 = 1 (upright)
        expect_true("orientation 5 masks to 0x01", pkt[3] == 0x01);
    }
    {
        // Temperature clamps (not masks): -5 °C must not alias to 251 °C.
        auto pkt = build_telemetry(-5, 50);
        expect_true("telemetry -5C clamps to 0", pkt[3] == 0x00);
    }
    {
        auto pkt = build_telemetry(300, 150);
        expect_true("telemetry 300C clamps to 255", pkt[3] == 0xFF);
        expect_true("telemetry usage 150% clamps to 100", pkt[6] == 0x64);
    }
}

// ── display image ──────────────────────────────────────────────────────────────────
void test_display_image() {
    const std::vector<std::uint8_t> jpeg(1000, 0x01);  // 1000 bytes all 0x01
    expect_eq("chunk count for 1000 bytes", static_cast<long>(image_chunk_count(jpeg.size())), 2);

    // START
    {
        auto s = build_image_start(jpeg.data(), jpeg.size());
        expect_eq("START size", static_cast<long>(s.size()), 512);
        expect_true("START tag 'Start'",
                    s[0] == 'S' && s[1] == 't' && s[2] == 'a' && s[3] == 'r' && s[4] == 't');
        expect_true("START frame type 0x01", s[5] == 0x01);
        expect_true("START len le32 = E8 03 00 00",
                    s[6] == 0xE8 && s[7] == 0x03 && s[8] == 0x00 && s[9] == 0x00);
        expect_true("START sum16 le16 = E8 03", s[10] == 0xE8 && s[11] == 0x03);
        expect_true("START chunk count le16 = 02 00", s[12] == 0x02 && s[13] == 0x00);
        bool padded = true;
        for (std::size_t i = 14; i < 512; ++i) padded = padded && (s[i] == 0x00);
        expect_true("START padding [14:512] = 0", padded);
    }

    // DATA #1 — payload of 505 0x01 bytes at offset 7
    {
        auto d = build_image_data(jpeg.data(), jpeg.size(), 1);
        expect_eq("DATA#1 size", static_cast<long>(d.size()), 512);
        expect_true("DATA#1 tag 'trans'",
                    d[0] == 't' && d[1] == 'r' && d[2] == 'a' && d[3] == 'n' && d[4] == 's');
        expect_true("DATA#1 seq le16 = 01 00", d[5] == 0x01 && d[6] == 0x00);
        bool payload = true;
        for (std::size_t i = 7; i < 512; ++i) payload = payload && (d[i] == 0x01);  // 505 bytes
        expect_true("DATA#1 payload at offset 7 = 505 x 0x01", payload);
    }

    // DATA #2 — remaining 495 bytes then 10 pad
    {
        auto d = build_image_data(jpeg.data(), jpeg.size(), 2);
        expect_eq("DATA#2 size", static_cast<long>(d.size()), 512);
        expect_true("DATA#2 seq le16 = 02 00", d[5] == 0x02 && d[6] == 0x00);
        bool payload = true;
        for (std::size_t i = 7; i < 7 + 495; ++i) payload = payload && (d[i] == 0x01);
        bool pad = true;
        for (std::size_t i = 7 + 495; i < 512; ++i) pad = pad && (d[i] == 0x00);  // 10 bytes
        expect_true("DATA#2 payload 495 x 0x01 then 10 x 0x00", payload && pad);
    }

    // FINISH
    {
        auto f = build_image_finish();
        expect_eq("FINISH size", static_cast<long>(f.size()), 512);
        const char tag[] = "DCLdfinish";
        bool ok = true;
        for (std::size_t i = 0; i < 10; ++i) ok = ok && (f[i] == static_cast<std::uint8_t>(tag[i]));
        for (std::size_t i = 10; i < 512; ++i) ok = ok && (f[i] == 0x00);
        expect_true("FINISH tag 'DCLdfinish' then 0-pad", ok);
    }
}

// ── image chunking edges ───────────────────────────────────────────────────────────
void test_image_edges() {
    expect_eq("chunk count for 0 bytes", static_cast<long>(image_chunk_count(0)), 0);
    expect_eq("chunk count for 1 byte", static_cast<long>(image_chunk_count(1)), 1);
    expect_eq("chunk count for 505 bytes", static_cast<long>(image_chunk_count(505)), 1);
    expect_eq("chunk count for 506 bytes", static_cast<long>(image_chunk_count(506)), 2);
    expect_eq("chunk count for 1010 bytes", static_cast<long>(image_chunk_count(1010)), 2);
    expect_eq("chunk count for 1011 bytes", static_cast<long>(image_chunk_count(1011)), 3);

    // A DATA packet whose 1-based seq lies past the end of the stream carries no payload.
    const std::vector<std::uint8_t> jpeg(1000, 0x01);
    auto d = build_image_data(jpeg.data(), jpeg.size(), 3);
    bool zero = true;
    for (std::size_t i = 7; i < 512; ++i) zero = zero && (d[i] == 0x00);
    expect_true("DATA past end: tag+seq set, payload zero",
                d[0] == 't' && d[5] == 0x03 && d[6] == 0x00 && zero);
}

// ── checksum primitives ────────────────────────────────────────────────────────────
void test_checksums() {
    const std::vector<std::uint8_t> ff(300, 0xFF);  // 300 * 255 = 76500
    expect_eq("sum16 wraps mod 2^16", sum16(ff.data(), ff.size()), 76500 & 0xFFFF);
    expect_eq("sum8 wraps mod 2^8", sum8(ff.data(), ff.size()), 76500 & 0xFF);

    // The checksum byte [37] is outside the summed range [1:37], so finalizing twice
    // must be idempotent.
    auto r = default_linker_report();
    finalize_linker_report(r);
    const std::uint8_t first = r[37];
    finalize_linker_report(r);
    expect_true("finalize_linker_report is idempotent", r[37] == first);
}

// ── Linker report ──────────────────────────────────────────────────────────────────
void test_linker_report() {
    auto r = default_linker_report();
    report_set_rainbow(r, 0xFA, 0x0A);
    report_set_fans(r, 55, 32, 32, 32, std::nullopt);
    finalize_linker_report(r);

    std::vector<std::uint8_t> e1 = zeros(64);
    e1[0] = 0x10;
    e1[1] = 0x68; e1[2] = 0x05; e1[3] = 0x02; e1[4] = 0x20; e1[5] = 0x08;
    e1[6] = 0x03; e1[7] = 0x0A; e1[8] = 0xFF; e1[9] = 0x00; e1[10] = 0xFF;
    e1[11] = 0xFA; e1[12] = 0x0A; e1[13] = 0x00; e1[14] = 0x00; e1[15] = 0xFF;
    e1[16] = 0x01;
    e1[17] = 0x37; e1[18] = 0x00; e1[19] = 0x01;  // pump 55%, ramp 0, software
    e1[20] = 0x20; e1[21] = 0x00; e1[22] = 0x01;  // aio 32%
    e1[23] = 0x20; e1[24] = 0x00; e1[25] = 0x01;  // ext1 32%
    e1[26] = 0x20; e1[27] = 0x00; e1[28] = 0x01;  // ext2 32%
    e1[37] = 0x41;  // SUM8 checksum
    e1[38] = 0x16;  // marker
    expect_bytes("linker rainbow + set_fans(55,32,32,32)", r.data(), r.size(), e1);
    expect_eq("linker checksum [37]", r[37], 0x41);

    // motherboard_sync(true) from the state above
    report_motherboard_sync(r, true);
    finalize_linker_report(r);
    std::vector<std::uint8_t> e2 = e1;
    e2[6] = 0x01;   // effect -> motherboard
    e2[16] = 0x00;  // fan sync -> motherboard
    e2[19] = 0x00;  // pump source -> motherboard
    e2[22] = 0x00;  // aio source -> motherboard
    e2[25] = 0x00;  // ext1 source -> motherboard
    e2[28] = 0x01;  // ext2 source stays software
    e2[37] = 0x3B;  // recomputed checksum
    expect_bytes("linker motherboard_sync(true)", r.data(), r.size(), e2);
    expect_eq("linker checksum [37] after mb sync", r[37], 0x3B);
}

// ── default report + lighting mutators (field layout per spec §5.1/§5.2) ───────────
void test_linker_defaults_and_lighting() {
    // The fully-populated default report, finalized. Built here by explicit byte
    // assignment so it is independent of the builder's internals.
    {
        auto r = default_linker_report();
        expect_true("default report not yet finalized", r[37] == 0x00);
        finalize_linker_report(r);

        std::vector<std::uint8_t> e = zeros(64);
        e[0] = 0x10;                                              // report id
        e[1] = 0x68; e[2] = 0x05; e[3] = 0x02; e[4] = 0x20; e[5] = 0x08;  // header
        e[6] = 0x03;                                              // effect: rainbow
        e[7] = 0x0A; e[8] = 0xFF; e[9] = 0x00; e[10] = 0xFF;      // breathing speed+colour
        e[11] = 0xFA; e[12] = 0x0A;                               // rainbow speed+saturation
        e[13] = 0x00; e[14] = 0x00; e[15] = 0xFF;                 // always-on colour
        e[16] = 0x01;                                             // fan sync: software
        e[17] = 0x3C; e[18] = 0x00; e[19] = 0x01;                 // pump 60%
        e[20] = 0x28; e[21] = 0x00; e[22] = 0x01;                 // aio 40%
        e[23] = 0x28; e[24] = 0x00; e[25] = 0x01;                 // ext1 40%
        e[26] = 0x28; e[27] = 0x00; e[28] = 0x01;                 // ext2 40%
        e[37] = 0x5E;                                             // SUM8 over [1:37]
        e[38] = 0x16;                                             // marker
        expect_bytes("default_linker_report (finalized)", r.data(), r.size(), e);
    }

    // Effect mutators only touch their own fields (§5.2: the report holds all effects'
    // settings simultaneously; [6] selects the active one).
    {
        auto r = default_linker_report();
        report_set_breathing(r, {0x11, 0x22, 0x33}, 0x40);
        expect_true("breathing: effect+speed+colour",
                    r[6] == 0x02 && r[7] == 0x40 && r[8] == 0x11 && r[9] == 0x22 &&
                        r[10] == 0x33);
        expect_true("breathing leaves rainbow/always-on fields",
                    r[11] == 0xFA && r[12] == 0x0A && r[13] == 0x00 && r[15] == 0xFF);

        report_set_always_on(r, {0x44, 0x55, 0x66});
        expect_true("always-on: effect+colour",
                    r[6] == 0x04 && r[13] == 0x44 && r[14] == 0x55 && r[15] == 0x66);
        expect_true("always-on leaves breathing fields",
                    r[7] == 0x40 && r[8] == 0x11 && r[9] == 0x22 && r[10] == 0x33);

        report_lighting_to_motherboard(r);
        expect_true("lighting->motherboard: effect only, fan sync untouched",
                    r[6] == 0x01 && r[16] == 0x01 && r[17] == 0x3C);
    }

    // Fan duty clamping and per-channel independence.
    {
        auto r = default_linker_report();
        report_set_fans(r, 150, -10, std::nullopt, std::nullopt, 300);
        expect_true("fan duty clamps to [0,100]", r[17] == 100 && r[20] == 0);
        expect_true("ramp clamps to [0,255]", r[18] == 0xFF && r[21] == 0xFF);
        expect_true("nullopt channels untouched (duty+ramp)",
                    r[23] == 0x28 && r[24] == 0x00 && r[26] == 0x28 && r[27] == 0x00);
    }
}

// ── neutral telemetry poll ─────────────────────────────────────────────────────────
void test_telemetry_poll() {
    // From the §6 software-control state (rainbow + fans 55/32/32/32): the poll flips the
    // effect to motherboard and EVERY channel source to motherboard — including EXT2,
    // unlike motherboard_sync — while duties are preserved (and ignored by the device).
    auto r = default_linker_report();
    report_set_rainbow(r, 0xFA, 0x0A);
    report_set_fans(r, 55, 32, 32, 32, std::nullopt);
    report_telemetry_poll(r);
    finalize_linker_report(r);

    std::vector<std::uint8_t> e = zeros(64);
    e[0] = 0x10;
    e[1] = 0x68; e[2] = 0x05; e[3] = 0x02; e[4] = 0x20; e[5] = 0x08;
    e[6] = 0x01;                                  // effect -> motherboard
    e[7] = 0x0A; e[8] = 0xFF; e[9] = 0x00; e[10] = 0xFF;
    e[11] = 0xFA; e[12] = 0x0A; e[13] = 0x00; e[14] = 0x00; e[15] = 0xFF;
    e[16] = 0x00;                                 // fan sync -> motherboard
    e[17] = 0x37; e[18] = 0x00; e[19] = 0x00;     // pump duty kept, source -> motherboard
    e[20] = 0x20; e[21] = 0x00; e[22] = 0x00;
    e[23] = 0x20; e[24] = 0x00; e[25] = 0x00;
    e[26] = 0x20; e[27] = 0x00; e[28] = 0x00;     // EXT2 too (no motherboard_sync quirk)
    e[37] = 0x3A;                                 // recomputed SUM8
    e[38] = 0x16;
    expect_bytes("telemetry poll asserts no software control", r.data(), r.size(), e);
}

// ── tachometry decode ──────────────────────────────────────────────────────────────
void test_decode_status() {
    std::array<std::uint8_t, 64> s{};
    s[0] = kLinkerReportId;  // real status reports echo report id 0x10
    // [29:37] = 0A 6D 03 C9 02 E5 03 1E
    s[29] = 0x0A; s[30] = 0x6D; s[31] = 0x03; s[32] = 0xC9;
    s[33] = 0x02; s[34] = 0xE5; s[35] = 0x03; s[36] = 0x1E;
    s[37] = sum8(s.data() + 1, 36);  // valid stored checksum
    auto st = decode_status(s.data(), s.size());
    expect_eq("decode pump", st.pump, 2669);
    expect_eq("decode aio", st.aio, 969);
    expect_eq("decode ext1", st.ext1, 741);
    expect_eq("decode ext2", st.ext2, 798);
    expect_true("decode checksumOk", st.checksumOk);

    // Robustness: malformed inputs decode to all-zero readings, never garbage.
    {
        auto st2 = decode_status(nullptr, 64);
        expect_true("decode nullptr -> zeros", st2.pump == 0 && !st2.checksumOk);
    }
    {
        auto st2 = decode_status(s.data(), 36);  // too short for the tach block
        expect_true("decode short buffer -> zeros", st2.pump == 0 && !st2.checksumOk);
    }
    {
        auto bad = s;
        bad[0] = 0x11;  // wrong report id
        auto st2 = decode_status(bad.data(), bad.size());
        expect_true("decode wrong report id -> zeros", st2.pump == 0 && !st2.checksumOk);
    }
    {
        auto bad = s;
        bad[37] ^= 0xFF;  // corrupt the stored checksum
        auto st2 = decode_status(bad.data(), bad.size());
        expect_true("decode bad checksum -> readings kept, flag false",
                    st2.pump == 2669 && !st2.checksumOk);
    }
    {
        // 37 bytes: readings present, checksum byte missing.
        auto st2 = decode_status(s.data(), 37);
        expect_true("decode 37 bytes -> readings, checksumOk false",
                    st2.pump == 2669 && !st2.checksumOk);
    }
}

}  // namespace

int main() {
    std::printf("== libspartacus golden-vector tests ==\n\n");
    test_display_control();
    test_display_clamping();
    test_display_image();
    test_image_edges();
    test_checksums();
    test_linker_report();
    test_linker_defaults_and_lighting();
    test_telemetry_poll();
    test_decode_status();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("FAILED\n");
    return 1;
}

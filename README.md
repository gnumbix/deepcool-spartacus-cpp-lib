# libspartacus (C++17)

This C++17 client library is for managing the CPU cooler DeepCool SPARTACUS 360 / 420.

Since the manufacturer DeepCool does not provide Linux support, I had to reverse engineer
it myself and analyze the communication protocol. As a result, a protocol specification
was obtained, and this library was written based on it.

The pump cap exposes two USB devices, and this library drives both:

- **Display** (`3633:0027`) — a 480×480 circular LCD (vendor bulk transport).
- **Linker** (`3633:002D`) — the pump / AIO fan / two EXT fan headers and ARGB lighting,
  with tachometry (HID interrupt transport).

The protocol was reverse-engineered; there is no vendor SDK. The wire format is documented
in [`docs/protocol-specification.md`](docs/protocol-specification.md)

Namespace: `spartacus`. Transport: **libusb-1.0**. License: **MIT** ([`LICENSE`](LICENSE)).

---

## Building

The **Makefile** is the primary build (no CMake required):

```sh
make            # build the static library (build/libspartacus.a) and the examples
make test       # build + run the golden-vector tests (no hardware needed; alias: check)
make install    # install headers, libspartacus.a and spartacus.pc under $(PREFIX)
make clean      # remove the build directory        (default PREFIX: /usr/local)
```

After `make install`, non-CMake consumers can use pkg-config (the library is static, so
ask for the private libusb dependency too):

```sh
c++ myapp.cpp $(pkg-config --cflags --libs --static spartacus)
```

Options:

- `make WITH_IMAGE=0 …` — build the **core-only** library (no bundled JPEG encoder, no
  `showRgb` / `showJpegFile` / `clear`). `showJpeg` and everything else still work.
- `make PREFIX=/opt/spartacus install`, `make CXX=clang++ …`, `make OPT='-O3' …`.

Requires a C++17 compiler and `pkg-config` + `libusb-1.0` development files.

### CMake alternative

For users who have CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build                 # golden-vector tests
cmake --install build --prefix /usr/local
```

Then, from another project:

```cmake
find_package(spartacus REQUIRED)
target_link_libraries(myapp PRIVATE spartacus::spartacus)
```

CMake options mirror the Makefile: `-DSPARTACUS_WITH_IMAGE=OFF`, `-DSPARTACUS_BUILD_EXAMPLES=OFF`,
`-DSPARTACUS_BUILD_TESTS=OFF`.

---

## Usage

### Display

```cpp
#include <spartacus/Spartacus.hpp>

auto display = spartacus::Display::open();   // opens 3633:0027, claims interface 0
display.sessionStart();                      // enable the image stream
display.setDisplay(spartacus::protocol::kOrientUpright, 80);  // orientation + brightness together

std::vector<uint8_t> rgb(480 * 480 * 3, 0x20);   // your 480×480 RGB frame
display.showRgb(rgb.data(), 480, 480, 3, 90);    // resize→encode baseline JPEG→stream
// display.showJpeg(bytes, len);             // or upload a ready-made 480×480 baseline JPEG
// display.showJpegFile("frame.jpg");        // or a .jpg on disk (no re-encode)
display.clear(0, 0, 0);                      // blank with a solid black frame

display.sessionStop();                       // switch to the native readout…
display.pushTelemetry(/*tempC=*/62, /*usagePct=*/45);   // …and push temp + usage ~1 Hz
// object closes the device in its destructor
```

### Linker

```cpp
#include <spartacus/Spartacus.hpp>

auto linker = spartacus::Linker::open();     // opens 3633:002D, claims interface 0
linker.setFans(/*pump=*/60, /*aio=*/45, /*ext1=*/45, /*ext2=*/45, /*ramp=*/0);
linker.setRainbow(/*speed=*/0xFA, /*saturation=*/0x0A);
linker.setBreathing({0xFF, 0x00, 0x00}, /*speed=*/0x0A);
linker.setAlwaysOn({0x00, 0x00, 0xFF});

spartacus::LinkerStatus s = linker.readStatus();   // RPM (big-endian on the wire)
printf("pump=%d aio=%d ext1=%d ext2=%d ok=%d\n", s.pump, s.aio, s.ext1, s.ext2, s.checksumOk);

linker.motherboardSync(true);                // hand fans + lighting to the motherboard
```

**Reading RPMs without taking control.** The device only replies to a host poll, and the
poll is a full control report. `readStatus()` solicits with the *retained* report — fine
when you are the one driving the fans, but on a fresh handle it applies the library
defaults. Monitoring tools should use `readStatusPassive()`, which solicits with a neutral
report (every channel handed to the motherboard) and therefore can never change pump/fan
speeds or lighting:

```cpp
auto linker = spartacus::Linker::open();
auto s = linker.readStatusPassive();         // safe by construction for monitors
```

Errors are reported by exceptions: `spartacus::Error` (base), `spartacus::DeviceNotFoundError`,
and `spartacus::UsbError` (which carries the libusb `code()`).

---

## API summary

**`spartacus::Display`** (move-only; `Display::open()`):
`sessionStart` · `sessionStop` · `setDisplay(orientation?, brightness?)` · `setOrientation` ·
`setBrightness` · `pushTelemetry(tempC, usagePct)` · `showJpeg(ptr, len)` ·
`showRgb(pixels, w, h, comp, quality)` · `showJpegFile(path)` · `clear(r, g, b)` ·
`orientation()` · `brightness()`.

**`spartacus::Linker`** (move-only; `Linker::open()`):
`setFans(pump?, aio?, ext1?, ext2?, ramp?)` ·
`readStatus()` / `readStatusPassive()` `-> LinkerStatus{pump,aio,ext1,ext2,checksumOk}` ·
`setRainbow(speed, saturation)` · `setBreathing(rgb, speed)` · `setAlwaysOn(rgb)` ·
`lightingToMotherboard()` · `motherboardSync(enable)` · `send()` · `report()`.

**`namespace spartacus::protocol`** — pure, hardware-free builders (the correctness core,
fully unit-tested): `sum8` · `sum16` · `build_session` · `build_display_config` ·
`build_telemetry` · `image_chunk_count` · `build_image_start` / `build_image_data` /
`build_image_finish` · `default_linker_report` + `report_set_*` mutators +
`report_telemetry_poll` + `finalize_linker_report` · `decode_status`, plus all wire
constants (`kVendorId`, endpoints, `kOrient*`, `kEffect*`, `kSource*`, `kLinkerReportId`,
channel offsets, `kBrightnessMin`, `kPumpDutyFloor`, …).

**`<spartacus/Version.hpp>`** — `SPARTACUS_VERSION_MAJOR/MINOR/PATCH` and
`SPARTACUS_VERSION_STRING`. The project follows [Semantic Versioning](https://semver.org/).

**Thread safety.** A `Display`/`Linker` instance is not internally synchronized — use it
from one thread at a time (or guard it externally). Distinct instances are independent;
each owns a private libusb context.

---

## Examples

Built into `build/examples/` by `make` (or `cmake --build`):

| Example | What it does |
|---------|--------------|
| `brightness [level] [--sweep]` | set backlight level (0–100; 0 = black); `--sweep` demos 0→100→0 (writes NVM, so opt-in) |
| `orientation [0..3] [--cycle]` | set orientation; `--cycle` demos all four first (writes NVM, so opt-in) |
| `image` | generate a 480×480 test pattern and push it via `showRgb` |
| `native_telemetry [secs]` | push **synthetic** temp/usage to the native readout |
| `clear [r g b]` | blank the panel with a solid colour |
| `linker_read_speeds` | poll and print fan tachometry (passively — never touches control) |
| `linker_set_speeds <pump> <aio> <ext1> <ext2> [--force]` | set duties (refuses pump < 40 % without `--force`; `-` leaves a channel unchanged) |
| `linker_argb` | cycle rainbow → breathing → always-on → motherboard |
| `linker_motherboard_sync [on\|off]` | hand fans/lighting to the motherboard or reclaim them |

All example values are synthetic / provided on the command line — none read real OS sensors.

```sh
./build/examples/linker_read_speeds
./build/examples/brightness 70
```

---

## Platform setup

**The official DeepCool software must not be running** — it holds the devices and will block
access on every platform.

### Linux

Raw USB access needs permissions. Install the ready-made udev rule shipped in
[`packaging/udev/99-spartacus.rules`](packaging/udev/99-spartacus.rules), then reload and
re-trigger (or replug the device):

```sh
sudo cp packaging/udev/99-spartacus.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

The library detaches the Linker's kernel HID driver itself and deliberately leaves it
detached on close: re-attaching races usbhid re-binding on the next open and made
reconnects unreliable (re-plug the device if you want `usbhid` back).

### Windows

Bind a WinUSB/libusb driver to **both** interfaces (e.g. with [Zadig](https://zadig.akeo.ie/))
so libusb can claim them.

---

## Troubleshooting

- **`DeviceNotFoundError` / claim failures** — the official DeepCool software (or another
  copy of your program) is holding the device, the udev rule is missing (Linux), or no
  WinUSB driver is bound (Windows).
- **`UsbError` with a timeout on `readStatus()`** — a wedged Linker keeps ACKing the poll
  writes but stops returning status reports. Power-cycle / re-plug the cooler. (Don't
  treat 0 RPM on EXT headers as an error: 0 simply means no fan is connected, and
  `checksumOk` tells you the report itself was intact.)
- **EIO on the first Linker transfer** — should not happen anymore: the library issues
  the full hardware-verified bring-up (explicit kernel-driver detach, unconditional
  `SET_CONFIGURATION`, endpoint-halt clears). If you still see it, re-plug and check that
  nothing else re-claimed the interface in between.
- **Panel reverts to the DeepCool logo** — the ~15 s idle watchdog. Refresh static
  screens within that window (see the protocol spec §4.5).

## Safety notes

The library enforces the protocol's safety invariants:

- **Brightness `[0, 100]`.** Brightness is clamped to `[0, 100]`; `0` is valid and shows a black
  screen.
  Orientation and brightness are **persisted in the panel's non-volatile memory**, so set them only
  when they change; a black frame (`clear(0,0,0)`) is preferred for a transient blank. With no data
  for ~15 s the panel reverts to its logo, so refresh static screens within that window.
- **Pump duty floor ~40 %.** The pump cools the CPU. The library sends what you ask, but the
  `linker_set_speeds` example refuses a pump duty below 40 % unless you pass `--force`.
- **Baseline JPEG, 480×480 only.** The panel accepts only baseline (sequential) JPEG at
  480×480. `showRgb` resizes and encodes baseline for you; `showJpegFile` sends bytes
  unchanged, so hand it a baseline 480×480 `.jpg`.
- **Full state every time.** Orientation + brightness are always sent together, and the Linker
  transmits its complete 64-byte report on every change (checksums recomputed automatically).

## Bundled encoder

`third_party/spartacus_jpeg.h` is a small, dependency-free **baseline** JPEG encoder
(public-domain-style; MIT at your option) bundled so the image helpers need no libjpeg. Exactly
one translation unit (`src/Image.cpp`) instantiates it. Do not modify the header.

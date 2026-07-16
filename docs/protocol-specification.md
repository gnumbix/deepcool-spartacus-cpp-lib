# DeepCool SPARTACUS — Protocol Specification

> There is no public SDK or vendor protocol documentation for this device. Everything here was
> obtained by reverse engineering.

---

## 1. Devices

The pump cap enumerates as **two independent USB devices**, both under DeepCool vendor id
`0x3633`. A complete host drives both.

| Device                                     | Product string       | VID:PID       | Transport             | Interface class |
|--------------------------------------------|----------------------|---------------|-----------------------|-----------------|
| **Display Controller** (LCD)               | `SPARTACUS`          | `3633:0027`   | Vendor **bulk**       | `0xFF`          |
| **Fan & Lighting Controller** ("Linker")   | `SPARTACUS Linker`   | `3633:002D`   | **HID / interrupt**   | `0x03`          |

- The **display** drives a **480 × 480 circular LCD** on the pump cap. Its corners fall outside the
  visible disc.
- The **Linker** controls the pump, AIO fan, and two external fan headers, plus ARGB lighting, and
  reports tachometry.

> **Bind by full VID:PID** (or product string), never by vendor id or endpoint alone — both devices
> share VID `0x3633`, and bulk OUT endpoint `0x02` is common to unrelated device classes.

### 1.1 Endpoints and I/O model

|                               | Display Controller (`3633:0027`)           | Linker (`3633:002D`)              |
|-------------------------------|--------------------------------------------|-----------------------------------|
| Interface to claim            | 0                                          | 0                                 |
| Host → device                 | bulk `0x02` (image), bulk `0x04` (control) | interrupt `0x01` (control report) |
| Device → host                 | `0x81`, `0x83` (present, unused)           | interrupt `0x81` (status report)  |
| Flow control                  | **None** — fire-and-forget, no ACKs        | **Poll / response**, ~1 Hz        |
| USB timeout used by reference | 2000 ms                                    | 2000 ms                           |

Bring-up for both: standard enumeration → `SET_CONFIGURATION(1)` → detach kernel driver if attached
→ `claim_interface(0)`.

---

## 2. Conventions

- Zero-based byte offsets. `[a:b]` is half-open (bytes `a … b−1`); `[a]` is one byte.
- Multi-byte integers are **little-endian** unless explicitly marked big-endian.
- **SUM16** (display control + image): `sum16(data) = (Σ data[i]) mod 65536`, transmitted
  little-endian. A plain additive sum, **not** a CRC.
- **SUM8** (Linker report): `sum8(data) = (Σ data[i]) mod 256`, a single byte.

```
sum16(data) = sum(data) & 0xFFFF     # little-endian on the wire
sum8(data)  = sum(data) & 0xFF
```

---

## 3. Display Controller — control channel (bulk EP `0x04`)

Fixed **46-byte** packets:

| Offset | Size | Field           | Notes                                             |
|--------|------|-----------------|---------------------------------------------------|
| 0      | 2    | Signature       | constant `AA 2E`                                  |
| 2      | 1    | Command         | `0x05` session · `0x04` config · `0x01` telemetry |
| 3      | 1    | Parameter 1     | command-dependent                                 |
| 4      | 40   | Parameter block | command-dependent; unused bytes `0x00`            |
| 44     | 2    | Checksum        | `sum16` of bytes `[0:44]`, little-endian          |

```
pkt        = [0xAA, 0x2E, cmd, p1] + [0x00]*40      # 44 bytes
pkt[4:44]  = params                                 # command-specific
pkt       += le16(sum16(pkt[0:44]))                 # -> 46 bytes
bulk_out(0x04, pkt)
```

### 3.1 Session Control — `cmd = 0x05`

`[3]` is the image-stream enable flag: `0x01` start/resume, `0x00` stop.

- Send **Session Start** once after connecting to enable the image stream.
- Send **Session Stop** before switching to native telemetry mode.

```
Start:  AA 2E 05 01 00…00 DE 00
Stop:   AA 2E 05 00 00…00 DD 00
```

### 3.2 Display Configuration — `cmd = 0x04`

Sets **orientation** (`[3]`) and **brightness** (`[4]`) together. The device has no "change one"
form — when you send `0x04` you must supply **both** fields; to change one, resend with the current
value of the other.

> 🧠 **These two settings are persistent.** The panel stores orientation and brightness in
> non-volatile memory and **remembers them across sessions and power cycles**. You do **not** need
> to resend them on every connect or before every image — send `0x04` **only when a value actually
> changes**. Re-writing the same value repeatedly (e.g. once per frame) wears the memory for no
> benefit; hosts should track the last-applied values and skip no-op writes.

Orientation (default upright is `0x01`, each increment rotates +90° CCW):

| Value    | Rotation | Appearance         |
|----------|----------|--------------------|
| `0x01`   | 0°       | Upright (default)  |
| `0x02`   | 90°      | Rotated left       |
| `0x03`   | 180°     | Inverted           |
| `0x00`   | 270°     | Rotated right      |

`rotation = ((value − 1) mod 4) × 90°` CCW.

Brightness (`[4]`): `u8` percentage, **valid range 0–100 (`0x00`–`0x64`)**, a direct linear
percentage.

> **`0x00` is a valid brightness: it renders a fully black screen** (the panel goes dark).
> Hosts may use brightness `0` as a
> lightweight way to blank the panel, though note it writes persistent memory (above); to blank
> without touching that memory, display a black image instead (see §4.4).

```
Upright, 100%:  AA 2E 04 01 64 00…00 41 01
Upright, black:  AA 2E 04 01 00 00…00 DD 00
```

### 3.3 Telemetry Push (native "always-on") — `cmd = 0x01`

A low-bandwidth mode where the panel renders a minimal CPU **temperature + usage** readout by
itself, without a streamed image. Uses a fixed 44-byte template; only two bytes are live:

| Byte   | Field                       |
|--------|-----------------------------|
| `[3]`  | CPU temperature, °C (`u8`)  |
| `[6]`  | CPU usage, % (`u8`, 0–100)  |

Other bytes select the on-device layout and must be preserved. **CPU power/frequency are not carried
by this command** — the native readout is temperature + usage only.

Template (46 bytes shown with temp `[3]=0x56`=86 °C and usage `[6]=0x64`=100 %, checksum `EF 02`):

```
AA 2E 01 56  00 00 64 00  00 08 00 D7  03 00 1E 05
00 07 0C 00  02 04 00 3E  00 00 00 00  00 00 00 00
00 00 00 00  00 00 00 00  00 00 00 00  EF 02
```

Sequence:

```
1.  AA 2E 05 00 …        # Session Stop (stop the image stream)
2.  AA 2E 01 <temp> …    # push telemetry, ~1 Hz (only [3],[6] change)  — repeat
3.  AA 2E 05 01 …        # Session Start to return to streamed display
```

---

## 4. Display Controller — image transfer (bulk EP `0x02`)

Every frame is a **480 × 480 baseline (sequential) JPEG**. Progressive JPEG is **not** supported.
One frame is sent as **START → DATA(1) … DATA(N) → FINISH**. All packets are **exactly 512 bytes**,
zero-padded. Unidirectional, no ACKs. The device decodes and displays on FINISH.

```
N = ceil(len(jpeg) / 505)
```

### 4.1 START packet

| Offset | Size | Type    | Field                            |
|--------|------|---------|----------------------------------|
| 0      | 5    | ascii   | Tag `"Start"` = `53 74 61 72 74` |
| 5      | 1    | u8      | Frame type `0x01` (all content)  |
| 6      | 4    | u32 le  | Image size = total JPEG length   |
| 10     | 2    | u16 le  | Image checksum = `sum16(jpeg)`   |
| 12     | 2    | u16 le  | Chunk count `N`                  |
| 14     | 498  | —       | Padding `0x00`                   |

### 4.2 DATA packet (repeated `N` times)

| Offset  | Size | Type   | Field                                                      |
|---------|------|--------|------------------------------------------------------------|
| 0       | 5    | ascii  | Tag `"trans"` = `74 72 61 6E 73`                           |
| 5       | 2    | u16 le | Sequence number, **1-based** (`1 … N`)                     |
| 7       | 505  | —      | Payload = consecutive JPEG bytes (last packet zero-padded) |

> Note byte `[6]` is skipped — the payload begins at **offset 7**.

### 4.3 FINISH packet

| Offset | Size | Type  | Field                                                |
|--------|------|-------|------------------------------------------------------|
| 0      | 10   | ascii | Tag `"DCLdfinish"` = `44 43 4C 64 66 69 6E 69 73 68` |
| 10     | 502  | —     | Padding `0x00`                                       |

### 4.4 Higher-level behaviour (host-side policy, not device commands)

- **Static image:** one START/DATA/FINISH sequence.
- **Video / GIF / animation:** there is **no** separate transport. Decode/scale/crop each source
  frame to 480 × 480, optionally composite an overlay, encode baseline JPEG, and stream frames
  back-to-back. ~20 fps is comfortable and the panel imposes no *minimum* interval (send as fast as
  the host encode/transfer loop allows); there is, however, a *maximum* silent interval — see §4.5.
- **Slideshows / transitions / layouts:** entirely host-side; the device only ever receives composed
  JPEG frames.
- **Clearing:** there is no clear command — upload a solid (e.g. black) 480 × 480 JPEG. (Brightness
  `0x00` also blanks the panel, but writes persistent memory — see §3.2.)

### 4.5 Idle logo watchdog & frame retention

- **Logo watchdog (~15 s).** If the panel receives **no data for ~15 seconds**, its firmware
  abandons the host's content and displays the built-in DeepCool logo splash. To hold a screen you
  must send *something* within this window. For motion this never triggers (frames flow
  continuously); for a **static or slowly-changing** screen the host must refresh periodically.
  Pick a keepalive interval comfortably below 15 s (e.g. 10–12 s).
- **Frame retention.** The panel keeps the **last decoded frame** in memory even after the logo
  takes over. A full re-upload always restores it, but a lightweight **control packet** (e.g. a
  Session Start, §3.1) appears to re-show the retained frame *without* re-sending the JPEG — turning
  the periodic keepalive into a 46-byte write instead of a whole-frame transfer (a ~1000× traffic
  cut for static content). This is host-observable only (the panel never acknowledges); Until confirmed on
  your firmware, the safe fallback is to re-upload the frame within the watchdog window.

---

## 5. Linker — control & status report (interrupt EP `0x01` / `0x81`)

One **64-byte** report, **report id `0x10`**, carries the complete control state in **both**
directions. Populate **every** field on every host transmission — the report is stateless per
transfer; omitting a field zeroes it. To change anything, resend the whole report.

| Offset | Size | Type       | Field              | Notes                                                             |
|--------|------|------------|--------------------|-------------------------------------------------------------------|
| 0      | 1    | u8         | Report id          | `0x10`                                                            |
| 1      | 5    | —          | Header             | fixed `68 05 02 20 08`                                            |
| 6      | 1    | u8         | Effect mode        | `01` motherboard · `02` breathing · `03` rainbow · `04` always-on |
| 7      | 1    | u8         | Breathing speed    |                                                                   |
| 8      | 3    | rgb        | Breathing colour   | R, G, B                                                           |
| 11     | 1    | u8         | Rainbow speed      |                                                                   |
| 12     | 1    | u8         | Rainbow saturation |                                                                   |
| 13     | 3    | rgb        | Always-On colour   | R, G, B                                                           |
| 16     | 1    | u8         | Fan sync flag      | `01` software · `00` motherboard                                  |
| 17     | 12   | —          | Channel controls   | 4 channels × 3 bytes (see §5.1)                                   |
| 29     | 2    | u16 **be** | Pump RPM           | ch0 tachometer (device→host)                                      |
| 31     | 2    | u16 **be** | AIO fan RPM        | ch1 tachometer                                                    |
| 33     | 2    | u16 **be** | EXT fan 1 RPM      | ch2 tachometer                                                    |
| 35     | 2    | u16 **be** | EXT fan 2 RPM      | ch3 tachometer                                                    |
| 37     | 1    | u8         | Checksum           | `sum8` of bytes `[1:37]` (Report id excluded)                     |
| 38     | 1    | u8         | Marker             | fixed `0x16`                                                      |
| 39     | 25   | —          | Reserved           | `0x00`                                                            |

### 5.1 Per-channel control (bytes `[17:29]`, 3 bytes each)

| Relative | Field     | Meaning                              |
|----------|-----------|--------------------------------------|
| +0       | Speed     | target PWM duty, 0–100 %             |
| +1       | Ramp time | smoothing; `0x00` = immediate        |
| +2       | Source    | `0x01` software · `0x00` motherboard |

| Channel | Base offset | Output    | Tachometer |
|---------|-------------|-----------|------------|
| 0       | 17          | Pump      | `[29:31]`  |
| 1       | 20          | AIO fan   | `[31:33]`  |
| 2       | 23          | EXT fan 1 | `[33:35]`  |
| 3       | 26          | EXT fan 2 | `[35:37]`  |

> The pump cools the CPU. Do not stop it — treat a pump duty below ~40 % as unsafe.

### 5.2 ARGB lighting (bytes `[6:16]`)

The report holds all effects' settings simultaneously; `[6]` selects the active one. Effect mode
`0x01` also hands lighting control to the motherboard.

### 5.3 Reading tachometry

Poll-driven: send the current control report to solicit a reply, then read the 64-byte status report
from `0x81` and decode **big-endian**:

```
pump = (s[29]<<8)|s[30] ;  aio  = (s[31]<<8)|s[32]
ext1 = (s[33]<<8)|s[34] ;  ext2 = (s[35]<<8)|s[36]     # 0 = no fan connected
```

### 5.4 Motherboard synchronization

Coordinated change of effect mode, fan-sync flag, and per-channel source:

| State              | `[6]` effect                  | `[16]` fan sync | ch0 src  | ch1 src | ch2 src  | ch3 src |
|--------------------|-------------------------------|-----------------|----------|---------|----------|---------|
| Software control   | active effect `0x02`–`0x04`   | `0x01`          | `0x01`   | `0x01`  | `0x01`   | `0x01`  |
| Motherboard sync   | `0x01`                        | `0x00`          | `0x00`   | `0x00`  | `0x00`   | `0x01`  |

> Note the asymmetry: under motherboard sync, channels 0–2 sources are `0x00` but **channel 3
> (EXT fan 2) keeps `0x01`**. Reproduce this exactly (it is what the reference and captures show).

---

## 6. Golden test vectors

Every library ships unit tests asserting its packet builders emit **exactly** these bytes. This is
the primary correctness gate and requires no hardware. `…` denotes zero-padding to the packet length.

### Display control (46 bytes each)

| Operation                                    | Bytes                                                                                     |
|----------------------------------------------|-------------------------------------------------------------------------------------------|
| Session Start                                | `AA 2E 05 01` `00×40` `DE 00`                                                             |
| Session Stop                                 | `AA 2E 05 00` `00×40` `DD 00`                                                             |
| Config: upright, 100 %                       | `AA 2E 04 01 64` `00×39` `41 01`                                                          |
| Config: upright, brightness 0 (black screen) | `AA 2E 04 01 00` `00×39` `DD 00`                                                          |
| Telemetry: 86 °C, 100 %                      | `AA 2E 01 56 00 00 64 00 00 08 00 D7 03 00 1E 05 00 07 0C 00 02 04 00 3E` `00×20` `EF 02` |

### Display image (deterministic synthetic frame)

For a JPEG stream of **1000 bytes all `0x01`**: `len = 1000 = 0x03E8`, `N = ceil(1000/505) = 2`,
`sum16 = 1000 = 0x03E8`.

- START (512 B): `53 74 61 72 74` `01` `E8 03 00 00` `E8 03` `02 00` then `00×498`.
- DATA #1 (512 B): `74 72 61 6E 73` `01 00` then `01×505` — payload begins at offset 7; 5+2+505 = 512
  exactly, so no padding.
- DATA #2 (512 B): `74 72 61 6E 73` `02 00` then `01×495` `00×10` (remaining 495 bytes at offset 7,
  then padded to 512).
- FINISH (512 B): `44 43 4C 64 66 69 6E 69 73 68` then `00×502`.

### Linker report (64 bytes)

Software control — Rainbow, pump 55 %, fans 32 % (`set_rainbow()` then `set_fans(55,32,32,32)`):

```
10 68 05 02 20 08 03 0A FF 00 FF FA 0A 00 00 FF
01 37 00 01 20 00 01 20 00 01 20 00 01 00 00 00
00 00 00 00 00 41 16 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```
Checksum byte `[37]` = `0x41`.

Motherboard sync (from the state above, `motherboard_sync(True)`):

```
10 68 05 02 20 08 01 0A FF 00 FF FA 0A 00 00 FF
00 37 00 00 20 00 00 20 00 00 20 00 01 00 00 00
00 00 00 00 00 3B 16 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```
Checksum byte `[37]` = `0x3B`.

Tachometry decode — status bytes `[29:37] = 0A 6D 03 C9 02 E5 03 1E` →
pump `0x0A6D` = **2669**, aio `0x03C9` = **969**, ext1 `0x02E5` = **741**, ext2 `0x031E` = **798** rpm.

---

## 7. Safety invariants (all libraries enforce these)

1. **Brightness clamp** to `[0, 100]`; `0x00` is valid and renders a black screen (it is **not**
   unsafe — the old "never emit `0x00`" rule was based on a mistaken observation and is retracted).
2. **Orientation** masked to `[0x00, 0x03]`; **upright is `0x01`, not `0x00`**.
3. **When sending `0x04`, send orientation + brightness together** (there is no partial form). But
   these settings are **persistent** — resend `0x04` only when a value changes, not on every
   connect/frame, to avoid needless writes to the panel's non-volatile memory (§3.2).
4. **Linker: send the full 64-byte report every time**, recomputing `sum8` over `[1:37]`.
5. **Recompute checksums after any byte change** (SUM16 for display, SUM8 for Linker).
6. **Baseline JPEG only, 480 × 480.** Image helpers resize + encode baseline; `clear` uploads a solid
   frame (brightness `0` is an alternative but writes persistent memory).
7. **Refresh within the ~15 s logo watchdog** (§4.5): a static screen must be re-sent (or kept alive
   with a lightweight control packet) before the panel reverts to its logo.
8. **Tachometers are big-endian** on the wire, despite the vendor code reading them little-endian
   (its native layer re-orders first).
9. **Bind by VID:PID.**
10. **Pump duty guard:** the example layer refuses a pump duty below ~40 % unless explicitly forced.

---

## 8. Platform access notes

- **Linux:** raw USB access needs permissions. Install a udev rule granting user access to both
  VID:PIDs (see this repository's `packaging/udev/` and README) or run as root. The Linker's kernel HID
  driver must be detached (libusb backends do this automatically).
- **Windows:** bind a WinUSB/libusb driver to both interfaces (e.g. via Zadig) so libusb can claim
  them.
- **The official DeepCool software must not be running** — it holds the devices and will block access.

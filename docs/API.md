# libg90 public API

Doxygen-style comments in headers are the source of truth. Include everything via:

```cpp
#include <g90/g90.hpp>
```

CMake target: `libg90::libg90` (static library).

Design choices (raw encode, soft CRC, TX arms): [`DESIGN.md`](DESIGN.md).

## Layering

| Layer | Headers | Role |
|-------|---------|------|
| Codec | `head.hpp`, `body.hpp`, `crc.hpp` | 96 B head encode/decode; 372 B body decode |
| Presets | `defaults.hpp` | Common `HeadEncodeRequest` shortcuts |
| Wire | `de9.hpp` | `ByteStream`, `SerialPort`, `Session`, `De9Link::exchange` |
| Core | `types.hpp`, `error.hpp` | `Result<T>`, `Error` |
| Meta | `version.hpp`, `log.hpp` | Version string, log levels |

**Bench CLI:** `g90ctl` — see [`CLI.md`](CLI.md).

## Head (96 bytes, display → radio)

- **`encode_head(HeadEncodeRequest)`** — build a command frame (freq, mode, filters, overlays).
- **`decode_head(Profile, HeadFrame)`** — parse a head frame; sets `DecodedHead::crc_ok`.
- **`head_frame_requests_tx(HeadFrame)`** — detect PTT/tune/VOX arms (`Result<bool>`; `Err` on bad sync/CRC).
- Helpers: `parse_profile`, `mode_from_string`, filter encode/decode, overlay packers.

Encode is **raw** (does not strip PTT/tune/VOX). See [`DESIGN.md`](DESIGN.md).

## Body (372 bytes, radio → display)

- **`decode_body(BodyFrame)`** — meters/status, raw header tail 13–47, 160× FFT bins, fingerprint.
- **`compute_fft_fingerprint`** — sum / max / argmax over FFT bins; not absolute dBm.

`decode_body` returns `Ok` with `crc_ok=false` on CRC mismatch.

## Wire round-trip

- **`g90::de9::SerialPort`** — POSIX termios 115200 8N1 raw; `write_exact` / `read_exact` take a timeout (`Error::Timeout`); I/O via `poll`.
- **`g90::de9::Session::hunt_body`** — CRC-valid 372 B window with 1-byte slip; validates a completed window even if the deadline has elapsed.
- **`g90::de9::Session::exchange`** — flush RX → TX 96 B head → `hunt_body` → `decode_body` (shared timeout budget).
- **`g90::De9Link::exchange(HeadEncodeRequest, timeout)`** — encode with link profile → session exchange → `DecodedBody`. Move-disabled (fail closed).
- Injectable **`g90::de9::ByteStream`** for tests (`FakeStream`) and serial (`SerialPort`).

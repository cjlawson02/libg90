# Design decisions

Plain-language choices for how libg90 behaves on the wire and at the CLI.

## Raw encode

`encode_head` builds the 96-byte frame from the request as given. It does **not**
strip PTT, ATU-tune, or VOX bits. Applications and the CLI own TX policy.

`g90ctl live` is fail-closed: it refuses to send when `head_frame_requests_tx`
returns an error (bad sync/CRC) or `true`, unless `--allow-tx` is set. The
check runs before the serial port is opened. The wire/`De9Link` layer stays
dumb — it transmits whatever frame it is given.

## Soft CRC on decode

Library `decode_head` / `decode_body` return `Ok` with `crc_ok` set from the
STM32 CRC check. A CRC mismatch is not a hard decode failure; callers decide
how to treat it.

- `g90ctl decode` exits non-zero when `!crc_ok`.
- `Session::hunt_body` only accepts CRC-valid 372-byte windows (1-byte slip
  hunt). Live exchange therefore only surfaces CRC-valid bodies.

## LabExtras::allow_tx

`LabExtras::allow_tx` is the master switch for encode-side arm flags
(`arm_ptt`, `arm_atu_tune`, `arm_vox`). Those arms are ignored unless
`allow_tx` is set. CLI `--arm-*` sets `allow_tx` so the frame carries the bits;
CLI `--allow-tx` is the separate permit to send a TX-armed frame.

## Shutdown

`HeadControls::shutdown_req` is a normal optional bit. The library does not
auto-clear or refuse it; the caller is responsible.

## Deferred

Not implemented yet:

- Absolute FFT / meter calibration (dBm and similar)
- Other host-side calibration helpers beyond the current fingerprint
  (sum / max / argmax over FFT bins)

## Fixtures

Static DE-9 hex under `tests/fixtures/` — checked-in wire bytes for unit tests.
No sync script or external pin required to run the suite.

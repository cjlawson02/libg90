# g90ctl CLI

`encode` / `decode` / `live` call the library only (no second codec).

## Version

```text
g90ctl --version
# → g90ctl 0.1.0
```

## Commands

| Command | Notes |
|---------|-------|
| `encode` | Flags → hex |
| `decode head` / `decode body` | Hex → fields |
| `live` | `--port` or `--auto-port`; **refuses TX** if `head_frame_requests_tx` is Err or true unless `--allow-tx` |
| `live --verbose` | + hex dump of TX/RX |
| `live --confirm-bench` | Ack wiring (3.3 V, head unplugged) — required unless `--i-know-what-im-doing` |
| `live --arm-ptt` / `--arm-atu-tune` / `--arm-vox` | Encode TX arms; still refused without `--allow-tx` |
| `live --allow-tx` | Permit frames that key TX/PTT (library encode still raw) |

### encode flags

```text
g90ctl encode [--profile 1.81] [--mode USB] [--freq 14225000] [--volume 1]
              [--filter-high 2700] [--filter-low 200] [--fft-scale 1]
```

Default profile is **1.79 codec** (`1.81` aliases to the same). Output is
lowercase hex of the 96-byte frame (no spaces). Unknown flags are rejected.

### decode

```text
g90ctl decode head [--profile 1.81] <hex96>
g90ctl decode body <hex372>
```

### live

```text
g90ctl live --port /dev/cu.usbserial-XXXX --profile 1.81 \
            --mode USB --freq 14225000 --confirm-bench
```

| Flag | Behavior |
|------|----------|
| `--port PATH` | Serial device (required unless `--auto-port`) |
| `--auto-port` | Use the single matching USB serial candidate (fail if 0 or >1) |
| `--profile 1.81` | Default V179 |
| `--mode`, `--freq`, `--volume`, filter flags | Same as `encode` → `HeadEncodeRequest` |
| `--timeout MS` | Shared write + body-hunt budget (default 3000) |
| `--confirm-bench` | Required ack (3.3 V, head unplugged) |
| `--i-know-what-im-doing` | Skip confirm (**dangerous**) |
| `--arm-ptt` / `--arm-atu-tune` / `--arm-vox` | Encode TX arms into the frame |
| `--allow-tx` | Permit sending TX-armed frames |
| `--verbose` | Print TX/RX hex + decoded fields |

Unknown tokens and flags without values are hard errors.

Default summary line:

```text
crc_ok=true S=-73 Po=0 SWR=0 V=13.5 fft_peak=18@42
```

Exit codes: **0** CRC-valid reply; **1** hard error / refused TX / missing confirm.
(`hunt_body` only returns CRC-valid frames, so live has no soft-CRC exit 2.)
`decode` still uses **2** when `!crc_ok`.

## Locked policy

- Library encode is **raw** (does not auto-clear PTT/tune/VOX). See [`DESIGN.md`](DESIGN.md).
- `decode` exits **0** when `crc_ok`; **2** when soft-CRC fails; **1** on hard errors.
- `live` TX gate is **CLI-only** and **fail-closed**: arm flags encode TX bits; `head_frame_requests_tx` Err or true refuses without `--allow-tx`. Gate runs **before** opening the port.
- `LabExtras::allow_tx` (encode) is set when arm flags are present so the gate can observe the frame; CLI `--allow-tx` is the send permit.
- `HeadControls::shutdown_req` is a normal optional field — caller responsible.

## Bench wiring

DE-9 body connector: use pins 2/3/5 for USB-UART data and ground. Never pin 1
(+8 V). Confirm 3.3 V logic and keep the head unit unplugged from the body
DE-9 when benching with `g90ctl live`. Optional session notes:
[`BENCH-LOG.md`](BENCH-LOG.md).

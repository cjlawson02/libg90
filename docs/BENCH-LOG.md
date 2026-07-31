# Bench log

Optional session notes when running live hardware with `g90ctl live`.

## Template

| Date | Radio / FW | Adapter | Profile | Result | Notes |
|------|------------|---------|---------|--------|-------|
| 2026-07-30 | MainUnit v1.81 | FT232 `cu.usbserial-B0044H9D` | 1.81 | **PASS** | RX-only; smoke + freq/mode + 15× soak; all `crc_ok=true`, `Po=0` |

### Wiring checklist

- [x] Head unit unplugged from body DE-9
- [x] USB-UART on DE-9 pins 2/3/5 only (never pin 1 +8 V)
- [x] 3.3 V logic confirmed
- [x] `g90ctl live --confirm-bench` acknowledged

### Session notes

RX-only (no `--allow-tx` / `--arm-ptt` / `--arm-atu-tune` / `--arm-vox`).

| Step | Output |
|------|--------|
| Smoke USB 14.225 | `crc_ok=true S=125 Po=0 SWR=0 V=13.5 fft_peak=7@64` |
| Freq 14.100 | `crc_ok=true S=123 Po=0 SWR=0 V=13.5 fft_peak=8@46` |
| Freq 14.225 | `crc_ok=true S=122 Po=0 SWR=0 V=13.5 fft_peak=9@57` |
| Mode CW | `crc_ok=true S=125 Po=0 SWR=0 V=13.5 fft_peak=10@71` |
| Mode USB | `crc_ok=true S=123 Po=0 SWR=0 V=13.5 fft_peak=8@79` |
| Soak ×15 | ok=15 fail=0 po_nonzero=0 |

FFT peak bin changed across freq/mode steps; S-meter moved slightly. No TX.

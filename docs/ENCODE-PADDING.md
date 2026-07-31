# Encode padding (stock defaults)

`encode_head` applies stock padding defaults for the control region (bytes
36–91) **before** overlays from `HeadControls` / `LabExtras`.

Cross-check: `Head.ControlsPackAndOverlay` and wire cases for profile **1.78**
with raw filters 100/4.

## 1. Default template bytes

Shared by V178 and V179 after VFO packing (sync `55 aa`, VFO-A @4, VFO-B @20):

| Offset | Default | Notes |
|--------|---------|-------|
| 27 (pad2b) | `0x55` | VFO-B pad; `LabExtras::pad2b` overrides |
| 38 | `CW_QSK` then operate OR | stock healthy stream → `0x03` |
| 39 | `125` | rclk_raw_high |
| 42–43 | `0x78` / `0x08` | AF gain 15 + beep |
| 44 | `1` | rf_power |
| 45 | `0` | sql |
| 46 | `0x33` | nb level/width 3/3 |
| 48 | `volume` | from `HeadEncodeRequest::volume` |
| 49 | `10` | mic_gain |
| 50–53 | `0x50`, `2`, `unknown2`, `6` | `unknown2` default 2 |
| 56–59 | `FF×4` | unknown3 |
| 60–61 | `10`, `0x01` | cw_wpm / cw_mlr |
| 62–81 | unknown4 blob | stock default blob |
| 82–83 | `0x01`, `0x98` | swr / aux vols |
| 86–91 | unknown5 | `FF FF 00 00 00 00` |
| 92–95 | STM32 CRC-32 | over bytes 0–91 |

Filter defaults: unset → V179 Hz 2700/200; V178 stock high edge **2725**.
Explicit Hz (including V178 **2700**) is preserved. Raw filter fields override
Hz and must be supplied as a pair.

## 2. Overlay order

1. Sync + VFO pack (freq/mode/filter/fft)
2. Stock padding table above
3. `HeadControls` overlays (NB/split/RIT/RCLK/SQL/…)
4. `LabExtras` operate / TX-arm policy
5. CRC over bytes 0–91

## 3. LabExtras

| Flag | Effect |
|------|--------|
| `pad2b` / `unknown2` | Replace stock padding bytes |
| `clear_operate` | Clear operate/PTT/tune/VOX |
| `allow_tx` | **Master TX switch** — required for any of `arm_ptt` / `arm_atu_tune` / `arm_vox` |
| `arm_ptt` / `arm_atu_tune` / `arm_vox` | Ignored unless `allow_tx` |
| (default) | Stock operate ON, PTT/tune/VOX OFF |

Filter / FFT optionals: unset Hz → stock (V178 **2725**/200, V179 **2700**/200);
explicit V178 `2700` is preserved. Unset `fft_scale` → `1` on A and inherit on B;
explicit B `1` is not overwritten. Both filter raws required together.

Library encode is **raw** — it does not strip caller-set PTT/tune/VOX. See
[`DESIGN.md`](DESIGN.md).

## 4. Round-trip

`Head.Roundtrip178` / `Head.Roundtrip179`: encode → decode → re-encode from
decoded raw filter/mode/fft fields must be byte-identical (V178) / field-stable
(V179).

#!/usr/bin/env python3
"""Optional maintainer tool: export golden vectors into tests/vectors/ (JSONL).

May import a head encoder from an optional reference codec checkout (sibling or
elsewhere) when refreshing checked-in cross-check vectors.

Usage::

  python3 scripts/export_golden_vectors.py [--ref-codec PATH] [--out tests/vectors]

Cross-check only: authoritative DE-9 fixtures live in ``tests/fixtures/``
(checked in as static test data). Vector export emits JSONL records that pair
encode inputs with expected wire hex for library round-trips.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def _resolve_ref_codec(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit).expanduser().resolve()
    import os

    env = os.environ.get("LIBG90_REF_CODEC_REPO")
    if env:
        return Path(env).expanduser().resolve()
    raise SystemExit("set --ref-codec PATH or LIBG90_REF_CODEC_REPO")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--ref-codec",
        default=None,
        help="Path to optional reference codec checkout",
    )
    ap.add_argument(
        "--out",
        default=None,
        help="Output directory (default: <repo>/tests/vectors)",
    )
    args = ap.parse_args(argv)

    root = Path(__file__).resolve().parents[1]
    out_dir = Path(args.out).expanduser().resolve() if args.out else root / "tests" / "vectors"
    ref_codec = _resolve_ref_codec(args.ref_codec)

    sys.path.insert(0, str(ref_codec / "src"))
    from g90_codec import encode_head_frame  # noqa: E402

    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "head_encode.jsonl"

    cases = [
        {
            "id": "volume_ladder_vol0",
            "profile": "1.78",
            "volume": 0,
            "filter_high_raw": 100,
            "filter_low_raw": 4,
            "fft_scale": 1,
            "extras": {"pad2b": 0x55, "unknown2": 2},
        },
        {
            "id": "volume_ladder_vol5",
            "profile": "1.78",
            "volume": 5,
            "filter_high_raw": 100,
            "filter_low_raw": 4,
            "fft_scale": 1,
            "extras": {"pad2b": 0x55, "unknown2": 2},
        },
        {
            "id": "v179_usb_default",
            "profile": "1.79",
            "mode_a": "USB",
            "freq_a_hz": 14_225_000,
            "filter_high_hz": 2700,
            "filter_low_hz": 200,
            "fft_scale": 1,
            "volume": 1,
        },
        {
            "id": "v181_cw_identical",
            "profile": "1.81",
            "mode_a": "CW",
            "freq_a_hz": 7_030_000,
            "filter_high_hz": 500,
            "filter_low_hz": 200,
            "fft_scale": 1,
        },
        {
            "id": "v178_roundtrip_cw",
            "profile": "1.78",
            "mode_a": "CW",
            "freq_a_hz": 14_225_000,
            "filter_high_hz": 2725,
            "filter_low_hz": 200,
            "fft_scale": 5,
            "volume": 3,
        },
    ]

    with path.open("w", encoding="utf-8") as fh:
        for case in cases:
            kwargs = {k: v for k, v in case.items() if k != "id"}
            profile = kwargs.pop("profile")
            wire = encode_head_frame(profile, **kwargs)
            rec = {
                "id": case["id"],
                "profile": profile,
                "request": {k: v for k, v in case.items() if k not in ("id", "extras")},
                "extras": case.get("extras"),
                "wire_hex": wire.hex(),
            }
            fh.write(json.dumps(rec, sort_keys=True) + "\n")

    print(f"wrote {path} ({len(cases)} vectors)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

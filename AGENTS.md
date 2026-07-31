# Agent notes

libg90 is a stand-alone C++20 library. Justify behavior from this tree:
`docs/DESIGN.md`, `docs/ENCODE-PADDING.md`, and `tests/fixtures/`.

## Background protocol notes

Optional deeper protocol notes and a Python codec (not a build dependency,
not normative for this tree):

https://github.com/cjlawson02/xiegu-g90-research

Use that repo only when a maintainer asks, or when refreshing optional
reference vectors via `scripts/export_golden_vectors.py --ref-codec`.

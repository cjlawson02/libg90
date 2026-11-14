#!/usr/bin/env bash
# Run the CI workflow's steps (.github/workflows/ci.yml) on this machine: lint
# (clang-format, clang-tidy), dev build + tests, sanitize build + tests.
# The pre-push hook runs this; it can also be run by hand.
#
# Only the host compiler is covered: CI's Ubuntu gcc / clang jobs still run there.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

step() {
  echo "ci_local: $*"
}

step "clang-format"
bash scripts/clang_format.sh check

step "configure (dev)"
cmake --preset dev >/dev/null

step "clang-tidy"
bash scripts/clang_tidy.sh

step "build + test (dev)"
cmake --build --preset dev -j "$jobs" >/dev/null
ctest --preset dev --output-on-failure

step "build + test (sanitize)"
cmake --preset sanitize >/dev/null
cmake --build --preset sanitize -j "$jobs" >/dev/null
ctest --preset sanitize --output-on-failure

step "OK"

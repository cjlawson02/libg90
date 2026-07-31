#!/usr/bin/env bash
# Check or apply .clang-format to tracked C++ sources.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang_format: clang-format not found in PATH" >&2
  exit 1
fi

files=()
while IFS= read -r f; do
  files+=("$f")
done < <(git ls-files '*.cpp' '*.hpp')

if ((${#files[@]} == 0)); then
  echo "clang_format: no tracked C++ files"
  exit 0
fi

mode="${1:-check}"
case "$mode" in
  check)
    clang-format --dry-run --Werror "${files[@]}"
    echo "clang_format: OK (${#files[@]} files)"
    ;;
  fix)
    clang-format -i "${files[@]}"
    echo "clang_format: formatted ${#files[@]} files"
    ;;
  *)
    echo "usage: $0 [check|fix]" >&2
    exit 2
    ;;
esac

#!/usr/bin/env bash
# Run clang-tidy on libg90 and g90ctl sources using compile_commands.json.
#
# macOS: Xcode does not ship clang-tidy. Install Homebrew LLVM and either add
#   export PATH="$(brew --prefix llvm)/bin:$PATH"
# or set CLANG_TIDY=/opt/homebrew/opt/llvm/bin/clang-tidy.
# Homebrew clang-tidy also needs the macOS SDK + libc++ include paths injected
# (see CoolProp #2926 / duckdb #18613); this script adds them automatically.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build_dir="${CLANG_TIDY_BUILD_DIR:-build/dev}"
compile_db="$build_dir/compile_commands.json"

find_clang_tidy() {
  if [[ -n "${CLANG_TIDY:-}" && -x "${CLANG_TIDY}" ]]; then
    echo "${CLANG_TIDY}"
    return 0
  fi
  if command -v clang-tidy >/dev/null 2>&1; then
    command -v clang-tidy
    return 0
  fi
  if [[ "$(uname -s)" == "Darwin" ]]; then
    local candidate
    for candidate in \
      /opt/homebrew/opt/llvm@21/bin/clang-tidy \
      /opt/homebrew/opt/llvm/bin/clang-tidy \
      /opt/homebrew/opt/llvm@18/bin/clang-tidy \
      /usr/local/opt/llvm@21/bin/clang-tidy \
      /usr/local/opt/llvm/bin/clang-tidy \
      /usr/local/opt/llvm@18/bin/clang-tidy; do
      if [[ -x "$candidate" ]]; then
        echo "$candidate"
        return 0
      fi
    done
  fi
  return 1
}

clang_tidy_bin=""
if ! clang_tidy_bin="$(find_clang_tidy)"; then
  echo "clang_tidy: clang-tidy not found" >&2
  echo "clang_tidy: Ubuntu CI uses apt install clang-tidy" >&2
  echo "clang_tidy: macOS: brew install llvm && export PATH=\"\$(brew --prefix llvm)/bin:\$PATH\"" >&2
  echo "clang_tidy: or set CLANG_TIDY=/path/to/clang-tidy" >&2
  exit 1
fi

if [[ ! -f "$compile_db" ]]; then
  echo "clang_tidy: generating $compile_db"
  cmake --preset dev
fi

files=()
while IFS= read -r f; do
  files+=("$f")
done < <(git ls-files 'lib/src/*.cpp' 'cli/*.cpp')

if ((${#files[@]} == 0)); then
  echo "clang_tidy: no implementation sources to analyze"
  exit 0
fi

extra_args=()
if [[ "${CLANG_TIDY_FIX:-0}" == "1" ]]; then
  extra_args+=(-fix-errors)
fi

# macOS: inject SDK + libc++ paths so <cstdint>, <string>, etc. resolve when
# using Homebrew clang-tidy against AppleClang compile_commands.json.
if [[ "$(uname -s)" == "Darwin" ]] && command -v xcrun >/dev/null 2>&1; then
  sdk_path="$(xcrun --show-sdk-path 2>/dev/null || true)"
  if [[ -n "$sdk_path" ]]; then
    extra_args+=(
      "--extra-arg=-isysroot${sdk_path}"
      "--extra-arg=-stdlib=libc++"
      "--extra-arg=-isystem${sdk_path}/usr/include/c++/v1"
    )
  fi
fi

if ((${#extra_args[@]} > 0)); then
  "$clang_tidy_bin" -p "$build_dir" "${extra_args[@]}" "${files[@]}"
else
  "$clang_tidy_bin" -p "$build_dir" "${files[@]}"
fi
echo "clang_tidy: OK (${#files[@]} files, binary=${clang_tidy_bin})"

#!/usr/bin/env bash
# Use the repo's hooks (scripts/hooks): pre-push runs scripts/ci_local.sh.
# core.hooksPath works in a plain clone and as a submodule (.git is a file there).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
git -C "$root" config core.hooksPath scripts/hooks
chmod +x "$root/scripts/hooks/pre-push"
echo "install_hooks: core.hooksPath = scripts/hooks (pre-push runs scripts/ci_local.sh)"

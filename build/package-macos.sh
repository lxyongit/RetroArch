#!/usr/bin/env bash
# Builds the customised Metal frontend and writes a replaceable app bundle to
# build/macos/RetroArch.app. Run from any directory with:
#   bash build/package-macos.sh
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${source_dir}/build/macos"

cd "${source_dir}"
./configure
make -j"$(sysctl -n hw.ncpu)" bundle BUNDLE="${output_dir}/RetroArch.app"

printf '\nBuilt app: %s\n' "${output_dir}/RetroArch.app"

#!/usr/bin/env bash
# Build the Intel (x86_64) macOS app bundle.
#
# Usage:
#   bash build/package-macos-intel.sh [configure options]
#
# On Apple Silicon this runs configure probes through Rosetta while keeping
# make and the Metal toolchain native. C/C++ output is still forced to x86_64.
# Intel Homebrew dependencies must be installed under /usr/local.
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_script="${source_dir}/build/package-macos.sh"

export MACOS_ARCH=x86_64
export MACOS_OUTPUT_DIR="${source_dir}/build/macos-intel"
export MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-12.0}"

# Do not let an inherited Apple Silicon pkg-config path select arm64
# libraries. The common packaging script adds Intel FFmpeg's keg path when
# it is installed.
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig"

# If this script was started from a Rosetta terminal, return to a native shell
# first. make must remain native so the current Metal compiler does not run as
# x86_64 and crash; only configure probes need the translated environment.
if [[ "$(sysctl -in sysctl.proc_translated 2>/dev/null || true)" == "1" ]]; then
  exec /usr/bin/arch -arm64 /bin/bash "${BASH_SOURCE[0]}" "$@"
fi

if [[ "$(uname -m)" == "arm64" ]]; then
  if ! /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then
    echo "无法启动 x86_64 进程，请先安装 Rosetta 2：" >&2
    echo "  softwareupdate --install-rosetta --agree-to-license" >&2
    exit 1
  fi

  export MACOS_CONFIGURE_ARCH=x86_64
fi

# Keep make and the Metal toolchain native by preferring the system tools.
# Intel-only Homebrew utilities and libraries remain available in /usr/local.
export PATH="/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin:/usr/local/sbin"

exec /bin/bash "${package_script}" "$@"

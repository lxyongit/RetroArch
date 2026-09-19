#!/usr/bin/env bash
# Run this in an MSYS2 MINGW64 shell. It writes retroarch.exe and every
# required MinGW DLL and the Chinese fallback font to build/windows-x64/.
#
# Prerequisites (once):
#   pacman -Syu
#   pacman -S --needed base-devel curl git mingw-w64-x86_64-toolchain
#
# Build with:
#   bash build/package-windows-x64.sh
set -euo pipefail

if [[ -z "${MINGW_PREFIX:-}" ]]; then
  echo "请在 MSYS2 MINGW64 终端中执行此脚本。" >&2
  exit 1
fi

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${source_dir}/build/windows-x64"
assets_pkg_dir="${output_dir}/assets/pkg"
assets_url="${RETROARCH_ASSETS_URL:-https://raw.githubusercontent.com/libretro/retroarch-assets/master}"
font_tmp="${assets_pkg_dir}/.chinese-fallback-font.ttf.tmp"

cd "${source_dir}"
./configure
make -j"$(nproc)" info all

mkdir -p "${output_dir}"
cp -f retroarch.exe "${output_dir}/"
cp -f retroarch.cfg "${output_dir}/retroarch.default.cfg"

# The Chinese message table in retroarch.exe is UTF-8. Ozone/XMB deliberately
# loads this CJK-capable font for both Chinese locales; without it the packaged
# application falls back to a Latin/bitmap font and Chinese text is unreadable.
mkdir -p "${assets_pkg_dir}"
trap 'rm -f "${font_tmp}"' EXIT
curl --fail --location --retry 3 \
  --output "${font_tmp}" \
  "${assets_url}/pkg/chinese-fallback-font.ttf"
mv -f "${font_tmp}" "${assets_pkg_dir}/chinese-fallback-font.ttf"
curl --fail --location --retry 3 \
  --output "${assets_pkg_dir}/chinese-fallback-font.txt" \
  "${assets_url}/pkg/chinese-fallback-font.txt"

# Keep the application portable: copy all runtime DLLs installed by the
# active MINGW64 environment, while leaving Windows system DLLs untouched.
while IFS= read -r library; do
  [[ -n "${library}" && -f "${library}" ]] || continue
  cp -f "${library}" "${output_dir}/"
done < <(ldd retroarch.exe | awk -v prefix="${MINGW_PREFIX}" '$3 ~ "^" prefix { print $3 }')

printf '\nBuilt package: %s\n' "${output_dir}"

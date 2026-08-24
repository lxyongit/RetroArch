#!/usr/bin/env bash
# Build a portable Metal app bundle for macOS.
#
# Usage:
#   bash build/package-macos.sh [configure options]
#
# The normal RetroArch build links against Homebrew libraries by absolute
# install name. The upstream Xcode packaging step embeds the libraries it
# needs, so this script does the same for the Makefile bundle target: collect
# Homebrew dylibs recursively, rewrite their install names, and sign the
# resulting bundle after all Mach-O files have been changed.
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${source_dir}/build/macos"
bundle="${output_dir}/RetroArch.app"
frameworks_dir="${bundle}/Contents/Frameworks"
jobs="${JOBS:-$(sysctl -n hw.ncpu)}"

cd "${source_dir}"

# Homebrew keeps FFmpeg's pkg-config files inside its keg. Without this path
# RetroArch may disable FFmpeg during configure while stale FFmpeg object
# dependency files from an earlier build still remain in obj-unix/.
ffmpeg_prefix=""
if command -v brew >/dev/null 2>&1; then
  ffmpeg_prefix="$(brew --prefix ffmpeg 2>/dev/null || true)"
fi
if [[ -z "${ffmpeg_prefix}" && -d /opt/homebrew/opt/ffmpeg ]]; then
  ffmpeg_prefix="/opt/homebrew/opt/ffmpeg"
fi
if [[ -n "${ffmpeg_prefix}" && -d "${ffmpeg_prefix}" ]]; then
  export PKG_CONFIG_PATH="${ffmpeg_prefix}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  export CFLAGS="${CFLAGS:-} -I${ffmpeg_prefix}/include"
  export LDFLAGS="${LDFLAGS:-} -L${ffmpeg_prefix}/lib"
fi

# configure changes feature flags, so old object/dependency files must not be
# reused across configurations (especially when FFmpeg was just enabled or
# disabled).
make clean
./configure "$@"
make -j"${jobs}" bundle BUNDLE="${bundle}"

executable="${bundle}/Contents/MacOS/RetroArch"
if [[ ! -f "${executable}" ]]; then
  echo "打包失败：未找到 ${executable}" >&2
  exit 1
fi

mkdir -p "${frameworks_dir}"

is_bundled_dependency() {
  case "$1" in
    /opt/homebrew/*|/usr/local/*) return 0 ;;
    *) return 1 ;;
  esac
}

dependency_install_name() {
  local binary="$1"
  local name="$2"

  if [[ "${binary}" == "${executable}" ]]; then
    printf '@rpath/%s\n' "${name}"
  else
    # All copied dylibs live in the same Frameworks directory.
    printf '@loader_path/%s\n' "${name}"
  fi
}

declare -A visited

bundle_macho_dependencies() {
  local binary="$1"
  local dependency name destination rewritten

  [[ -f "${binary}" ]] || return 0
  [[ -n "${visited[${binary}]+yes}" ]] && return 0
  visited["${binary}"]=1

  while IFS= read -r dependency; do
    [[ -n "${dependency}" ]] || continue
    is_bundled_dependency "${dependency}" || continue

    if [[ ! -f "${dependency}" ]]; then
      echo "找不到构建依赖：${dependency}" >&2
      echo "请先安装对应的 Homebrew 运行库，或关闭该功能后再打包。" >&2
      exit 1
    fi

    name="$(basename "${dependency}")"
    destination="${frameworks_dir}/${name}"
    if [[ ! -f "${destination}" ]]; then
      # -L dereferences Homebrew's opt symlinks; the bundle must contain the
      # actual file rather than a symlink back into /opt/homebrew.
      cp -Lf "${dependency}" "${destination}"
    fi

    rewritten="$(dependency_install_name "${binary}" "${name}")"
    install_name_tool -change "${dependency}" "${rewritten}" "${binary}"

    # Give each embedded dylib a self-contained install name as well. The
    # main executable uses its @rpath, while dylibs refer to siblings via
    # @loader_path.
    install_name_tool -id "@rpath/${name}" "${destination}" 2>/dev/null || true
    bundle_macho_dependencies "${destination}"
  done < <(otool -L "${binary}" | awk 'NR > 1 { print $1 }')
}

bundle_macho_dependencies "${executable}"

if ! otool -l "${executable}" | grep -Fq 'path @executable_path/../Frameworks'; then
  install_name_tool -add_rpath '@executable_path/../Frameworks' "${executable}"
fi

# Re-sign after install_name_tool changed the executable and embedded dylibs.
# Nested code must be signed before the outer app bundle.
while IFS= read -r library; do
  codesign --force --sign - --timestamp=none "${library}"
done < <(find "${frameworks_dir}" -type f \( -name '*.dylib' -o -perm -111 \) -print)

codesign --force --sign - --timestamp=none "${bundle}"

printf '\nBuilt app: %s\n' "${bundle}"

# RetroArch custom build scripts

These scripts build the local source tree, including the custom network
commands `SET_CHEAT`, `CHEAT_RESET`, and `SET_CORE_OPTION`.

## macOS

Run on macOS with Xcode command line tools installed:

```bash
bash build/package-macos.sh
```

Output: `build/macos/RetroArch.app`.

To build an Intel (`x86_64`) app, run:

```bash
bash build/package-macos-intel.sh
```

Output: `build/macos-intel/RetroArch.app`.

On an Apple Silicon Mac this requires Rosetta 2. Optional third-party
dependencies must be installed with Intel Homebrew under `/usr/local`; the
script rejects ARM-only libraries instead of placing them in the Intel app.
The Intel bundle targets macOS 12.0 by default to match current Homebrew
bottles. Override this only when all linked libraries support an older system:

```bash
MACOS_DEPLOYMENT_TARGET=10.13 bash build/package-macos-intel.sh
```

## Windows x64

Run in an MSYS2 **MINGW64** shell after installing the prerequisites listed at
the top of `package-windows-x64.sh`:

```bash
bash build/package-windows-x64.sh
```

Alternatively, run `build\package-windows-x64.bat` from Windows Explorer or
Command Prompt. It locates MSYS2 and launches the same script in a MINGW64
environment. Set `MSYS2_ROOT` first when MSYS2 is installed in a non-standard
location.

Output: `build/windows-x64/`, containing `retroarch.exe`, its required DLLs,
and the Chinese fallback font under `assets/pkg/`. The font is downloaded from
the official `libretro/retroarch-assets` repository while packaging so the
Chinese UI renders correctly on a clean Windows installation.

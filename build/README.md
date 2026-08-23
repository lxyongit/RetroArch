# RetroArch custom build scripts

These scripts build the local source tree, including the custom network
commands `SET_CHEAT`, `CHEAT_RESET`, and `SET_CORE_OPTION`.

## macOS

Run on macOS with Xcode command line tools installed:

```bash
bash build/package-macos.sh
```

Output: `build/macos/RetroArch.app`.

## Windows x64

Run in an MSYS2 **MINGW64** shell after installing the prerequisites listed at
the top of `package-windows-x64.sh`:

```bash
bash build/package-windows-x64.sh
```

Output: `build/windows-x64/`, containing `retroarch.exe` and its required DLLs.

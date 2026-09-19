@echo off
setlocal EnableExtensions

rem Windows launcher for package-windows-x64.sh.
rem It enters an MSYS2 MINGW64 environment so configure, make, ldd and the
rem dependency-copying logic remain identical to the shell packaging path.

set "SOURCE_DIR=%~dp0.."
set "MSYS2_SHELL="

if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\msys2_shell.cmd" (
  set "MSYS2_SHELL=%MSYS2_ROOT%\msys2_shell.cmd"
)

if not defined MSYS2_SHELL if exist "C:\msys64\msys2_shell.cmd" (
  set "MSYS2_SHELL=C:\msys64\msys2_shell.cmd"
)

if not defined MSYS2_SHELL if exist "%LOCALAPPDATA%\Programs\msys64\msys2_shell.cmd" (
  set "MSYS2_SHELL=%LOCALAPPDATA%\Programs\msys64\msys2_shell.cmd"
)

if not defined MSYS2_SHELL (
  echo [ERROR] MSYS2 was not found.
  echo Install MSYS2 in C:\msys64 or set MSYS2_ROOT to its installation directory.
  echo Required packages:
  echo   pacman -S --needed base-devel curl git mingw-w64-x86_64-toolchain
  exit /b 1
)

pushd "%SOURCE_DIR%" || (
  echo [ERROR] Cannot enter the source directory: "%SOURCE_DIR%"
  exit /b 1
)

set "CHERE_INVOKING=1"
call "%MSYS2_SHELL%" -defterm -no-start -mingw64 -here -c "bash build/package-windows-x64.sh"
set "BUILD_RESULT=%ERRORLEVEL%"

popd

if not "%BUILD_RESULT%"=="0" (
  echo [ERROR] Windows x64 packaging failed with exit code %BUILD_RESULT%.
  exit /b %BUILD_RESULT%
)

echo.
echo Windows x64 package completed successfully.
exit /b 0

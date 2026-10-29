@echo off
setlocal
cd /d "%~dp0"
set "TC=%~dp0tools\llvm-mingw-20261020-ucrt-x86_64\bin"
if defined CORETRACE_TOOLCHAIN set "TC=%CORETRACE_TOOLCHAIN%"
if not exist "%TC%\clang++.exe" (
  echo Compiler not found. Run setup-tools.ps1 or set CORETRACE_TOOLCHAIN to llvm-mingw bin.
  exit /b 1
)
"%TC%\x86_64-w64-mingw32-windres.exe" -I native-src native-src\CoreTrace.rc -O coff -o native-src\CoreTrace.res
if errorlevel 1 exit /b 1
"%TC%\clang++.exe" -std=c++20 -O2 -static -municode -mwindows -DUNICODE -D_UNICODE native-src\telemetry.cpp native-src\ui.cpp native-src\CoreTrace.res -o native-portable\CoreTrace.exe -lpdh -lpsapi -lcomctl32 -lgdi32 -luser32 -lshell32 -ladvapi32
if errorlevel 1 exit /b 1
"%TC%\llvm-strip.exe" native-portable\CoreTrace.exe

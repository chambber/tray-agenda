#!/bin/bash
# Builds and runs the pure-logic tests (time parsing, PKCE, link allowlist, event
# selection, reminders, Google event mapping) with the Windhawk compiler.
# Run from WSL; the test executable is copied to a Windows folder and executed there.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/ics_logic_tests.exe"
CLANG="/mnt/c/Program Files/Windhawk/Compiler/bin/clang++.exe"
"$CLANG" -x c++ -std=c++23 -target x86_64-w64-mingw32 -DUNICODE -D_UNICODE -DWINVER=0x0A00 \
  -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 -D__USE_MINGW_ANSI_STDIO=0 \
  -DWH_MOD -DWH_MOD_ID='L"test"' -DWH_MOD_VERSION='L"0"' -include windhawk_api.h \
  -Wno-pragma-pack -O1 -static -o "$(wslpath -w "$OUT")" "$(wslpath -w "$HERE/logic_tests.cpp")" \
  -Wl,/force:unresolved -lruntimeobject -luuid -luser32 -lwindowsapp -lshell32 -lwinhttp -lbcrypt \
  -lcrypt32 -lws2_32 -ladvapi32 -lole32 2>&1 | grep -E "error:" | grep -v "undefined symbol" || true
chmod +x "$OUT"
"$OUT"

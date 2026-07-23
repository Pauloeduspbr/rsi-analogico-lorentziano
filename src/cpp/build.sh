#!/usr/bin/env bash
# Build da DLL x64 do Laguerre RSI + deploy no terminal Pepperstone.
#   uso:  bash build.sh            -> compila e faz deploy
#         bash build.sh --no-copy  -> apenas compila
set -euo pipefail

# O g++ do MSYS2 carrega DLLs de mingw64/bin; sem esse dir no PATH o loader
# do Windows aborta antes do main (rc=1, stderr vazio).
MINGW_BIN="/c/msys64/mingw64/bin"
export PATH="$MINGW_BIN:$PATH"

GXX="$MINGW_BIN/g++.exe"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DLL="$SRC_DIR/ml_rsi.dll"

# Terminal alvo: Pepperstone MT5 (origin.txt confirma C:\Program Files\Pepperstone MetaTrader 5)
TERMINAL="/c/Users/MagnaTI/AppData/Roaming/MetaQuotes/Terminal/73B7A2420D6397DFF9014A20F1201F97"
LIB_DIR="$TERMINAL/MQL5/Libraries"
TICKMILL_LIB="/c/Users/MagnaTI/AppData/Roaming/MetaQuotes/Terminal/29E91DA909EB4475AB204481D1C2CE7D/MQL5/Libraries"

[ -x "$GXX" ] || { echo "ERRO: g++ nao encontrado em $GXX"; exit 1; }

echo "== compilando ml_rsi.dll (x64) =="
"$GXX" -O2 -std=c++17 -Wall -Wextra -shared \
  -static -static-libgcc -static-libstdc++ \
  -fno-exceptions \
  -o "$OUT_DLL" \
  "$SRC_DIR/ml_rsi.cpp"

echo "== simbolos exportados =="
"$MINGW_BIN/objdump.exe" -p "$OUT_DLL" | sed -n '/Export Address Table/,/^$/p' | head -20

if [ "${1:-}" != "--no-copy" ]; then
  [ -d "$LIB_DIR" ] || { echo "ERRO: nao existe $LIB_DIR"; exit 1; }
  cp -f "$OUT_DLL" "$LIB_DIR/"
  echo "== deploy: $LIB_DIR/ml_rsi.dll =="
  ls -l "$LIB_DIR/ml_rsi.dll"
  [ -d "$TICKMILL_LIB" ] && cp -f "$OUT_DLL" "$TICKMILL_LIB/" && echo "== deploy: $TICKMILL_LIB/ml_rsi.dll =="
fi

echo "OK"

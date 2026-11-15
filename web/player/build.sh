#!/bin/sh
# CUMBIA en el navegador: player.c -> $1/cumbia.wasm (clang con destino wasm32-wasi y la wasi-libc)
set -e
OUT=${1:-build/play}
mkdir -p build/host "$OUT"
sed 's/^int main(void)/static int ui_test_main(void)/' tests/ui_test.c > build/host/ui_test_nomain.c
clang --target=wasm32-wasi --sysroot=/usr -O2 -w -DFELUCCA_SLICE=0 -Ibuild/gen -Ifirmware/src -Ifirmware/hal \
    -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 -Itests -Ibuild/host -Iweb/player/shim -nostartfiles -Wl,--no-entry \
    -Wl,--initial-memory=16777216 -o "$OUT/cumbia.wasm" web/player/player.c -lm

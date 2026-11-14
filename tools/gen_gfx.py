#!/usr/bin/env python3
"""Los gráficos de la cumbia (assets/gfx/*.png, a su medida real) -> firmware/src/cumbia_gfx.h

Cada PNG es una máscara en gris: 0 fondo, 255 el color del tema. Se guarda a 4 bits (16 tonos: el antialias) en RLE:
  0x00..0x7F  n + 1 píxeles de fondo
  0x80..0xFF  (b >> 3) & 15 = tono, (b & 7) + 1 píxeles de ese tono
cumbia_ui.c cb_spr la pinta con la rampa del color elegido.

    python3 tools/gen_gfx.py
"""
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
GFX = ROOT / "assets/gfx"

SPRITES = (
    [f"bailarin_{i}" for i in range(8)] + [f"dj_{i}" for i in range(4)] +
    [f"acordeon_{i}" for i in range(3)] + [f"guacharaca_{i}" for i in range(4)] +
    [f"{n}_{i}" for n in ("bajo", "congas", "timbal", "micro") for i in range(3)] +
    [f"dado_{i}" for i in range(10)] +
    ["fx_rebajada", "fx_reverb", "fx_tono", "fx_eco_sonidero", "fx_boom_bap", "fx_cinta_atras", "gritos_16",
     "gritos_24", "papel_acordes", "papel_remates", "papel_voz", "papel_melodia", "papel_final",
     "logo_encabezado_20px", "logo_arranque_240"])


def rle(a):
    v = [int(x) for x in (a.astype(np.int32) >> 4).ravel()]
    out, i = bytearray(), 0
    while i < len(v):
        j = i
        if v[i] == 0:
            while j < len(v) and v[j] == 0 and j - i < 128:
                j += 1
            out.append(j - i - 1)
        else:
            while j < len(v) and v[j] == v[i] and j - i < 8:
                j += 1
            out.append(0x80 | v[i] << 3 | (j - i - 1))
        i = j
    return out


def main():
    data, rows, names = bytearray(), [], []
    for n in SPRITES:
        a = np.array(Image.open(GFX / f"{n}.png").convert("L"))
        h, w = a.shape
        b = rle(a)
        rows.append((len(data), w, h))
        data += b
        names.append(n)
    L = ["/* tools/gen_gfx.py: los gráficos de la cumbia (assets/gfx), máscaras de 4 bits en RLE */",
         "typedef struct { uint32_t off; uint8_t w, h; } cb_spr_t;"]
    L.append("enum {")
    for i, n in enumerate(names):
        L.append(f"    SPR_{n.upper()} = {i},")
    L.append(f"    SPR_N = {len(names)}")
    L.append("};")
    L.append("static const cb_spr_t CB_SPR[SPR_N] = {" +
             ", ".join(f"{{{o}u, {w}, {h if h < 256 else 0}}}" for o, w, h in rows) + "};")
    L.append(f"static const uint8_t CB_GFX[{len(data)}] = {{")
    for k in range(0, len(data), 24):
        L.append("    " + ", ".join(str(x) for x in data[k:k + 24]) + ",")
    L.append("};")
    (ROOT / "firmware/src/cumbia_gfx.h").write_text("\n".join(L) + "\n")
    print(f"gráficos: {len(names)} piezas, {len(data)} B")


if __name__ == "__main__":
    main()

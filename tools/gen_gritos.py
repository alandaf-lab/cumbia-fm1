#!/usr/bin/env python3
"""Los gritos (voz C) para la ranura de usuario 2 del FM-1 (USR2).

El grito i ocupa las teclas 12 + 24 i .. 35 + 24 i; su altura natural (CB_VOZ_M0 en cumbia.c, redondeada) suena en la
tecla 24 + 24 i, y la fracción de semitono va en la raíz de la zona: cumbia.c lo afina a la tónica o la quinta.
Escribe web/gritos.js (para el instalador) y build/gritos.hdr / .bin.

    python3 tools/gen_gritos.py
"""
import base64
import sys
from pathlib import Path

import numpy as np
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import sampleio as sio  # noqa: E402

GRITOS = [("wepa", 62.6), ("cumbia", 54.6), ("opa", 69.5), ("abailar", 60.7), ("mty", 48.0)]   # = cumbia.c CB_VOZ_M0


def one(name):
    sr, x = sio.read_any_wav(ROOT / "assets/voz" / f"{name}_C.wav")
    x = np.asarray(x, dtype=np.float64)
    from math import gcd
    g = gcd(sio.SLOT_RATE, sr)
    y = resample_poly(x, sio.SLOT_RATE // g, sr // g)
    nz = np.nonzero(np.abs(y) > .01 * np.abs(y).max())[0]
    y = y[max(0, nz[0] - 32):nz[-1] + 64]
    f = min(len(y) // 4, 400)
    y[-f:] *= np.linspace(1, 0, f)
    return y


def build():
    ys = [one(n) for n, _ in GRITOS]
    g = 30000 / max(np.abs(y).max() for y in ys)
    zones = []
    for i, ((name, m0), y) in enumerate(zip(GRITOS, ys)):
        s = [int(v) for v in np.clip(np.round(y * g), -32768, 32767)]
        r = 24 + 24 * i
        zones.append((s, r + (m0 - round(m0)), r - 12, r + 11))
    return sio.user_slot("GRITOS", zones)


if __name__ == "__main__":
    hdr, data = build()
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build/gritos.hdr").write_bytes(hdr)
    (ROOT / "build/gritos.bin").write_bytes(data)
    (ROOT / "web/gritos.js").write_text(
        "/* tools/gen_gritos.py: la ranura USR2 con los gritos (cabecera 480 B, luego el ADPCM) */\n"
        "const GRITOS = {name: 'GRITOS', hdr: '%s',\n  data: '%s'};\n"
        % (base64.b64encode(hdr).decode(), base64.b64encode(data).decode()))
    print(f"gritos: {len(GRITOS)} zonas, {len(data)} B de {sio.SLOT_MAX_DATA}")

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

GRITOS = [("wepa_C", 62.6), ("cumbia_C", 54.6), ("opa_C", 69.5), ("abailar_C", 60.7), ("mty_C", 48.0)]   # = cumbia.c CB_VOZ_M0
GRITOS2 = [("weweaa", 0), ("pagozar", 0), ("ave", 0)]
BOOST = {"weweaa": 1.4}                  # (al oído, un poco abajo de las demás: +3 dB)
COMP = {"weweaa": 2.5}                   # comprimida (tanh): más fuerte sin pasarse del pico   # USR3: frases nuevas (3 semitonos abajo, como la voz C)


def one(name):
    sr, x = sio.read_any_wav(ROOT / "assets/voz" / f"{name}.wav")
    x = np.asarray(x, dtype=np.float64)
    if name in COMP:
        x = np.tanh(COMP[name] * x / np.abs(x).max()) / np.tanh(COMP[name])
    from math import gcd
    g = gcd(sio.SLOT_RATE, sr)
    y = resample_poly(x, sio.SLOT_RATE // g, sr // g)
    nz = np.nonzero(np.abs(y) > .003 * np.abs(y).max())[0]   # (bajo: el final suave de la frase se queda)
    y = y[max(0, nz[0] - 32):nz[-1] + 64]
    f = min(len(y) // 4, 400)
    y[-f:] *= np.linspace(1, 0, f)
    return y


def rms(y):
    return float(np.sqrt(np.mean(y ** 2)))


def build(lst, name, g=None, like=None):
    """like: el RMS (ya con g) al que se igualan, uno por uno (las frases nuevas con las autorizadas)"""
    ys = [one(n) for n, _ in lst]
    g = g or 30000 / max(np.abs(y).max() for y in ys)
    if like:
        ys = [y * min(like / (rms(y) * g) * BOOST.get(n, 1), 30000 / (np.abs(y).max() * g)) for y, (n, _) in zip(ys, lst)]
    zones = []
    for i, ((_, m0), y) in enumerate(zip(lst, ys)):
        s = [int(v) for v in np.clip(np.round(y * g), -32768, 32767)]
        r = 12 + 20 * i                                   # = cumbia.c cb_grito
        zones.append((s, r, r - 10, r + 9))               # tal como se grabó: la tecla R, sin afinar
    import zlib
    hdr, data = sio.user_slot(name, zones)
    tag = name[:2] + "%06X" % (zlib.crc32(data) & 0xFFFFFF)   # el nombre cambia con el contenido: el instalador lo
    hdr = hdr[:8] + tag.encode().ljust(8, b"\0") + hdr[16:]   # reescribe si no es el mismo (no solo por el nombre)
    return (hdr, data, tag), g, float(np.mean([rms(y) * g for y in ys]))


if __name__ == "__main__":
    (ROOT / "build").mkdir(exist_ok=True)
    (hdr, data, t1), g, lv = build(GRITOS, "GRITOS")
    (hdr2, data2, t2), _, _ = build(GRITOS2, "GRITOS2", g, lv)   # cada una al nivel promedio de las autorizadas
    js = "/* tools/gen_gritos.py: USR2 los gritos de la voz C, USR3 los de las voces A y B (cabecera 480 B, el ADPCM) */\n"
    for nm, var, h, d, tag in (("GRITOS", "GRITOS", hdr, data, t1), ("GRITOS2", "GRITOS2", hdr2, data2, t2)):
        (ROOT / f"build/{nm.lower()}.hdr").write_bytes(h)
        (ROOT / f"build/{nm.lower()}.bin").write_bytes(d)
        js += "const %s = {name: '%s', hdr: '%s',\n  data: '%s'};\n" % (var, tag, base64.b64encode(h).decode(),
                                                                       base64.b64encode(d).decode())
        print(f"{nm}: {len(d)} B de {sio.SLOT_MAX_DATA}")
    (ROOT / "web/gritos.js").write_text(js)

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

GRITOS = [("wepa_C", 0), ("cumbia_C", 0), ("opa_C", 0), ("abailar_C", 0), ("mty_C", 0), ("escuchen", 0), ("ave", 0)]
GRITOS2 = [("weweaa", 0), ("pagozar", 0), ("estumusica", 0), ("ohohoh", 0)]
# USR2 y USR3 (en una sola no caben): cumbia.c CB_GRITO dice qué tecla negra toca cuál
LOUD = {"wepa_C": 0.0, "cumbia_C": 2.7, "opa_C": 7.2, "abailar_C": 5.1, "mty_C": 2.0, "weweaa": -0.6, "pagozar": -3.0, "ave": -0.5, "escuchen": -1.9, "estumusica": -6.0, "ohohoh": -0.2}   # dB: medido a través del FM-1 simulado (cumbia_sim, pyloudnorm) para que suenen
# parejos, más lo pedido al oído: cumbia +1 dB, las teclas 6, 7 y 8 -1 dB; después, en el FM-1 con la canción:
# opa, a bailar, Monterrey y ave -1.5 dB, wewea +2 dB; luego wepa +1.8, wewea, pagozar y oh oh oh +4, ave -3; después wewea +3, ave -3. Lo que sigue:
# las teclas 6, 7 y 8 un poco abajo
TARGET = -17.0                           # LUFS (sonoridad percibida, pyloudnorm), igual para todos


def one(name, thr=.005):
    sr, x = sio.read_any_wav(ROOT / "assets/voz" / f"{name}.wav")
    x = np.asarray(x, dtype=np.float64)
    from math import gcd
    g = gcd(sio.SLOT_RATE, sr)
    y = resample_poly(x, sio.SLOT_RATE // g, sr // g)
    nz = np.nonzero(np.abs(y) > thr * np.abs(y).max())[0]   # (bajo: el final suave de la frase se queda)
    y = y[max(0, nz[0] - 32):nz[-1] + 64] / max(1e-9, np.abs(x).max())
    f = min(len(y) // 4, 400)
    y[-f:] *= np.linspace(1, 0, f)
    return y


def rms_active(y):
    """el RMS de la parte que suena (sin los silencios): lo que se oye como volumen"""
    a = np.abs(y)
    k = a > .05 * a.max()
    return float(np.sqrt(np.mean(y[k] ** 2)))


def build(lst, name, thr=.005):
    zones = []
    for i, (n, _) in enumerate(lst):
        y = one(n, thr)
        import pyloudnorm
        pad = np.concatenate([y, np.zeros(sio.SLOT_RATE // 2)])          # (los cortos: al menos 0.4 s para medir)
        lufs = pyloudnorm.Meter(sio.SLOT_RATE, block_size=.2).integrated_loudness(pad)
        y = y * 10 ** ((TARGET + LOUD.get(n, 0.0) - lufs) / 20)
        if np.abs(y).max() > .92:                         # los picos que se pasen: limitados suave (el volumen se queda)
            y = np.where(np.abs(y) > .8, np.sign(y) * (.8 + .12 * np.tanh((np.abs(y) - .8) / .12)), y)
        s = [int(v) for v in np.clip(np.round(y * 32767), -32768, 32767)]
        r = 12 + 18 * i                                   # = cumbia.c cb_grito
        zones.append((s, r, r - 9, r + 8))               # tal como se grabó: la tecla R, sin afinar
    import zlib
    hdr, data = sio.user_slot(name, zones)
    tag = name[:2] + "%06X" % (zlib.crc32(data) & 0xFFFFFF)   # el nombre cambia con el contenido: el instalador lo
    hdr = hdr[:8] + tag.encode().ljust(8, b"\0") + hdr[16:]   # reescribe si no es el mismo (no solo por el nombre)
    return hdr, data, tag


if __name__ == "__main__":
    (ROOT / "build").mkdir(exist_ok=True)
    hdr, data, t1 = build(GRITOS, "GRITOS")
    hdr2, data2, t2 = build(GRITOS2, "GRITOS2", .012)   # (justa: las colas más cortas)
    js = "/* tools/gen_gritos.py: USR2 los gritos de la voz C, USR3 los de las voces A y B (cabecera 480 B, el ADPCM) */\n"
    for nm, var, h, d, tag in (("GRITOS", "GRITOS", hdr, data, t1), ("GRITOS2", "GRITOS2", hdr2, data2, t2)):
        (ROOT / f"build/{nm.lower()}.hdr").write_bytes(h)
        (ROOT / f"build/{nm.lower()}.bin").write_bytes(d)
        js += "const %s = {name: '%s', hdr: '%s',\n  data: '%s'};\n" % (var, tag, base64.b64encode(h).decode(),
                                                                       base64.b64encode(d).decode())
        print(f"{nm}: {len(d)} B de {sio.SLOT_MAX_DATA}")
    (ROOT / "web/gritos.js").write_text(js)

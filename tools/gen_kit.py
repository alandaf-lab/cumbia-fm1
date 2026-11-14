#!/usr/bin/env python3
"""El kit de percusión de la cumbia para la ranura de usuario 1 del FM-1 (USR1).

Lee los one-shots de assets/perc/ (los del prototipo web), los pasa a 22.05 kHz, los recorta con un desvanecido,
y arma la ranura (cabecera de 480 B + ADPCM) con una zona por instrumento: la tecla CB_KIT_KEY0 + i, en el orden
de tools/gen_cumbia.py INST. Escribe web/kit_perc.js (para el instalador) y build/kit_perc.hdr / .bin.

    python3 tools/gen_kit.py
"""
import base64
import sys
from pathlib import Path

import numpy as np
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import sampleio as sio  # noqa: E402
from gen_cumbia import INST  # noqa: E402

KEY0 = 48                                 # = cumbia.c CB_KIT_KEY0
LEN = {"Conga_1_Open": .45, "Conga_2": .5, "Small_Cowbell": .4, "Big_Cowbell": .27, "Woodblock": .25,
       "Guira_Long": .5, "Guira_Short": .13, "Guiro_Long": .17, "Guiro_Short": .14, "Tambora_Open": .55,
       "Tambora_Shell": .4, "Timbal_High": .55, "Timbal_Low": .55, "Crash_Mute": .7}
FADE = .35                                # la última parte de cada sonido se desvanece


def one(name):
    sr, x = sio.read_any_wav(ROOT / "assets/perc" / f"{name}.wav")
    x = np.asarray(x, dtype=np.float64)
    x = x[max(0, sio.onset(list(x), 0.01) - 8):]
    y = resample_poly(x, 1, 2) if sr == 44100 else resample_poly(x, sio.SLOT_RATE, sr)
    n = min(len(y), int(LEN[name] * sio.SLOT_RATE))
    y = y[:n].copy()
    f = max(1, int(n * FADE))
    y[-f:] *= np.cos(np.linspace(0, np.pi / 2, f)) ** 2
    return y


def boombap():
    """bombo y tarola del boom bap, sintetizados como en el prototipo (un poco opacos y saturados)"""
    sr = sio.SLOT_RATE
    rng = np.random.default_rng(7)
    t = np.arange(int(.45 * sr)) / sr
    f = 46 + (150 - 46) * np.exp(-t / .03)
    k = np.sin(2 * np.pi * np.cumsum(f) / sr) * np.minimum(1, t / .003) * np.exp(-np.maximum(0, t - .04) / .09)
    t = np.arange(int(.3 * sr)) / sr
    nz = rng.uniform(-1, 1, len(t))
    from scipy.signal import butter, lfilter
    b, a = butter(2, [1300 / (sr / 2), 2600 / (sr / 2)], "band")
    sn = lfilter(b, a, nz) * 2.2 * np.exp(-np.maximum(0, t - .01) / .06) + \
        .6 * (2 * np.abs(2 * ((185 * t) % 1) - 1) - 1) * np.exp(-t / .03)
    sat = lambda x: np.tanh(1.8 * x) / np.tanh(1.8)
    return [sat(k) * .95, sat(sn / np.abs(sn).max()) * .8]   # (el hat: la güira corta; 16 zonas a lo más)


def build():
    ys = [one(n) for n, _ in INST]
    g = 30000 / max(np.abs(y).max() for y in ys)          # un solo factor: se respeta el balance del prototipo
    ys = [y * g / 30000 for y in ys] + boombap()          # (y el boom bap a su nivel: GI_KICK, GI_SNARE)
    zones = []
    for i, y in enumerate(ys):
        s = [int(v) for v in np.clip(np.round(y * 30000), -32768, 32767)]
        zones.append((s, KEY0 + i, KEY0 + i, KEY0 + i))
    import zlib
    hdr, data = sio.user_slot("CUMBIA", zones)
    tag = "KT%06X" % (zlib.crc32(data) & 0xFFFFFF)          # el nombre cambia con el contenido (el instalador compara)
    return hdr[:8] + tag.encode().ljust(8, b"\0") + hdr[16:], data, tag


if __name__ == "__main__":
    hdr, data, tag = build()
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build/kit_perc.hdr").write_bytes(hdr)
    (ROOT / "build/kit_perc.bin").write_bytes(data)
    (ROOT / "web/kit_perc.js").write_text(
        "/* tools/gen_kit.py: la ranura USR1 con la percusión (cabecera 480 B, luego el ADPCM) */\n"
        "const KIT_PERC = {name: '%s', hdr: '%s',\n  data: '%s'};\n"
        % (tag, base64.b64encode(hdr).decode(), base64.b64encode(data).decode()))
    print(f"kit: {len(INST) + 2} zonas, {len(data)} B de {sio.SLOT_MAX_DATA}")

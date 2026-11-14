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


def build():
    ys = [one(n) for n, _ in INST]
    g = 30000 / max(np.abs(y).max() for y in ys)          # un solo factor: se respeta el balance del prototipo
    zones = []
    for i, y in enumerate(ys):
        s = [int(v) for v in np.clip(np.round(y * g), -32768, 32767)]
        zones.append((s, KEY0 + i, KEY0 + i, KEY0 + i))
    return sio.user_slot("CUMBIA", zones)


if __name__ == "__main__":
    hdr, data = build()
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build/kit_perc.hdr").write_bytes(hdr)
    (ROOT / "build/kit_perc.bin").write_bytes(data)
    (ROOT / "web/kit_perc.js").write_text(
        "/* tools/gen_kit.py: la ranura USR1 con la percusión (cabecera 480 B, luego el ADPCM) */\n"
        "const KIT_PERC = {name: 'CUMBIA', hdr: '%s',\n  data: '%s'};\n"
        % (base64.b64encode(hdr).decode(), base64.b64encode(data).decode()))
    print(f"kit: {len(INST)} zonas, {len(data)} B de {sio.SLOT_MAX_DATA}")

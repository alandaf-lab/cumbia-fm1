#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""El groove de la cumbia lenta (extraído de las pistas aisladas del paquete de percusión, en el prototipo)
como tabla C: firmware/src/cumbia_groove.h.  gen_cumbia.py assets/cumbia/lenta.json OUT.h"""
import json, sys
from pathlib import Path
# instrumento -> nota GM del motor DRUM (mientras llegan los samples reales)
INST = [("Conga_1_Open", 63), ("Conga_2", 64), ("Small_Cowbell", 56), ("Big_Cowbell", 56), ("Woodblock", 76),
        ("Guira_Long", 70), ("Guira_Short", 70), ("Guiro_Long", 74), ("Guiro_Short", 73), ("Tambora_Open", 41),
        ("Tambora_Shell", 37), ("Timbal_High", 65), ("Timbal_Low", 66), ("Crash_Mute", 49)]
GI = {"Conga_1_Open": "GI_CONGA1", "Conga_2": "GI_CONGA2", "Small_Cowbell": "GI_COWBELL_S", "Big_Cowbell": "GI_COWBELL_B",
      "Guiro_Long": "GI_GUIRO_L", "Guiro_Short": "GI_GUIRO_S"}
def main(src, out):
    g = json.loads(Path(src).read_text())
    steps = g["pasos"]
    names = [n for n, _ in INST]
    by = [[] for _ in range(steps)]
    for pos, name, gain in g["eventos"]:
        st = int(pos) % steps
        frac = min(255, int(round((pos - int(pos)) * 256)))
        vel = max(1, min(127, int(round(gain * 70))))
        by[st].append((frac, names.index(name), vel))
    L = ["/* generado por tools/gen_cumbia.py: la cumbia lenta (%d pasos de 1/16) */" % steps,
         "typedef struct { uint8_t frac, inst, vel; } cb_hit_t;   /* frac: 1/256 de paso */",
         "#define CB_GROOVE_STEPS %du" % steps]
    for i, n in enumerate(names):
        if n in GI:
            L.append("#define %s %du" % (GI[n], i))
    L.append("static const uint8_t GI_NOTE[%d] = {%s};" % (len(INST), ", ".join(str(m) for _, m in INST)))
    flat, idx = [], [0]
    for st in by:
        flat += sorted(st)
        idx.append(len(flat))
    L.append("static const cb_hit_t CB_GROOVE[%d] = {" % len(flat))
    for i in range(0, len(flat), 8):
        L.append("    " + " ".join("{%d, %d, %d}," % h for h in flat[i:i + 8]))
    L.append("};")
    L.append("static const uint16_t CB_GROOVE_IDX[%d] = {%s};" % (len(idx), ", ".join(map(str, idx))))
    Path(out).write_text("\n".join(L) + "\n")
    print("groove: %d golpes en %d pasos -> %s" % (len(flat), steps, out))
if __name__ == "__main__":
    main(*sys.argv[1:3])

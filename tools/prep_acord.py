# tools/prep_acord.py ORIG_DIR OUT_DIR smeta.json
# Prepara el acordeón para el SAMPLER de Felucca: 22.05 kHz, loop corto con crossfade, loops.json
import json, numpy as np, wave
from scipy.signal import resample_poly
import sys
SRC, OUT = sys.argv[1], sys.argv[2]   # WAV originales (MIDI.wav) y carpeta de salida
META=json.load(open(sys.argv[3]))   # {midi: {cents, ...}} medidos en el prototipo; TR=22050
loops={}; total=0
for m,mt in sorted(META.items(), key=lambda k:int(k[0])):
    w=wave.open(f'{SRC}/{m}.wav'); x=np.frombuffer(w.readframes(w.getnframes()),np.int16).astype(np.float64)
    x=resample_poly(x,1,2)
    ls=int((0.45 if len(x)>1.6*TR else 0.3)*TR); win=1024; xf=int(0.07*TR)
    best,bl=-2,None
    ref=x[ls-win:ls]
    for le in range(min(ls+int(0.75*TR), len(x)-int(0.2*TR)), min(len(x)-1, ls+int(1.15*TR))):
        c=x[le-win:le]; r=np.dot(ref,c)/(np.linalg.norm(ref)*np.linalg.norm(c)+1e-9)
        # same loudness on both sides
        r-=abs(np.sqrt(np.mean(c**2))-np.sqrt(np.mean(ref**2)))/(np.sqrt(np.mean(ref**2))+1e-9)
        if r>best: best,bl=r,le
    le=bl; y=x[:le+1].copy()
    for i in range(xf):
        a=i/xf; y[le-xf+i]=y[le-xf+i]*(1-a)+x[ls-xf+i]*a
    y=np.clip(y,-32767,32767).astype(np.int16)
    o=wave.open(f'{OUT}/{m}.wav','wb'); o.setnchannels(1); o.setsampwidth(2); o.setframerate(TR); o.writeframes(y.tobytes()); o.close()
    loops[m]={'ls':ls,'le':le,'root':int(m)+mt['cents']/100}
    total+=(le+1+1)//2
    print(m, 'loop %.2fs corr %.3f'%((le-ls)/TR,best), 'adpcm %d B'%((le+2)//2))
json.dump(loops,open(f'{OUT}/loops.json','w'),indent=1)
print('total ADPCM bytes', total)

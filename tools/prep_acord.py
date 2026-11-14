# tools/prep_acord.py ORIG_DIR OUT_DIR smeta.json
# Prepara el acordeón para el SAMPLER de Felucca: 22.05 kHz, loop corto con crossfade, loops.json
import json, numpy as np, wave
from scipy.signal import resample_poly, butter, sosfiltfilt
import sys
SRC, OUT = sys.argv[1], sys.argv[2]   # WAV originales (MIDI.wav) y carpeta de salida
META=json.load(open(sys.argv[3]))   # {midi: {cents, ...}} medidos en el prototipo
TR=22050
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
    # ataque: sin chasquido. El pico de cada ventana de 3 ms en los primeros 90 ms no pasa de 1.1x el pico
    # típico del sostenido, y fade-in de 6 ms
    hop=int(0.003*TR); n90=int(0.09*TR)
    sp=np.median([np.abs(y[i:i+hop]).max() for i in range(int(0.3*TR),int(0.45*TR),hop)])
    pk=np.array([np.abs(y[i:i+hop]).max()+1e-9 for i in range(0,n90,hop)])
    g=np.minimum(1.0, 1.1*sp/pk)
    g=np.minimum(g, np.convolve(g,np.ones(3)/3,'same'))
    g=np.interp(np.arange(n90), np.arange(len(g))*hop+hop/2, g)
    y[:n90]*=g
    # golpe de la válvula: grave por debajo de la fundamental en el arranque (fuerte de C4 a D5). Se filtra
    # en los primeros 60 ms y se regresa al original entre 60 y 120 ms
    f0=440*2**((int(m)-69)/12); n1,n2=int(0.06*TR),int(0.12*TR)
    hp=sosfiltfilt(butter(4,max(70,0.7*f0),'high',fs=TR,output='sos'), y[:n2+400])[:n2]
    a=np.ones(n2); a[n1:]=0.5+0.5*np.cos(np.pi*np.arange(n2-n1)/(n2-n1))
    y[:n2]=hp*a+y[:n2]*(1-a)
    fi=int(0.025*TR); y[:fi]*=0.5-0.5*np.cos(np.pi*np.arange(fi)/fi)   # ataque de 25 ms, como el botón real
    y=np.clip(y,-32767,32767).astype(np.int16)
    o=wave.open(f'{OUT}/{m}.wav','wb'); o.setnchannels(1); o.setsampwidth(2); o.setframerate(TR); o.writeframes(y.tobytes()); o.close()
    loops[m]={'ls':ls,'le':le,'root':int(m)+mt['cents']/100}
    total+=(le+1+1)//2
    print(m, 'loop %.2fs corr %.3f'%((le-ls)/TR,best), 'adpcm %d B'%((le+2)//2))
json.dump(loops,open(f'{OUT}/loops.json','w'),indent=1)
print('total ADPCM bytes', total)

import sys, numpy as np, subprocess
from PIL import Image, ImageDraw, ImageFont
R='/home/claude/cumbia-fm1'; IMG=f'{R}/web/landing/img'
FONT=f'{R}/assets/fonts/InterTight[wght].ttf'
def font(sz, w='Bold'):
    f=ImageFont.truetype(FONT, sz)
    try: f.set_variation_by_name(w)
    except Exception: pass
    return f
FPS=30; DUR=66; SCR_END=59.8; TOY=(999,999); OUT=60.2
raw=np.memmap(f'{R}/build/video/toma.rgb',np.uint8,'r').reshape(-1,240,240,3)
CAP=[  # inicio, fin, botón, mantener, español, inglés
 (4.0,10.3,'REC',0,'Tira el dado:\nnace una cumbia nueva','Roll the die: a brand-new cumbia'),
 (10.3,16.0,'TECLAS NEGRAS · BLACK KEYS',0,'Gritos con eco\nde sonidero','Shouts with a sonidero echo'),
 (16.0,27.5,'KNOB 1',0,'La rebajada:\nla cinta se frena','Rebajada: the tape slows down,\npitch and all'),
 (27.5,32.5,'SEQ',0,'Boom bap:\nentra el DJ','Boom bap: the DJ steps in'),
 (32.5,36.3,'SCL',1,'Ahogada','Muffled'),
 (36.3,39.8,'FX',1,'Cinta para atrás','Tape rewind'),
 (39.8,43.3,'EDIT',1,'Corte','Cut'),
 (43.3,46.8,'LFO',1,'Repetición','Repeat'),
 (46.8,50.5,'ARP',1,'Eco a todo','Echo on everything'),
 (50.5,56.0,'OCT+',0,'Medio tono arriba,\ncomo en el último tramo','Up a half step,\nlike the last stretch of the song'),
 (56.0,59.6,'HOME',0,'Cambia quién baila','Switch dancers'),
]
logo=Image.open(f'{IMG}/logo.png').convert('L')
def load(n):
    out=[]; im=Image.open(f'{IMG}/{n}.gif')
    for i in range(im.n_frames): im.seek(i); out.append((im.convert('L'),im.info['duration']))
    return out
dancer=load('bailarin'); chola=load('cholombiana')
DCYC=sum(d for _,d in dancer)
def dancer_at(ms,seq=None):
    seq=seq or dancer; ms%=DCYC
    for f,d in seq:
        if ms<d: return f
        ms-=d
def ease(x): x=max(0,min(1,x)); return x*x*(3-2*x)
def fade(t,a,b,f=0.3): return ease((t-a)/f)*ease((b-t)/f)
class Fmt:
    def __init__(s,W,H,vert):
        s.W,s.H,s.v=W,H,vert
        if vert: s.scr=(60,560); s.cap=(80,150,920); s.foot=1720
        else: s.scr=(110,60); s.cap=(1170,0,680); s.foot=None
        s.cache={}
    def caption(s,c):
        if c in s.cache: return s.cache[c]
        _,_,btn,hold,es,en=c
        x,y,w=s.cap; L=Image.new('L',(s.W,s.H)); d=ImageDraw.Draw(L)
        fb=font(30,'SemiBold'); fe=font(92 if s.v else 80); fn=font(44 if s.v else 38,'Regular')
        # medir
        lines=[]
        if btn: lines.append(('chip',btn,hold))
        lines.append(('es',es)); lines.append(('en',en))
        while max(d.textlength(l,font=fe) for l in es.split('\n'))>w and fe.size>40: fe=font(fe.size-4)
        while max(d.textlength(l,font=fn) for l in en.split('\n'))>w and fn.size>24: fn=font(fn.size-2,'Regular')
        hes=d.multiline_textbbox((0,0),es,font=fe,spacing=10)[3]; hen=d.multiline_textbbox((0,0),en,font=fn,spacing=8)[3]
        tot=(70 if btn else 0)+hes+34+hen
        cy=y+(300-tot)//2 if s.v else (s.H-tot)//2
        if btn:
            tw=d.textlength(btn,font=fb); d.rounded_rectangle((x,cy,x+tw+44,cy+50),25,outline=255,width=3); d.text((x+22,cy+8),btn,font=fb,fill=255)
            if hold:
                hl='MANTENER · HOLD'; d.text((x+tw+66,cy+10),hl,font=font(26,'Regular'),fill=150)
            cy+=70
        d.multiline_text((x,cy),es,font=fe,fill=255,spacing=10); cy+=hes+34
        d.multiline_text((x,cy),en,font=fn,fill=150,spacing=8)
        s.cache[c]=L; return L
    def frame(s,n):
        t=n/FPS; out=Image.new('L',(s.W,s.H),0); rgb=None
        # pantalla
        sa=ease((t-3.4)/0.6)*ease((SCR_END-t)/0.6)
        if sa>0:
            sc=Image.fromarray(raw[min(n,len(raw)-1)]).resize((960,960),Image.NEAREST)
            base=Image.new('RGB',(s.W,s.H)); x,y=s.scr
            ImageDraw.Draw(base).rounded_rectangle((x-22,y-22,x+982,y+982),46,fill=(21,21,21))
            base.paste(sc,(x,y))
            rgb=Image.blend(Image.new('RGB',(s.W,s.H)),base,sa) if sa<1 else base
        else: rgb=Image.new('RGB',(s.W,s.H))
        lay=Image.new('L',(s.W,s.H))
        for c in CAP:
            a=fade(t,c[0],c[1],0.25)
            if a>0: lay=Image.composite(Image.new('L',(s.W,s.H),int(255*a)),lay,s.caption(c)) if a<1 else Image.composite(Image.new('L',(s.W,s.H),255),lay,s.caption(c))
        # intro
        ia=fade(t,-1,3.8,0.5)
        if ia>0: lay=Image.composite(Image.new('L',(s.W,s.H),int(255*ia)),lay,s.intro())
        oa=fade(t,OUT,DUR+1,0.6)*ease((DUR-0.1-t)/0.6)
        if oa>0: lay=Image.composite(Image.new('L',(s.W,s.H),int(255*oa)),lay,s.outro(t))
        ta=fade(t,TOY[0],TOY[1],0.5)
        if ta>0: lay=Image.composite(Image.new('L',(s.W,s.H),int(255*ta)),lay,s.toy())
        if s.v and 3.4<t<SCR_END:
            lay=Image.composite(Image.new('L',(s.W,s.H),110),lay,s.foot_l())
        rgb.paste((255,255,255),(0,0),lay)
        return rgb
    def intro(s):
        if 'intro' in s.cache: return s.cache['intro']
        L=Image.new('L',(s.W,s.H)); d=ImageDraw.Draw(L)
        lw=900 if s.v else 1000; lg=logo.resize((lw,int(logo.height*lw/logo.width)),Image.LANCZOS)
        y=(s.H-lg.height)//2-120; L.paste(lg,((s.W-lw)//2,y))
        y+=lg.height+50
        for txt,f,c in [('Firmware de cumbia para el M-VAVE FM-1',font(44 if s.v else 48,'SemiBold'),255),('A cumbia firmware for the M-VAVE FM-1',font(34 if s.v else 36,'Regular'),150)]:
            w=d.textlength(txt,font=f); d.text(((s.W-w)//2,y),txt,font=f,fill=c); y+=70
        s.cache['intro']=L; return L
    def toy(s):
        if 'toy' in s.cache: return s.cache['toy']
        L=Image.new('L',(s.W,s.H)); d=ImageDraw.Draw(L)
        if s.v:
            es='En 2001, Toy Selectah\ncruzó la cumbia de Celso Piña\ncon el hip hop y el reggae:\n«Cumbia sobre el río»'
            en='In 2001, Toy Selectah crossed\nCelso Piña\'s cumbia with hip hop\nand reggae on “Cumbia sobre el río”'
            fe,fn,w=font(64),font(40,'Regular'),940
        else:
            es='En 2001, Toy Selectah cruzó\nla cumbia de Celso Piña con el hip hop\ny el reggae: «Cumbia sobre el río»'
            en='In 2001, Toy Selectah crossed Celso Piña\'s cumbia with hip hop\nand reggae on “Cumbia sobre el río”'
            fe,fn,w=font(68),font(40,'Regular'),1700
        while max(d.textlength(l,font=fe) for l in es.split('\n'))>w: fe=font(fe.size-4)
        h1=d.multiline_textbbox((0,0),es,font=fe,spacing=14)[3]; h2=d.multiline_textbbox((0,0),en,font=fn,spacing=10)[3]
        y=(s.H-h1-50-h2)//2
        for txt,f,c,sp,h in [(es,fe,255,14,h1),(en,fn,150,10,h2)]:
            for ln in txt.split('\n'):
                lw=d.textlength(ln,font=f); d.text(((s.W-lw)//2,y),ln,font=f,fill=c); y+=f.size+sp
            y+=50
        s.cache['toy']=L; return L
    def foot_l(s):
        if 'foot' in s.cache: return s.cache['foot']
        L=Image.new('L',(s.W,s.H)); d=ImageDraw.Draw(L); f=font(34,'Medium')
        txt='CUMBIA · M-VAVE FM-1'; w=d.textlength(txt,font=f); d.text(((s.W-w)//2,s.foot),txt,font=f,fill=255)
        s.cache['foot']=L; return L
    def outro(s,t):
        key='outro'
        if key not in s.cache:
            L=Image.new('L',(s.W,s.H)); d=ImageDraw.Draw(L)
            lines=[('Gratis · Free',font(56),255),('alandaf-lab.github.io/cumbia-fm1',font(40,'Medium'),255),
                   ('Homenaje a Celso Piña, Sonido Dueñez,\nlos cholombianos de Monterrey y toda la gente\nque ha hecho este movimiento',font(34,'Regular'),150),('Alan Daf',font(34,'Regular'),150)]
            if s.v:
                lw=700; lg=logo.resize((lw,int(logo.height*lw/logo.width)),Image.LANCZOS); L.paste(lg,((s.W-lw)//2,170))
                y=1180; 
            else:
                lw=640; lg=logo.resize((lw,int(logo.height*lw/logo.width)),Image.LANCZOS); L.paste(lg,(1100,110))
                y=560
            for txt,f,c in lines:
                if s.v:
                    for ln in txt.split('\n'):
                        w=d.textlength(ln,font=f); d.text(((s.W-w)//2,y),ln,font=f,fill=c); y+=f.size+14
                    y+=26
                else:
                    d.multiline_text((1100,y),txt,font=f,fill=c,spacing=10); y+=d.multiline_textbbox((0,0),txt,font=f)[3]+36
            s.cache[key]=L
        L=s.cache[key].copy()
        dh=520 if s.v else 640; dw=int(dh*384/599); ms=int((t-OUT)*1000)
        a=dancer_at(ms).resize((dw,dh),Image.LANCZOS); b=dancer_at(ms,chola).resize((dw,dh),Image.LANCZOS)
        if s.v: y=590; L.paste(a,(s.W//2-dw+30,y)); L.paste(b,(s.W//2-30,y))
        else: y=(s.H-dh)//2; L.paste(a,(80,y)); L.paste(b,(80+dw-40,y))
        return L
if __name__=='__main__':
    mode=sys.argv[1]; F=Fmt(1080,1920,True) if mode=='v' else Fmt(1920,1080,False)
    if sys.argv[2]=='test':
        for t in map(float,sys.argv[3:]): F.frame(int(t*FPS)).save(f'/tmp/{mode}_{t}.png')
    else:
        out=sys.argv[2]; tmp=out.replace('.mp4','.tmp.mp4')
        p=subprocess.Popen(['ffmpeg','-y','-loglevel','error','-f','rawvideo','-pix_fmt','rgb24','-s',f'{F.W}x{F.H}','-r','30','-i','-',
            '-i',f'{R}/build/video/toma.wav','-map','0:v','-map','1:a','-af',f'afade=t=out:st={DUR-3}:d=3','-t',str(DUR),
            '-c:v','libx264','-crf','18','-preset','veryfast','-pix_fmt','yuv420p','-c:a','aac','-b:a','192k','-movflags','+faststart',tmp],stdin=subprocess.PIPE)
        for n in range(DUR*FPS): p.stdin.write(F.frame(n).tobytes())
        p.stdin.close(); assert p.wait()==0
        import os; os.rename(tmp,out)

"""Peter's sounds, made from scratch: no show audio, no recordings, no voice cloning.

A small formant synthesiser (glottal pulse + aspiration through three resonators) speaks the short words
("Hey Peter!", "Hey Joe!", "Bye, Joe", "Alright!") and Zelda text-box style babble carries the rest of each line,
under subtitles. Joe is pitched up and squeezed into a phone's band. Timing comes from dialogue.py.
Run: python tools/peter/build_peter_audio.py   (needs numpy)
"""
from pathlib import Path
import sys, wave
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'assets/npcs/peter/audio';OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(Path(__file__).resolve().parent))
import dialogue as D

def resonate(x,f,bw,rate):
    # Time-varying two-pole resonator (Klatt), one sample at a time.
    y=np.zeros_like(x);y1=y2=0.0
    c=-np.exp(-2*np.pi*bw/rate);b=2*np.exp(-np.pi*bw/rate)*np.cos(2*np.pi*f/rate);a=1-b-c
    for n in range(len(x)):
        v=a[n]*x[n]+b[n]*y1+c[n]*y2;y2=y1;y1=v;y[n]=v
    return y

def speak(knots,pitch,rate,rng,breath=.04,nasal=0.0):
    """knots: (seconds from previous, F1, F2, F3, voicing, noise). pitch: (seconds, Hz) absolute knots."""
    flat=lambda k:[v for x in k for v in (x if isinstance(x,tuple) else (x,))]
    knots=[flat(k) for k in knots]
    t=np.cumsum([k[0] for k in knots]);n=int(t[-1]*rate)+1;s=np.arange(n)/rate
    f1,f2,f3,av,an=(np.interp(s,t,[k[i] for k in knots]) for i in range(1,6))
    f0=np.interp(s,[p[0] for p in pitch],[p[1] for p in pitch])*(1+.012*np.sin(2*np.pi*5.5*s))
    ph=np.cumsum(f0/rate)%1.0
    flow=np.where(ph<.6,.5-.5*np.cos(np.pi*ph/.6),np.cos(np.pi*(ph-.6)/.8)**2)
    pulse=np.diff(flow,prepend=0);pulse/=np.abs(pulse).max()+1e-9
    src=pulse*av*2.5+rng.standard_normal(n)*(an+breath*av)*.5
    v=resonate(src,f1,np.full(n,90.0),rate)+.0
    v=resonate(v,f2,np.full(n,120.0),rate)
    v=resonate(v,np.minimum(f3,rate*.45),np.full(n,170.0),rate)
    if nasal:v=v+nasal*resonate(src,np.full(n,270.0),np.full(n,100.0),rate)
    return v

# Vowel targets (F1, F2, F3) for babble and words.
V={'a':(730,1250,2500),'e':(560,1800,2500),'i':(290,2250,2950),'o':(520,900,2400),'u':(330,950,2300),
   'ae':(680,1700,2450),'uh':(600,1200,2450),'er':(470,1350,1650)}
def word(spec,rate,rng,f0,rise=1.0,nasal=0.0):
    """Tiny phoneme strings: h e i p ee t er j o b ai l w (enough for the scripted words)."""
    k=[(0,*V['uh'],0,0)];P=[]
    def add(dt,f,av,an):k.append((dt,*f,av,an))
    for ph in spec.split():
        if ph=='h':add(.01,V['e'],0,.35);add(.06,V['e'],.15,.25)
        elif ph=='ey':add(.03,V['e'],1,.02);add(.12,(450,2050,2600),1,0);add(.07,(380,2150,2650),.9,0)
        elif ph=='p':add(.02,(300,1800,2400),0,0);add(.06,(300,1800,2400),0,0);add(.005,(400,1200,2400),0,.7);add(.02,(300,2200,2900),0,.3)
        elif ph=='ee':add(.02,V['i'],.9,.03);add(.09,V['i'],.9,0)
        elif ph=='t':add(.02,(250,1900,2700),.35,0);add(.02,(300,1700,2200),.35,.1)
        elif ph=='er':add(.05,V['er'],1,0);add(.16,(480,1300,1600),.85,0);add(.07,(480,1300,1600),0,0)
        elif ph=='j':add(.03,(280,1800,2600),0,0);add(.03,(280,1800,2600),.2,.55);add(.05,(300,1900,2600),.4,.35)
        elif ph=='oh':add(.03,V['o'],1,0);add(.13,(450,850,2380),1,0);add(.12,(360,780,2300),.8,0);add(.06,(360,780,2300),0,0)
        elif ph=='b':add(.02,(250,800,2200),.25,0);add(.03,(250,800,2200),.25,0)
        elif ph=='ai':add(.03,(750,1250,2500),1,0);add(.13,(700,1350,2500),1,0);add(.09,(400,2000,2600),.9,0)
        elif ph=='aw':add(.03,(600,900,2500),1,0);add(.08,(600,900,2500),1,0)
        elif ph=='l':add(.04,(380,1000,2700),.7,0);add(.03,(380,1000,2700),.7,0)
        elif ph=='s':add(.02,(300,1800,6000),0,.0);add(.1,(300,1800,6000),0,.5);add(.02,(300,1800,6000),0,0)
        elif ph=='th':add(.02,(300,1500,5000),0,.15);add(.05,(300,1500,5000),0,.2)
        elif ph=='f':add(.02,(300,1200,4500),0,.2);add(.07,(300,1200,4500),0,.3)
        elif ph=='ih':add(.02,(400,1900,2550),1,0);add(.08,(420,1850,2550),1,0)
        elif ph=='uh':add(.03,V['uh'],1,0);add(.1,V['uh'],.9,0);add(.05,V['uh'],0,0)
        elif ph=='n':add(.02,(250,1400,2500),.55,0);add(.05,(250,1400,2500),.55,0)
        elif ph=='r':add(.03,(400,1100,1500),.8,0);add(.04,(420,1150,1600),.9,0)
        elif ph=='k':add(.02,(300,1800,2500),0,0);add(.05,(300,1800,2500),0,0);add(.005,(400,2000,3000),0,.6);add(.03,(400,2000,3000),0,.25)
        elif ph=='w':add(.03,(300,700,2200),.8,0);add(.04,(320,750,2200),.8,0)
        elif ph=='tt':add(.02,(300,1800,2600),0,0);add(.05,(300,1800,2600),0,0);add(.005,(400,2500,3500),0,.6);add(.04,(400,2500,3500),0,.25);add(.02,(400,2500,3500),0,0)
    total=sum(x[0] for x in k)
    pitch=[(0,f0),(total*.3,f0*1.18*rise),(total*.65,f0*1.1),(total,f0*.82)]
    return speak(k,pitch,rate,rng,nasal=nasal)

def babble(seconds,rate,rng,f0,question=False,excited=False,nasal=0.0):
    """Syllables of nonsense in the same voice, with a sentence melody: falling, or rising for a question."""
    k=[(0,*V['uh'],0,0)];t=0
    while t<seconds-.2:
        onset=rng.integers(4);vow=V[rng.choice(list(V))];d=float(rng.uniform(.09,.17))
        if onset==0:k+=[(.02,300,1500,2500,0,0),(.03,300,1500,2500,0,0),(.004,400,1800,2600,0,.6),(.02,*vow,.3,.2)]
        elif onset==1:k+=[(.02,*vow,0,.45),(.05,*vow,.1,.4)]
        elif onset==2:k+=[(.02,260,1000,2300,.5,0),(.04,270,1100,2300,.5,0)]
        k+=[(.025,*vow,1,0),(d,*vow,1,0),(.02,*vow,.6,0)]
        if rng.random()<.18:k+=[(.04,*vow,0,0),(float(rng.uniform(.04,.12)),*vow,0,0)]
        t=sum(x[0] for x in k)
    k+=[(.05,*V['uh'],0,0)];total=sum(x[0] for x in k)
    hi=f0*(1.25 if excited else 1.12)
    pitch=[(0,f0*1.05),(total*.15,hi),(total*.7,f0),(total,f0*(1.35 if question else .8))]
    return speak(k,pitch,rate,rng,nasal=nasal)

def laugh(rate,rng,f0=165,count=4):
    out=[]
    for i in range(count):
        g=f0*(1-.04*i)
        out.append(speak([(0,*V['e'],0,0),(.01,*V['e'],0,.45),(.05,*V['e'],.3,.25),(.02,*V['e'],1,0),(.09,(520,1700,2450),.9,0),(.03,(520,1700,2450),0,0)],[(0,g*1.1),(.2,g*.85)],rate,rng,breath=.12))
        out.append(np.zeros(int(.05*rate)))
    return np.concatenate(out)

def phone_band(x,rate):
    spec=np.fft.rfft(x);fr=np.fft.rfftfreq(len(x),1/rate)
    spec*=1/(1+(350/(fr+1))**4)/(1+(fr/3100)**6);y=np.fft.irfft(spec,len(x))
    return np.tanh(y*2.0/(np.abs(y).max()+1e-9))*.8

def norm(x,peak=.7):
    x=x-np.mean(x);return x/(np.abs(x).max()+1e-9)*peak
def fade(x,rate,ms=8):
    n=min(len(x)//2,int(rate*ms/1000));e=np.ones(len(x));e[:n]=np.linspace(0,1,n);e[len(x)-n:]=np.linspace(1,0,n);return x*e

PETER_F0=108;JOE_F0=128
def build_call(rate):
    rng=np.random.default_rng(1999)
    track=np.zeros(int(D.DURATION*rate))
    def place(at,x,gain=1.0):
        i=int(at*rate);x=x[:len(track)-i];track[i:i+len(x)]+=x*gain
    # Ring ring: a bright cartoon trill, two bursts.
    for start in (0.0,.85):
        s=np.arange(int(.65*rate))/rate;trill=np.where((s*24)%1<.5,1250,1560)
        ring=np.sin(2*np.pi*np.cumsum(trill)/rate)*np.minimum(1,np.minimum(s/.01,(.65-s)/.04))
        place(start,ring*.33)
    click=np.random.default_rng(3).standard_normal(int(.03*rate))*np.exp(-np.arange(int(.03*rate))/(rate*.004))
    place(D.RAISE[1]-.05,click*.4);place(D.LOWER[0],click*.4)
    beep=np.sin(2*np.pi*620*np.arange(int(.18*rate))/rate)
    place(D.LOWER[0]+.12,fade(beep,rate)*.18)
    for i,(speaker,a,b,text) in enumerate(D.LINES):
        f0=JOE_F0 if speaker==0 else PETER_F0;nasal=.0 if speaker==0 else .35
        parts=[]
        if i==len(D.LINES)-1:parts+=[laugh(rate,rng),np.zeros(int(.12*rate))]
        if i in D.SPOKEN:
            spec={'hey peter':'h ey p ee t er','hey joe':'h ey j oh','bye joe':'b ai j oh','cynthia':'s ih n th ee uh'}[D.SPOKEN[i]]
            parts+=[word(spec,rate,rng,f0*1.1,rise=1.1,nasal=nasal),np.zeros(int(.1*rate))]
        used=sum(len(p) for p in parts)/rate
        if b-a-used>.6:parts.append(babble(b-a-used-.05,rate,rng,f0,question=text.rstrip().endswith('?'),excited='!' in text,nasal=nasal))
        line=norm(np.concatenate(parts),.75)[:int((b-a)*rate)]
        line=fade(line,rate,30)
        place(a,phone_band(line,rate) if speaker==0 else line,.8 if speaker==0 else 1.0)
    return norm(track,.8)

def cue(name,rate):
    rng=np.random.default_rng({'greet':1,'hire':2,'attack':3,'hurt':4,'belly':5,'chicken':6,'kick':7,'cheer':8}[name])
    if name=='greet':x=laugh(rate,rng,170,3)
    elif name=='cheer':x=word('f r ee k ih n s w ee tt',rate,rng,135,rise=1.25,nasal=.3)
    elif name=='belly':
        # A cartoon "boing" of the gut: a bouncing pitch drop on a round tone, with a grunt.
        n=int(.45*rate);s=np.arange(n)/rate
        f=180+260*np.exp(-s*9)*(1+.35*np.sin(2*np.pi*14*s));boing=np.sin(2*np.pi*np.cumsum(f)/rate)*np.exp(-s*5)
        g=speak([(0,500,1000,2400,0,0),(.02,500,1000,2400,1,.1),(.12,560,1100,2400,1,0),(.05,560,1100,2400,0,0)],[(0,140),(.19,100)],rate,rng,nasal=.3)
        x=np.zeros(n);x+=norm(boing,.8);x[:len(g)]+=norm(g,.5)
    elif name=='chicken':
        # Flurry: four quick barked grunts.
        x=np.concatenate([np.concatenate([norm(speak([(0,550,1150,2400,0,0),(.01,550,1150,2400,1,.15),(.07,600,1200,2450,1,.05),(.03,600,1200,2450,0,0)],[(0,150+12*i),(.11,115)],rate,rng,breath=.2,nasal=.3),.7),np.zeros(int(.07*rate))]) for i in range(4)])
    elif name=='kick':
        g=word('h uh',rate,rng,150,rise=1.2,nasal=.2)
        n=int(.3*rate);s=np.arange(n)/rate;whoosh=resonate(rng.standard_normal(n)*np.sin(np.pi*s/.3)**2,500+2000*s/.3,np.full(n,500.0),rate)
        x=np.zeros(max(n,len(g))+int(.05*rate));x[:len(g)]+=norm(g,.8);x[int(.05*rate):int(.05*rate)+n]+=norm(whoosh,.45)
    elif name=='hire':x=np.concatenate([word('aw l ai tt',rate,rng,120,rise=1.2,nasal=.3),np.zeros(int(.06*rate)),laugh(rate,rng,175,2)])
    elif name=='attack':
        g=speak([(0,450,1100,2400,0,0),(.02,450,1100,2400,1,.1),(.1,520,1150,2400,1,.05),(.06,520,1150,2400,0,0)],[(0,125),(.18,95)],rate,rng,breath=.15,nasal=.3)
        n=int(.28*rate);s=np.arange(n)/rate;whoosh=resonate(rng.standard_normal(n)*np.sin(np.pi*s/.28)**2,700+2500*s/.28,np.full(n,600.0),rate)
        x=np.zeros(n+len(g));x[:len(g)]+=norm(g,.8);x[:n]+=norm(whoosh,.35)
    else:
        # "Ssssss... ahh!" - a hiss through the teeth, then a pained yelp.
        n=int(.5*rate);s=np.arange(n)/rate
        hiss=resonate(rng.standard_normal(n),np.full(n,4800.0),np.full(n,1400.0),rate)*np.minimum(1,s/.15)*np.minimum(1,(.5-s)/.05)
        ah=speak([(0,*V['a'],0,0),(.02,*V['a'],1,.05),(.3,(700,1150,2550),1,.03),(.1,(650,1100,2500),0,0)],[(0,230),(.15,215),(.42,140)],rate,rng,breath=.1,nasal=.2)
        x=np.concatenate([norm(hiss,.4),np.zeros(int(.08*rate)),norm(ah,.8)])
    return fade(norm(x,.75),rate)

def write_wav(path,pcm,rate):
    with wave.open(str(path),'wb') as w:w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(pcm.astype('<i2').tobytes())

def main():
    clips=[]
    for name in ('greet','hire','attack','hurt','belly','chicken','kick','cheer'):
        pcm=(cue(name,22050)*32767).astype(np.int16);clips.append((name,pcm,22050))
    call=(build_call(11025)*32767).astype(np.int16);clips.append(('call',call,11025))
    for name,pcm,rate in clips:write_wav(OUT/f'peter_{name}.wav',pcm,rate)
    L=['#pragma once','// Generated by tools/peter/build_peter_audio.py: original synthesised voice and phone-call track. Regenerate; do not edit.',
       '#include <cstdint>','namespace royale::peter_snd {']
    for name,pcm,rate in clips:
        L.append(f'inline constexpr int16_t k_{name}[]={{')
        L+=[','.join(map(str,pcm[i:i+40].tolist()))+',' for i in range(0,len(pcm),40)]
        L.append('};')
    L+=['struct Clip { const int16_t* data; unsigned count; int rate; };','enum { kGreet,kHire,kAttack,kHurt,kBelly,kChicken,kKick,kCheer,kCall,kClipCount };',
        'inline constexpr Clip kClips[kClipCount]={']+[f'  {{k_{n},{len(p)},{r}}},' for n,p,r in clips]+['};',
        '// The phone call: Joe (0) on the line, Peter (1). Seconds from the first ring, the same table drives his animation.',
        'struct Line { int speaker; float start,end; const char* text; };',
        f'inline constexpr float kCallDuration={D.DURATION}f;','inline constexpr Line kLines[]={']
    for sp,a,b,text in D.LINES:L.append(f'  {{{sp},{a}f,{b}f,"{text}"}},')
    L+=['};','// Quip bubbles over his head; order matches tools/peter/dialogue.py QUIPS.',
        'enum { kQuipHaymaker,kQuipBelly,kQuipChicken,kQuipHurt,kQuipLaugh,kQuipSweet,kQuipCynthia,kQuipRoadhouse,kQuipCount };',
        'inline constexpr const char* kQuips[kQuipCount]={'+','.join('"'+q+'"' for q in D.QUIPS)+'};','}']
    (ROOT/'shared/peter_sounds.h').write_text('\n'.join(L)+'\n')
    (OUT/'PROVENANCE.md').write_text('All Peter audio is original and synthesised by tools/peter/build_peter_audio.py (formant synthesis + babble, numpy only). '
        'No Family Guy audio, no recordings of any actor, no voice cloning or imitation of a real person\'s voice. '
        'Cues: greet (laugh), hire ("Alright!" + laugh), attack (grunt + swing), hurt (hiss + yelp), belly (boing + grunt), chicken (grunt flurry), kick ("Hup!" + whoosh), cheer ("Freakin\' sweet!"), call (ring, "Hey Peter!", conversation babble timed to dialogue.py subtitles, hang-up). '
        'Reproduce: python tools/peter/build_peter_audio.py.\n')
    print('Peter audio:',', '.join(f'{n} {len(p)/r:.2f}s' for n,p,r in clips))
if __name__=='__main__':main()

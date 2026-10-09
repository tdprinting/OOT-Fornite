"""Original deterministic PCM sound effects; no sampled recordings."""
from pathlib import Path
import math, random, struct, wave
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'assets/bosses/morrow/audio';out.mkdir(parents=True,exist_ok=True)
rate=44100
names=['aggro','sweep','impact','toll','exposed','defeat']
durations=[1,.6,.5,.7,.8,1.8]
clips=[]
for cue,duration in enumerate(durations):
    rng=random.Random(440+cue);samples=[]
    for i in range(round(duration*rate)):
        t=i/rate;fade=min(1,t/.008,(duration-t)/.035)
        bell=lambda f:sum(a*math.sin(2*math.pi*f*m*t)*math.exp(-t*d) for m,a,d in [(1,.65,3),(2.64,.24,6),(4.6,.12,10)])
        noise=rng.uniform(-1,1)
        if cue==0:v=bell(220)+.08*noise*math.exp(-t*30)
        elif cue==1:v=.30*noise*math.sin(math.pi*t/duration)**2+.15*math.sin(2*math.pi*(800*t-400*t*t))*math.exp(-t*4)
        elif cue==2:v=.6*math.sin(2*math.pi*90*t)*math.exp(-t*12)+.25*noise*math.exp(-t*18)
        elif cue==3:v=bell(440)
        elif cue==4:v=.6*math.sin(2*math.pi*(520*t-180*t*t))*math.exp(-t*5)+.1*noise*math.exp(-t*25)
        else:
            v=0
            for start,f in [(0,440),(.45,330),(.9,220)]:
                if t>=start:
                    u=t-start;v+=.4*math.sin(2*math.pi*f*u)*math.exp(-u*4)
            v+=.08*noise*math.sin(math.pi*t/duration)
        samples.append(v*max(0,fade))
    gain=.85*32767/max(abs(x) for x in samples)
    pcm=[round(x*gain) for x in samples];clips.append(pcm)
    with wave.open(str(out/f'morrow_{names[cue]}.wav'),'wb') as w:
        w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(struct.pack('<'+'h'*len(pcm),*pcm))
lines=['#pragma once','#include <cstdint>','namespace royale::morrow_snd {','inline constexpr int kRate=44100;']
for n,pcm in zip(names,clips):
    lines.append(f'inline constexpr int16_t k_{n}[] = {{')
    lines.extend(','.join(map(str,pcm[i:i+32]))+',' for i in range(0,len(pcm),32));lines.append('};')
lines+=['struct Clip { const int16_t* data; unsigned count; };','inline constexpr Clip kClips[]={']
lines.extend(f'{{k_{n},{len(pcm)}}},' for n,pcm in zip(names,clips));lines+=['};','inline constexpr int kClipCount=6;','}']
(ROOT/'shared/morrow_sounds.h').write_text('\n'.join(lines)+'\n')
(out/'PROVENANCE.md').write_text('Original synthesized effects created for this project. No external samples or franchise recordings. Reproduce with python tools/bosses/build_morrow_audio.py. Same repository license applies.\n')
print('Created six original mono 44.1 kHz PCM16 cues.')

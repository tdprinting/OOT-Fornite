"""Original cartoon Bokoblin Foley and vocal synthesis, 16 kHz mono PCM.
Uses deterministic oscillators/noise, with no game recordings or voice samples.
"""
from pathlib import Path
import math, random, struct, wave
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'assets/bokoblin/sounds';RATE=16000
CLIPS=[('alert',.55),('swing',.32),('throw',.35),('stumble',.45),('laugh',.8),('hurt',.3),('flee',.7),('death',.65)]

def build(name,seconds):
    rng=random.Random(name);out=[];phase=0
    for i in range(round(seconds*RATE)):
        t=i/RATE;u=t/seconds
        env=min(1,t/.015)*min(1,(seconds-t)/.06)
        if name in ('swing','throw'):
            tone=(rng.random()*2-1)*(.5+.5*math.sin(math.pi*u))
            if name=='throw':tone+=.25*math.sin(2*math.pi*(400-180*u)*t)
            env*=math.sin(math.pi*u)**2
        elif name=='stumble':
            tone=math.sin(2*math.pi*(180-110*u)*t)*math.exp(-5*u)+.35*(rng.random()*2-1)*math.exp(-18*u)
        else:
            pitch={'alert':260+150*math.sin(u*math.pi),'laugh':210+70*math.sin(u*math.pi*6),'hurt':460-240*u,
                   'flee':330+100*math.sin(u*math.pi*5),'death':260-190*u}[name]
            phase+=2*math.pi*pitch/RATE
            tone=math.sin(phase)+.38*math.sin(phase*2)+.18*math.sin(phase*3)
            tone+=.12*(rng.random()*2-1)
            if name=='laugh':env*=max(0,math.sin(u*math.pi*6))**.6
            elif name in ('alert','flee'):env*=.6+.4*math.sin(u*math.pi*5)**2
            tone=math.tanh(tone*1.8)*.75
        out.append(round(max(-1,min(1,tone*env*.55))*32767))
    return out

def main():
    OUT.mkdir(parents=True,exist_ok=True);lines=['#pragma once','#include <cstdint>','namespace royale::bokoblin_snd {',f'constexpr int kRate = {RATE};']
    for name,duration in CLIPS:
        data=build(name,duration)
        with wave.open(str(OUT/(name+'.wav')),'wb') as w:
            w.setparams((1,2,RATE,0,'NONE','not compressed'));w.writeframes(struct.pack('<'+'h'*len(data),*data))
        lines.append(f'inline constexpr int16_t k{name.capitalize()}[] = {{')
        lines.extend(','.join(map(str,data[i:i+24]))+',' for i in range(0,len(data),24));lines.append('};')
    lines+=['struct Clip { const int16_t* data; int count; };','inline constexpr Clip kClips[] = {']
    lines += ['{k%s, sizeof(k%s)/sizeof(int16_t)},'%(n.capitalize(),n.capitalize()) for n,_ in CLIPS]
    lines+=['};','constexpr int kClipCount = sizeof(kClips)/sizeof(kClips[0]);','}']
    (ROOT/'shared/bokoblin_sounds.h').write_text('\n'.join(lines)+'\n')
if __name__=='__main__':main()

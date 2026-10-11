"""Original deterministic scout/hero cues, no Fortnite recordings or voice imitation."""
from pathlib import Path
import math, random, struct, wave
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'assets/npcs/jones/audio';OUT.mkdir(parents=True,exist_ok=True)
rate=22050;names=['greet','hire','attack','hurt','burst','carbine','scoped'];durations=[.65,.7,.26,.32,.34,.18,.42];clips=[]
for cue,duration in enumerate(durations):
    rng=random.Random(610+cue);samples=[]
    for i in range(round(duration*rate)):
        t=i/rate;u=t/duration;noise=rng.uniform(-1,1)
        fade=max(0,min(1,t/.006,(duration-t)/.025))
        if cue in (0,1):
            # A brief bright UI flourish and a fabric/gear rustle, familiar to adventure shooters.
            seq=[440,554,659] if cue==0 else [523,659,784]
            note=min(2,int(u*3));age=t-note*duration/3
            v=.32*math.sin(2*math.pi*seq[note]*age)*math.exp(-age*12)+.04*noise*math.exp(-t*6)
        elif cue==3:
            # Low nonverbal impact/grunt-like synthesized cue; no actor's voice is sampled.
            v=.4*math.sin(2*math.pi*(125*t-60*t*t))*math.exp(-t*12)+.12*noise*math.exp(-t*15)
        else:
            # Distinct rifle reports: sharp attack noise, low pressure thump, metallic tail.
            freq={2:95,4:120,5:165,6:62}[cue]
            starts=[0,.065,.13] if cue==4 else [0]
            v=0
            for start in starts:
                if t>=start:
                    age=t-start
                    v+=.45*noise*math.exp(-age*55)+.50*math.sin(2*math.pi*freq*age)*math.exp(-age*22)+.08*math.sin(2*math.pi*1100*age)*math.exp(-age*16)
        samples.append(v*fade)
    peak=max(abs(v) for v in samples);pcm=[round(v/peak*.65*32767) for v in samples];clips.append(pcm)
    with wave.open(str(OUT/f'jones_{names[cue]}.wav'),'wb') as w:
        w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(struct.pack('<'+'h'*len(pcm),*pcm))
lines=['#pragma once','#include <cstdint>','namespace royale::jones_snd {',f'inline constexpr int kRate={rate};']
for n,pcm in zip(names,clips):
    lines+=[f'inline constexpr int16_t k_{n}[]={{']
    lines += [','.join(map(str,pcm[i:i+32]))+',' for i in range(0,len(pcm),32)]
    lines+=['};']
lines+=['struct Clip { const int16_t* data; unsigned count; };','inline constexpr Clip kClips[]={']
lines += [f'{{k_{n},{len(pcm)}}},' for n,pcm in zip(names,clips)]
lines += ['};','inline constexpr int kClipCount=7;','}']
(ROOT/'shared/jones_sounds.h').write_text('\n'.join(lines)+'\n')
(OUT/'PROVENANCE.md').write_text('Seven original synthesized cues: scout greeting, hire confirmation, four distinct rifle reports (assault, burst, carbine, scoped), and impact reaction. Similar adventure/shooter sound direction; not Fortnite recordings. No sampled external audio or voice imitation. Reproduce: python tools/jones/build_jones_audio.py.\n')
print('Exported seven original Jones cues, PCM16 mono 22050 Hz')

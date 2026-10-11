#pragma once
// Master Chief's baked rigid clips (tools/chief/build_chief.py writes chief_model.h); same sampling as jones_animation.h.
#include "chief_model.h"
#include <algorithm>
#include <cmath>
namespace royale::chief_model {
inline Rotation Sample(int clip, unsigned part, float seconds) {
    clip=std::clamp(clip,static_cast<int>(kIdle),static_cast<int>(kDeath));
    part=std::min(part,kPartCount-1);
    const auto& c=kClips[clip];
    float u=std::max(0.0f,seconds)/c.duration;
    u=c.loop?u-std::floor(u):std::min(u,1.0f);
    const float f=u*(c.frames-1);const unsigned a=static_cast<unsigned>(f),b=std::min(a+1,c.frames-1);
    const auto& x=kRotations[c.first+a*kPartCount+part];const auto& y=kRotations[c.first+b*kPartCount+part];
    const float t=f-a;
    return {x.x+(y.x-x.x)*t,x.y+(y.y-x.y)*t,x.z+(y.z-x.z)*t};
}
}

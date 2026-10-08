#pragma once
#include "sandbox_terrain.h"
#include "storm.h"

namespace royale::arena {
inline constexpr Vec2 kSpawn{0,-900};
inline constexpr Vec2 kBoss{0,0};
inline constexpr Vec2 kArmory{0,-2400};
inline sandbox::Data Build(bool major) {
    sandbox::Data d;
    const int verts=fortnite_data::kCells+1;
    const float half=major?4600.0f:3600.0f;
    d.heights.resize(verts*verts);
    for(int z=0;z<verts;z++) for(int x=0;x<verts;x++) {
        const float wx=-fortnite_data::kHalfX+x*2*fortnite_data::kHalfX/fortnite_data::kCells;
        const float wz=-fortnite_data::kHalfZ+z*2*fortnite_data::kHalfZ/fortnite_data::kCells;
        // A perfectly flat fight floor and a steep, solid enclosing rim.
        const float r=(std::max)(std::fabs(wx),std::fabs(wz));
        d.heights[z*verts+x]=static_cast<int16_t>(std::clamp((r-half)/200.0f,0.0f,1.0f)*1200.0f);
    }
    const int fine=fortnite_data::kCells*fortnite_data::kSub+1;
    d.colours.resize(fine*fine*3);
    for(int z=0;z<fine;z++) for(int x=0;x<fine;x++) {
        const float wx=-fortnite_data::kHalfX+x*2*fortnite_data::kHalfX/(fine-1);
        const float wz=-fortnite_data::kHalfZ+z*2*fortnite_data::kHalfZ/(fine-1);
        const bool line=std::fmod(std::fabs(wx),500.0f)<14 || std::fmod(std::fabs(wz),500.0f)<14;
        const bool ring=std::fabs(std::hypot(wx,wz)-1000.0f)<22;
        const bool rim=(std::max)(std::fabs(wx),std::fabs(wz))>half;
        auto* c=&d.colours[(z*fine+x)*3];
        c[0]=rim?80:ring?190:line?135:major?95:115;
        c[1]=rim?82:ring?165:line?145:major?105:128;
        c[2]=rim?88:ring?80:line?150:major?120:112;
    }
    d.cover.assign(sandbox::kCoverSquares*sandbox::kCoverSquares,'4');
    return d;
}
inline const sandbox::Data& Terrain(bool major) {
    static const sandbox::Data mini=Build(false), main=Build(true);
    return major?main:mini;
}
}

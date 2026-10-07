#pragma once
#include "convergence_data.h"
#include "poi.h"
#include <algorithm>
#include <utility>
#include <vector>
namespace royale {
inline float ConvergenceTerrainHeight(Vec2 p) {
    const float x = std::clamp((p.x+7412.53f)/14825.06f*64.0f,0.0f,63.999f);
    const float z = std::clamp((p.z+7705.72f)/15411.44f*64.0f,0.0f,63.999f);
    const int i=static_cast<int>(x),j=static_cast<int>(z); const float u=x-i,v=z-j;
    const auto* h=convergence::kHeights;
    const float a=h[j*65+i],b=h[j*65+i+1],c=h[(j+1)*65+i],d=h[(j+1)*65+i+1];
    return u>=v ? a+u*(b-a)+v*(d-b) : a+u*(d-c)+v*(c-a);
}
inline float ConvergenceGroundHeight(Vec2 p) {
    float y=ConvergenceTerrainHeight(p);
    for (const auto& b:convergence::kBuildings)
        if (std::fabs(p.x-b.x)<=b.halfWidth && std::fabs(p.z-b.z)<=b.halfDepth) y=std::max(y,b.floorY);
    for (const auto& patch:convergence::kFloorPatches)
        if (Distance(p,{patch.x,patch.z})<=patch.radius) y=std::max(y,patch.y);
    return y;
}
inline std::vector<size_t> ConvergenceCollidersNear(float x,float y,float z,size_t limit) {
    std::vector<std::pair<float,size_t>> near;
    y+=40.0f; // prioritize the player's body rather than overhead roof ornaments
    for(size_t i=0;i<sizeof(convergence::kPropColliders)/sizeof(convergence::kPropColliders[0]);++i) {
        const auto& c=convergence::kPropColliders[i];
        const float dx=std::max({c.x0-x,0.0f,x-c.x1}),dy=std::max({c.y0-y,0.0f,y-c.y1}),dz=std::max({c.z0-z,0.0f,z-c.z1});
        const float d=dx*dx+dy*dy+dz*dz;
        if(d<=900.0f*900.0f) near.push_back({d,i});
    }
    std::sort(near.begin(),near.end());
    std::vector<size_t> result;
    for(size_t i=0;i<std::min(limit,near.size());++i) result.push_back(near[i].second);
    return result;
}
inline bool ConvergenceDryGround(Vec2 p) {
    if (std::fabs(p.x)>=7412.53f || std::fabs(p.z)>=7705.72f || ConvergenceTerrainHeight(p)<-212) return false;
    const float sx=(ConvergenceTerrainHeight({p.x+15,p.z})-ConvergenceTerrainHeight({p.x-15,p.z}))/30;
    const float sz=(ConvergenceTerrainHeight({p.x,p.z+15})-ConvergenceTerrainHeight({p.x,p.z-15}))/30;
    return 1/std::sqrt(1+sx*sx+sz*sz)>=.8f;
}
inline bool ConvergenceObstacleAt(Vec2 p, float margin = 22.0f) {
    for (const auto& b : convergence::kObstacles)
        if (p.x > b.x0-margin && p.x < b.x1+margin && p.z > b.z0-margin && p.z < b.z1+margin) return true;
    return false;
}
inline PoiLayout GenerateConvergenceLayout(const PlacementFn& valid = nullptr) {
    PoiLayout out;
    int i=0;
    for (const auto& r : convergence::kRegions) {
        out.pois.push_back({static_cast<uint8_t>(kConvergenceMapIndex*kNamesPerMap+i++),{r.x,r.z},1100.0f});
        if (r.boss >= 0) out.bossSpots.push_back({r.x,r.z});
    }
    out.pois.push_back({static_cast<uint8_t>(kConvergenceMapIndex*kNamesPerMap+9),{0,-2300},900});
    for (const auto& p : convergence::kLootSites) {
        Vec2 at{p.x,p.z};
        if (!valid || valid(at)) out.lootSpots.push_back(at);
    }
    return out;
}
inline BossKind ConvergenceBossAt(Vec2 at) {
    for (const auto& r : convergence::kRegions)
        if (r.boss >= 0 && Distance(at,{r.x,r.z}) < 1.0f) return static_cast<BossKind>(r.boss);
    return BossKind::Stone;
}
} // namespace royale

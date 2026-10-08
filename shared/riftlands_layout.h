#pragma once
#include "riftlands_data.h"
#include "poi.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
namespace royale {
// Hyrule Riftlands (tools/maps/riftlands): the ground and every place on it are baked by tools/maps/riftlands/export.py. Nothing is scattered over it.
inline float RiftlandsTerrainHeight(Vec2 p) {
    const float x = std::clamp((p.x+7412.53f)/14825.06f*64.0f,0.0f,63.999f);
    const float z = std::clamp((p.z+7705.72f)/15411.44f*64.0f,0.0f,63.999f);
    const int i=static_cast<int>(x),j=static_cast<int>(z); const float u=x-i,v=z-j;
    const auto* h=riftlands::kHeights;
    const float a=h[j*65+i],b=h[j*65+i+1],c=h[(j+1)*65+i],d=h[(j+1)*65+i+1];
    return u>=v ? a+u*(b-a)+v*(d-b) : a+u*(d-c)+v*(c-a);
}
inline float RiftlandsWaterY(Vec2 p) {
    for(const auto& b:riftlands::kPools) if(p.x>=b.x0 && p.x<=b.x1 && p.z>=b.z0 && p.z<=b.z1) return b.y;
    return -227.0f;
}
// Inside a building's footprint (turned by its yaw)?
inline bool RiftlandsInBuilding(const riftlands::Building& b, Vec2 p, float grow = 0.0f) {
    const float dx=p.x-b.x, dz=p.z-b.z, c=std::cos(b.yaw), s=std::sin(b.yaw);
    const float lx=dx*c+dz*s, lz=-dx*s+dz*c;
    return std::fabs(lx)<=b.halfWidth+grow && std::fabs(lz)<=b.halfDepth+grow;
}
// The height of a walkway (a deck, bridge, boardwalk or log ramp) at p, or false when p is not on one.
inline bool RiftlandsWalkwayAt(const riftlands::Walkway& w, Vec2 p, float* y) {
    const float dx=w.x1-w.x0, dz=w.z1-w.z0, len2=dx*dx+dz*dz;
    if (len2<1.0f) return false;
    const float t=((p.x-w.x0)*dx+(p.z-w.z0)*dz)/len2;
    if (t<0.0f || t>1.0f) return false;
    const float len=std::sqrt(len2), off=std::fabs((p.x-w.x0)*dz-(p.z-w.z0)*dx)/len;
    if (off>w.halfWidth) return false;
    if (y) *y=w.y0+(w.y1-w.y0)*t;
    return true;
}
// The floor under p: the terrain, the ground floor of a building whose footprint holds p, or a deck or bridge over it.
inline float RiftlandsGroundHeight(Vec2 p) {
    float y=RiftlandsTerrainHeight(p);
    for (const auto& b:riftlands::kBuildings)
        if (RiftlandsInBuilding(b,p)) y=std::max(y,b.floorY);
    for (const auto& w:riftlands::kWalkways) {
        float wy;
        if (RiftlandsWalkwayAt(w,p,&wy) && wy>y-40.0f) y=std::max(y,wy);   // a bridge far above the river is the floor there; a log ramp beginning in the ground blends in
    }
    return y;
}
// A loot site's own height when p is at it (upper floors, rooftops and tree decks hold chests too); false when p is no site.
inline bool RiftlandsLootHeightAt(Vec2 p, float* y) {
    for (const auto& s:riftlands::kLootSites)
        if (std::fabs(p.x-s.x)<2.0f && std::fabs(p.z-s.z)<2.0f) { if (y) *y=s.y; return true; }
    return false;
}
inline std::vector<size_t> RiftlandsCollidersNear(float x,float y,float z,size_t limit) {
    std::vector<std::pair<float,size_t>> near;
    y+=40.0f;
    for(size_t i=0;i<sizeof(riftlands::kPropColliders)/sizeof(riftlands::kPropColliders[0]);++i) {
        const auto& c=riftlands::kPropColliders[i];
        const float dx=std::max({c.x0-x,0.0f,x-c.x1}),dy=std::max({c.y0-y,0.0f,y-c.y1}),dz=std::max({c.z0-z,0.0f,z-c.z1});
        const float d=dx*dx+dy*dy+dz*dz;
        if(d<=900.0f*900.0f) near.push_back({d,i});
    }
    std::sort(near.begin(),near.end());
    std::vector<size_t> result;
    for(size_t i=0;i<std::min(limit,near.size());++i) result.push_back(near[i].second);
    return result;
}
// Somewhere to stand: above the water and not too steep. A floor, deck or bridge is flat by construction, and the step down at its edge is a
// matter for the nav grid's step limits, so only bare terrain gets the slope test.
inline bool RiftlandsOnStructure(Vec2 p, float grow = 0.0f) {
    for (const auto& b:riftlands::kBuildings) if (RiftlandsInBuilding(b,p,grow)) return true;
    for (const auto& w:riftlands::kWalkways) { float wy; if (RiftlandsWalkwayAt(w,p,&wy)) return true; }
    return false;
}
inline bool RiftlandsDryGround(Vec2 p) {
    if (std::fabs(p.x)>=7412.53f || std::fabs(p.z)>=7705.72f || RiftlandsGroundHeight(p)<RiftlandsWaterY(p)+15) return false;
    if (RiftlandsOnStructure(p,30.0f)) return true;
    const float sx=(RiftlandsTerrainHeight({p.x+15,p.z})-RiftlandsTerrainHeight({p.x-15,p.z}))/30;
    const float sz=(RiftlandsTerrainHeight({p.x,p.z+15})-RiftlandsTerrainHeight({p.x,p.z-15}))/30;
    return 1/std::sqrt(1+sx*sx+sz*sz)>=.8f;
}
inline bool RiftlandsObstacleAt(Vec2 p, float margin = 22.0f) {
    for (const auto& b : riftlands::kObstacles)
        if (p.x > b.x0-margin && p.x < b.x1+margin && p.z > b.z0-margin && p.z < b.z1+margin) return true;
    return false;
}
// Shared host/server placement contract. Upper-room sites carry their own floor;
// ordinary points use authored deck/floor heights before testing bare terrain.
inline bool RiftlandsPlacementValid(Vec2 p) {
    float y;
    if(RiftlandsLootHeightAt(p,&y)) return y>RiftlandsWaterY(p)+15.0f;
    return RiftlandsDryGround(p) && !RiftlandsObstacleAt(p,0.0f);
}
inline PoiLayout GenerateRiftlandsLayout(const PlacementFn& valid = nullptr) {
    PoiLayout out;
    int i=0;
    for (const auto& r : riftlands::kRegions) {
        out.pois.push_back({static_cast<uint8_t>(PoiNameBase(kRiftlandsMapIndex)+i++),{r.x,r.z},900.0f});

    }
    for(const auto& b:riftlands::kBossSites) out.bossSpots.push_back({b.x,b.z});
    for (const auto& p : riftlands::kLootSites) {
        Vec2 at{p.x,p.z};
        if (!valid || valid(at)) out.lootSpots.push_back(at);
    }
    return out;
}
inline BossKind RiftlandsBossAt(Vec2 at) {
    for (const auto& r : riftlands::kBossSites)
        if (r.boss >= 0 && Distance(at,{r.x,r.z}) < 1.0f) return static_cast<BossKind>(r.boss);
    return BossKind::Stone;
}
} // namespace royale

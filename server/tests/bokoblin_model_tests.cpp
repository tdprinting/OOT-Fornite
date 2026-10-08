#include "../../shared/bokoblin_anim.h"
#include "../../shared/bokoblin_sounds.h"
#include "../../shared/bokoblin.h"
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace royale::bokoblin;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* what) { if (!ok) { std::printf("FAIL %s\n",what);++failures; } };
    check(kTriCount<1200 && kTriCount>300,"N64 triangle budget");
    check(kClothW==128 && kClothH==128 && kSkinW==128 && kSkinH==128 && kFaceW==128 && kFaceH==128,"128x128 atlases");
    int triangles=0;
    for (const auto& b:kBatches) {
        check(b.vertCount<=32 && b.firstVert+b.vertCount<=kVertCount && b.firstTri+b.triCount<=kTriCount,"vertex batch bounds");
        triangles+=b.triCount;
        for (int t=b.firstTri;t<b.firstTri+b.triCount;t++) for (auto v:kTris[t]) check(v<b.vertCount,"triangle indices");
    }
    check(triangles==kTriCount,"all triangles exported");
    for (const auto& v:kVerts) check(v.b0<kBoneCount && v.b1<kBoneCount && v.s>=0 && v.s<=128*32 && v.t>=0 && v.t<=128*32,"skin and UV bounds");
    check(kClipCount==static_cast<int>(royale::BokoMode::Count),"clip for every gameplay mode");
    for (int c=0;c<kClipCount;c++) {
        const auto& info=kClips[c];
        check(info.frames>1 && info.firstFrame+info.frames<=kFrameCount,"clip bounds");
        bool moving=false;Pose first;SampleClip(c,0,first);
        for (int f=0;f<info.frames;f++) {
            Pose p;SampleClip(c,f/info.fps,p);
            for (int b=0;b<kBoneCount;b++) for (int axis=0;axis<4;axis++) moving|=std::fabs(p.bone[b].q[axis]-first.bone[b].q[axis])>.001f;
            for (const auto& v:kVerts) {
                float pos[3],n[3];SkinVertex(p,v,pos,n);
                for (int axis=0;axis<3;axis++) check(std::isfinite(pos[axis]) && std::fabs(pos[axis])<220 && std::isfinite(n[axis]),"finite bounded posed model");
            }
        }
        check(moving,"nonempty animation");
    }
    Animator a;a.Play(kWalk);a.Update(.25f);a.Play(kSwing,.1f);a.Update(.05f);Pose blend;a.Evaluate(blend);
    float mn[3],mx[3];PoseBounds(blend,mn,mx);check(mx[1]>40 && mx[1]<180,"crossfade pose");
    namespace S=royale::bokoblin_snd;
    for (const auto& clip:S::kClips) {
        check(clip.count>S::kRate/5 && clip.count<S::kRate,"short PCM effects");
        int peak=0;for (int i=0;i<clip.count;i++) peak=std::max(peak,std::abs(static_cast<int>(clip.data[i])));
        check(peak>1000 && peak<30000 && clip.data[0]==0,"audible unclipped effects with fade");
    }
    std::printf("Bokoblin model/audio: %d failures, %d triangles, %d clips\n",failures,kTriCount,kClipCount);
    return failures?1:0;
}

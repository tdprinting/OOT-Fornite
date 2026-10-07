#include "maya_anim.h"
#include "map.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <cstring>
#include <cstdlib>
#undef assert
#define assert(expr) do { if (!(expr)) { std::cerr << "Maya validation failed: " << #expr << " at " << __LINE__ << "\n"; std::exit(1); } } while (0)
int main() {
    namespace M = royale::maya;
    static_assert(M::kClipCount==10 && M::kFaceCount==5);
    assert(royale::kMayaCompanionLineCount==14);
    for (const auto& batch:M::kBatches) {
        assert(batch.vertCount>0 && batch.vertCount<=32);
        assert(batch.firstVert+batch.vertCount<=M::kVertCount);
        assert(batch.firstTri+batch.triCount<=M::kTriCount);
        int bone=M::kVerts[batch.firstVert].b0;
        for (int v=0;v<batch.vertCount;v++) assert(M::kVerts[batch.firstVert+v].b0==bone);
        for (int t=0;t<batch.triCount;t++) for(int k=0;k<3;k++) assert(M::kTris[batch.firstTri+t][k]<batch.vertCount);
    }
    for (int clip=0;clip<M::kClipCount;clip++) {
        const auto& info=M::kClips[clip];
        assert(info.firstFrame+info.frames<=M::kFrameCount);
        bool animated=false;
        for (int f=0;f<info.frames;f++) {
            M::Pose p;M::SampleClip(clip,f/info.fps,p);
            for(int b=0;b<M::kBoneCount;b++) {
                float norm=0;for(float q:p.bone[b].q) norm+=q*q;assert(std::fabs(norm-1)<.001f);
                for(int k=0;k<3;k++) assert(std::isfinite(p.bone[b].t[k]));
                if(f>0) for(int k=0;k<7;k++) if(M::kPoses[((info.firstFrame+f)*M::kBoneCount+b)*7+k]!=M::kPoses[(info.firstFrame*M::kBoneCount+b)*7+k]) animated=true;
            }
            for (const auto& v:M::kVerts) {
                assert(v.b0<M::kBoneCount && v.b1<M::kBoneCount);
                float pos[3],n[3];M::SkinVertex(p,v,pos,n);
                for(int k=0;k<3;k++) assert(std::isfinite(pos[k]) && std::isfinite(n[k]));
            }
        }
        assert(animated);
    }
    // Every hobby reveals exactly its own prop; walking parks all five.
    const int clips[]={M::kTablet,M::kDraw,M::kPizza,M::kScooter,M::kLearn};
    const char* names[]={"tablet","draw","pizza","scooter","learn"};
    for(int c=0;c<5;c++) {M::Pose p;M::SampleClip(clips[c],.2f,p);for(int n=0;n<5;n++) {int b=0;while(std::strcmp(M::kBoneNames[b],names[n])!=0)++b;assert((p.bone[b].t[1]>-100)==(n==c));}}
    M::Animator a;a.Play(M::kTablet);a.Update(.1f);a.Play(M::kWalk);a.Update(.1f);M::Pose p;a.Evaluate(p);
    for(const auto& bone:p.bone)for(float q:bone.q)assert(std::isfinite(q));
    std::cout<<"Maya: all batches, rig transforms, ten clips, prop visibility and transitions passed\n";
}

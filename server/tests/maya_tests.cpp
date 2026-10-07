#include "maya_anim.h"
#include "maya_sounds.h"
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
    static_assert(M::kClipCount==15 && M::kFaceCount==8);
    assert(royale::kMayaCompanionLineCount==14);
    for (const auto& batch:M::kBatches) {
        assert(batch.vertCount>0 && batch.vertCount<=32);
        assert(batch.firstVert+batch.vertCount<=M::kVertCount);
        assert(batch.firstTri+batch.triCount<=M::kTriCount);
        int bone=M::kVerts[batch.firstVert].b0;
        for (int v=0;v<batch.vertCount;v++) assert(M::kBoneIsProp[M::kVerts[batch.firstVert+v].b0]==M::kBoneIsProp[bone]);
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
    M::Pose idle;M::SampleClip(M::kIdle,0,idle);float minY=1e9f,maxY=-1e9f;
    for(const auto& v:M::kVerts) if(!M::kBoneIsProp[v.b0]) {float pos[3],n[3];M::SkinVertex(idle,v,pos,n);minY=std::min(minY,pos[1]*M::kWorldScale);maxY=std::max(maxY,pos[1]*M::kWorldScale);}
    assert(maxY-minY>45 && maxY-minY<61);
    assert(M::Expression(M::kTalk,.25f,0)!=M::Expression(M::kTalk,.4f,0));
    assert(M::Expression(M::kGiggle,1,0)==M::kFaceGiggle);
    assert(M::Expression(M::kIdle,0,3.18f)==M::kFaceShut);
    bool blended=false;for(const auto& v:M::kVerts) blended|=v.b0!=v.b1 && v.w0<255;assert(blended);
    for(int f=0;f<M::kFrameCount;f++)assert(M::kFrameFaces[f]<M::kFaceCount);
    static_assert(royale::maya_snd::kClipCount==2);
    for(const auto& clip:royale::maya_snd::kClips) {
        assert(clip.count>1000 && clip.count<royale::maya_snd::kRate*2);
        assert(clip.data[0]==0 && clip.data[clip.count-1]==0);
        int peak=0;for(int i=0;i<clip.count;i++)peak=std::max(peak,std::abs(static_cast<int>(clip.data[i])));
        assert(peak>20000 && peak<=24576);
    }
    // Every hobby reveals exactly its own prop; walking parks all five.
    const int clips[]={M::kTablet,M::kDraw,M::kPizza,M::kScooter,M::kLearn};
    const char* names[]={"tablet","draw","pizza","scooter","learn"};
    for(int c=0;c<5;c++) {M::Pose p;M::SampleClip(clips[c],.2f,p);for(int n=0;n<5;n++) {int b=0;while(std::strcmp(M::kBoneNames[b],names[n])!=0)++b;assert((p.bone[b].t[1]>-100)==(n==c));}}
    auto boneOf=[](const char* name) { for(int b=0;b<M::kBoneCount;b++)if(std::strcmp(M::kBoneNames[b],name)==0)return b;std::exit(1); };
    auto point=[](const M::Pose& p,int bone,const float rest[3],float out[3]) {M::Rotate(p.bone[bone].q,rest,out);for(int k=0;k<3;k++)out[k]+=p.bone[bone].t[k];};
    auto contact=[&](int clip,const char* prop,const char* hand,const float grip[3],float tolerance) {
        const auto& info=M::InfoOf(clip);
        const float palm[3]={std::strcmp(hand,"handL")==0 ? 20.6f : -20.6f,69.7f,.6f};
        for(int f=0;f<info.frames;f+=2) {M::Pose p;M::SampleClip(clip,f/info.fps,p);float a[3],b[3];point(p,boneOf(prop),grip,a);point(p,boneOf(hand),palm,b);
            float d=0;for(int k=0;k<3;k++)d+=(a[k]-b[k])*(a[k]-b[k]);
            if(d>tolerance*tolerance)std::cerr<<"Grip drift "<<prop<<" frame "<<f<<": "<<std::sqrt(d)<<"\n";
            assert(d<tolerance*tolerance);
        }
    };
    const float tabletGrip[3]={-12.7f,84,22.6f},paperGrip[3]={-12.6f,84,22.4f},pencilGrip[3]={12,83.7f,23},handleL[3]={15.5f,87,21},handleR[3]={-15.5f,87,21};
    contact(M::kTablet,"tablet","handL",tabletGrip,2.7f);
    contact(M::kDraw,"draw","handL",paperGrip,2.7f);
    contact(M::kDraw,"pencil","handR",pencilGrip,2.7f);
    contact(M::kScooter,"scooter","handL",handleL,2.7f);contact(M::kScooter,"scooter","handR",handleR,2.7f);
    for(int f=0;f<M::InfoOf(M::kDraw).frames;f+=2) {
        M::Pose p;M::SampleClip(M::kDraw,f/M::InfoOf(M::kDraw).fps,p);
        const float tip[3]={12,78.5f,23};float world[3],local[3],relative[3];point(p,boneOf("pencil"),tip,world);
        const auto& book=p.bone[boneOf("draw")];for(int k=0;k<3;k++)relative[k]=world[k]-book.t[k];
        const float inverse[4]={-book.q[0],-book.q[1],-book.q[2],book.q[3]};M::Rotate(inverse,relative,local);
        assert(std::fabs(local[2]-24.5f)<.25f && std::fabs(local[0])<12.5f && std::fabs(local[1]-84)<9);
    }
    // While a tap/drag highlight is visible, the extended index fingertip meets it.
    for(int f=0;f<M::InfoOf(M::kTablet).frames;f++) {
        M::Pose p;M::SampleClip(M::kTablet,f/M::InfoOf(M::kTablet).fps,p);
        if(p.bone[boneOf("tablet_cursor")].t[1]<-100)continue;
        const float tip[3]={-18.8f,64.95f,.6f},cursor[3]={0,85,25.2f};float a[3],b[3];point(p,boneOf("indexR"),tip,a);point(p,boneOf("tablet_cursor"),cursor,b);
        float d=0;for(int k=0;k<3;k++)d+=(a[k]-b[k])*(a[k]-b[k]);
        assert(d<2.0f*2.0f);
    }
    // The visible page/screen points toward Maya and upward, not toward the camera.
    for(int clip:{M::kTablet,M::kDraw,M::kLearn}) {
        M::Pose p;M::SampleClip(clip,.9f,p);float front[3];const float restFront[3]={0,0,1};
        M::Rotate(p.bone[boneOf(clip==M::kTablet ? "tablet" : clip==M::kDraw ? "draw" : "learn")].q,restFront,front);
        assert(front[1]>.5f && front[2]<-.2f); // game +Y up, -Z is toward Maya
    }
    for(int start:{M::kWave,M::kTablet,M::kCheer}) {
        M::Animator a;a.Play(start,0,true);a.Update(M::ClipSeconds(start)*.7f);a.Play(M::kIdle,.4f);
        for(int tick=0;tick<9;tick++) {a.Update(.05f);M::Pose p;a.Evaluate(p);
            for(int b=0;b<M::kBoneCount;b++)if(M::kKeepJointLink[b]) {
                float joint[3],parent[3];point(p,b,M::kBoneHeads[b],joint);point(p,M::kBoneParents[b],M::kBoneHeads[b],parent);
                for(int k=0;k<3;k++)assert(std::fabs(joint[k]-parent[k])<.002f);
            }
        }
    }
    M::Animator a;a.Play(M::kTablet);a.Update(.1f);a.Play(M::kWalk);a.Update(.1f);M::Pose p;a.Evaluate(p);
    for(const auto& bone:p.bone)for(float q:bone.q)assert(std::isfinite(q));
    std::cout<<"Maya: all batches, rig transforms, fifteen clips, child scale and expressions, prop visibility and transitions passed\n";
}

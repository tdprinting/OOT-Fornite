#include "maya_anim.h"
#include "maya_sounds.h"
#include "avriella_anim.h"
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
    static_assert(M::kClipCount==19 && M::kFaceCount==8);
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
    // Feet never sink into the floor while she walks or runs, and sitting lowers her.
    for(int clip:{(int)M::kWalk,(int)M::kRun}) {
        float lowest=1e9f;
        for(int f=0;f<M::kClips[clip].frames;f++) {M::Pose p;M::SampleClip(clip,f/M::kClips[clip].fps,p);
            for(const auto& v:M::kVerts) if(!M::kBoneIsProp[v.b0]) {float pos[3],n[3];M::SkinVertex(p,v,pos,n);lowest=std::min(lowest,pos[1]);}}
        assert(lowest>-1.5f);
    }
    for(int clip:{(int)M::kSit,(int)M::kSleep}) {
        M::Pose p;M::SampleClip(clip,1.0f,p);float lo=1e9f,hi=-1e9f;
        for(const auto& v:M::kVerts) if(!M::kBoneIsProp[v.b0]) {float pos[3],n[3];M::SkinVertex(p,v,pos,n);lo=std::min(lo,pos[1]);hi=std::max(hi,pos[1]);}
        assert(lo>-6.0f && hi*M::kWorldScale<45.0f);   // seated: above the floor, well under her standing height
    }
    // Stride constants match the clips: a cycle's planted foot travels about one stride.
    assert(M::kWalkStride>20 && M::kRunStride>M::kWalkStride);
    // Cloth physics on her ponytail: walking along +X (her left) trails the tail out to -X, and a rest pose with no motion leaves it alone.
    {
        royale::TailTracker tr;M::Pose rest;M::SampleClip(M::kIdle,0,rest);M::Pose moved=rest;
        double now=0;tr.Update(now,0,0,0,0);
        for(int i=0;i<40;i++){now+=1.0/20;tr.Update(now,i*6.0f,0,0,0);}
        M::ApplyPonytail(moved,tr.spring,1.0f);
        const int b2=M::BoneByName("ponytail2");assert(b2>=0);
        float a[3],b[3];royale::PosedPoint(rest.bone[b2],M::kBoneHeads[b2],a);royale::PosedPoint(moved.bone[b2],M::kBoneHeads[b2],b);
        // the lower joint's rest tip moves; check a vertex on it instead of the head
        float tipRest[3]={0,0,0},tipMoved[3]={0,0,0};int n=0;
        for(const auto& v:M::kVerts) if(v.b0==b2&&v.w0==255){float pr[3],pm[3],nn[3];M::SkinVertex(rest,v,pr,nn);M::SkinVertex(moved,v,pm,nn);for(int k=0;k<3;k++){tipRest[k]+=pr[k];tipMoved[k]+=pm[k];}n++;}
        assert(n>0);
        assert(tipMoved[0]/n<tipRest[0]/n-.2f);           // trails to -X
        assert(std::fabs(tipMoved[1]/n-tipRest[1]/n)<4.0f);   // still hangs, does not stretch away
        M::Pose off=rest;M::ApplyPonytail(off,tr.spring,0.0f);
        for(int k=0;k<7;k++) assert(off.bone[b2].q[k%4]==rest.bone[b2].q[k%4]);   // cloth physics off: untouched
        royale::TailTracker still;now=0;still.Update(now,5,5,1,0);for(int i=0;i<40;i++){now+=1.0/20;still.Update(now,5,5,1,0);}
        M::Pose calm=rest;M::ApplyPonytail(calm,still.spring,1.0f);
        assert(std::fabs(calm.bone[b2].q[0]-rest.bone[b2].q[0])<.08f);
    }
    {   // Avriella's tuft: moving forward trails it back (toward -Z), cloth off leaves it alone.
        namespace A=royale::avriella;royale::TailTracker tr;double now=0;tr.Update(now,0,0,0,0);
        for(int i=0;i<40;i++){now+=1.0/20;tr.Update(now,0,i*6.0f,0,0);}
        A::Pose rest;A::SampleClip(A::kSit,0,rest);A::Pose moved=rest;A::ApplyTuft(moved,tr.spring,1.0f);
        float zr=0,zm=0;int n=0;for(const auto& v:A::kVerts) if(v.b0==5&&v.w0==255){float pr[3],pm[3],nn[3];A::SkinVertex(rest,v,pr,nn);A::SkinVertex(moved,v,pm,nn);zr+=pr[2];zm+=pm[2];n++;}
        assert(n>0 && zm/n<zr/n-.1f);
        A::Pose off=rest;A::ApplyTuft(off,tr.spring,0.0f);assert(off.bone[5].q[0]==rest.bone[5].q[0] && off.bone[5].t[2]==rest.bone[5].t[2]);
    }
    // The video-chat scene: the tablet is in her hands until the drop, the screen turns to the fart picture, and a dropped tablet settles flat.
    {
        int tb=M::BoneByName("tablet");assert(tb>=0);
        M::Pose early,late;M::SampleClip(M::kVideochat,M::kVideoChatDropTime-.05f,early);M::SampleClip(M::kVideochat,M::kVideoChatDropTime+.1f,late);
        assert(early.bone[tb].t[1]>-100 && late.bone[tb].t[1]<-100);
        assert(M::ScreenFor(M::kVideochat,0.1f)==M::kScreenBuilding && M::ScreenFor(M::kVideochat,M::kVideoChatFartTime+.3f)==M::kScreenFart);
        assert(M::ScreenFor(M::kTablet,1.0f)==M::kScreenBuilding);
        assert(M::kVideoChatFartTime<M::kVideoChatDropTime && M::kVideoChatDropTime<M::kVideoChatFleeTime && M::kVideoChatFleeTime<M::ClipSeconds(M::kVideochat)+.2f);
        royale::TabletBody b;b.p[1]=14;b.v[0]=40;b.v[1]=25;b.w[0]=7;b.w[2]=-3;
        auto floor=[](float,float){return 0.0f;};
        for(int i=0;i<1200&&!b.asleep;i++){b.Step(1/120.0f,floor);assert(b.LowestCorner()>-1.0f);}
        assert(b.asleep && b.p[1]<2.0f && b.p[1]>0.2f);   // lying flat, on the floor
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
    std::cout<<"Maya: all batches, rig transforms, all nineteen clips, child scale and expressions, prop visibility and transitions passed\n";
}

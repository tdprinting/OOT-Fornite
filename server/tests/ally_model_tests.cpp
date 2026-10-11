// The four hireable-ally models (shared/ally_anim.h): budgets, data bounds, every clip moves, feet stay on the ground while walking, and the
// curled-up Goron really is a ball.
#include "../../shared/ally_anim.h"
#include "../../shared/ally.h"
#include <cmath>
#include <cstdio>
using namespace royale::allymodel;
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what, const char* model) { if (!ok) { std::printf("FAIL %s: %s\n", model, what); ++failures; } };
    for (int kind = 0; kind < royale::kAllyCount; kind++) {
        const Model& m = ModelOf(kind);
        check(m.triCount > 2000 && m.triCount < 14000, "triangle budget", m.name);
        check(m.vertCount < 18000, "vertex budget (skinned on the CPU every frame)", m.name);
        int tris = 0;
        for (int b = 0; b < m.batchCount; b++) {
            const Batch& bt = m.batches[b];
            check(bt.vertCount <= 32 && bt.firstVert + bt.vertCount <= m.vertCount && bt.firstTri + bt.triCount <= m.triCount, "batch bounds", m.name);
            tris += bt.triCount;
            for (int t = bt.firstTri; t < bt.firstTri + bt.triCount; t++) for (int i = 0; i < 3; i++) check(m.tris[t][i] < bt.vertCount, "triangle indices", m.name);
            check(bt.tex == kTexBody || bt.tex == kTexFace, "batch texture", m.name);
            const int w = bt.tex == kTexBody ? kBodyW : kFaceW, h = bt.tex == kTexBody ? kBodyH : kFaceH;
            for (int i = bt.firstVert; i < bt.firstVert + bt.vertCount; i++)
                check(m.verts[i].s >= 0 && m.verts[i].s <= w * 32 && m.verts[i].t >= 0 && m.verts[i].t <= h * 32, "UV bounds", m.name);
        }
        check(tris == m.triCount, "all triangles in batches", m.name);
        for (int i = 0; i < m.vertCount; i++) {
            const Vert& v = m.verts[i];
            check(v.b0 < kBoneCount && v.b1 < kBoneCount, "skin bounds", m.name);
        }
        check(m.walkSpeed > 50 && m.height > 40 && m.height < 160, "walk speed and height", m.name);
        for (int c = 0; c < kClipCount; c++) {
            const ClipInfo& info = m.clips[c];
            check(info.frames > 1 && info.firstFrame + info.frames <= m.frameCount && info.fps > 0, "clip bounds", m.name);
            bool moving = false;
            Pose first;
            SampleClip(m, c, 0, first);
            for (int f = 0; f < info.frames; f++) {
                Pose p;
                SampleClip(m, c, f / info.fps, p);
                for (int b = 0; b < kBoneCount; b++) for (int k = 0; k < 4; k++) moving |= std::fabs(p.bone[b].q[k] - first.bone[b].q[k]) > .001f;
                float mn[3], mx[3];
                PoseBounds(m, p, mn, mx);
                for (int k = 0; k < 3; k++) check(std::isfinite(mn[k]) && std::isfinite(mx[k]) && std::fabs(mn[k]) < 260 && std::fabs(mx[k]) < 260, "finite bounded pose", m.name);
                if (c == kWalk || c == kIdle || c == kAttack || c == kRest) check(std::fabs(mn[1]) < 0.2f, "feet on the ground", m.name);
            }
            check(moving, "nonempty animation", m.name);
        }
        Animator a;
        a.model = &m;
        a.Play(kWalk, 0.0f);
        a.Update(.25f, 1.5f);
        a.Play(kAttack, .1f);
        a.Update(.05f);
        check(!a.Done() && a.from == kWalk, "crossfade", m.name);
        a.Update(5.0f);
        check(a.Done(), "one-shot ends", m.name);
    }
    // The Goron's ball: about as wide as it is deep, and much lower than him standing.
    Pose ball, stand;
    SampleClip(goron::kModel, kRest, 0, ball);
    SampleClip(goron::kModel, kIdle, 0, stand);
    float bmn[3], bmx[3], smn[3], smx[3];
    PoseBounds(goron::kModel, ball, bmn, bmx);
    PoseBounds(goron::kModel, stand, smn, smx);
    check(bmx[1] < smx[1] * 0.6f, "curled ball is low", "goron");
    check(std::fabs((bmx[2] - bmn[2]) - (bmx[1] - bmn[1]) * 1.7f) < 60.0f && std::fabs(bmn[2] + bmx[2]) < 20.0f, "ball centred over his feet", "goron");
    if (failures == 0) std::printf("ally model tests passed\n");
    return failures == 0 ? 0 : 1;
}

// ---- player skins: another body on Link's skeleton ---------------------------------------------------------------------------------
// A skin model (shared/skin_<id>_model.h, made by tools/skins/, docs/SKINS.md) is a whole new mesh for Link that still plays every one of
// the game's animations, because it hangs off the game's own 21 Link limbs. Patch 0031 calls OnPlayerSkinLimb for every limb the game is
// about to draw, with that limb's final position and rotation; the hook keeps the limb's matrix, blanks the game's display list for it,
// and when the last limb has gone by it skins the whole mesh on the CPU with those matrices and draws it in one go. The hands keep the
// game's own display list while they hold something (the sword or shield is part of that display list) and the sheath always does, so
// those parts of the skin are left out on that pass.
//
// Binding (how the skin's vertices find their place on the skeleton) is worked out once per skeleton and model, at run time, so nothing
// from the game's skeleton is ever stored in the repository: the skeleton is posed in a known standing pose (the last frame of our own
// landing clip), the model's anchor points (its hips, knees, ankles, shoulders, elbows, wrists, the root of the cap's tail) are matched
// to those joints, each limb's mesh is scaled to its game bone's length and turned from the model's T pose to the way the game's limb
// hangs, and the result is expressed in each limb's own space. From then on a vertex is just limbMatrix * bind * vertex.
namespace sk = royale::skinmodel;

// The models behind royale::kSkinModelNames (shared/skins.h): 0 is the game's own Link.
const sk::Model* const kSkinModelOf[] = { nullptr, &sk::link_hd::kModel };
static_assert(sizeof(kSkinModelOf) / sizeof(kSkinModelOf[0]) == royale::kSkinModelCount, "one model per name in royale::kSkinModelNames");

struct SkinStats { unsigned limbsSeen = 0, drawn = 0; };
SkinStats gSkinStats;   // limbs the skin hook took and skins drawn, for the self-test (SelfTest.inc resets it)
int gLocalSkinModel = 0;   // the model the local player picked (index into kSkinModelOf); sent to everyone else with the tunic colour
constexpr float kSkinFarDistance = 1800.0f;   // beyond this from the camera the game's own Link is drawn instead (cheaper, and too small to tell)

const sk::Model* SkinModelOf(int index) {
    return index > 0 && index < royale::kSkinModelCount ? kSkinModelOf[index] : nullptr;
}

// A small affine transform: v' = r * v + t.
struct Aff {
    float r[3][3];
    float t[3];
    void Apply(const float v[3], float out[3]) const {
        for (int i = 0; i < 3; i++) out[i] = r[i][0] * v[0] + r[i][1] * v[1] + r[i][2] * v[2] + t[i];
    }
    void Rotate(const float v[3], float out[3]) const {
        for (int i = 0; i < 3; i++) out[i] = r[i][0] * v[0] + r[i][1] * v[1] + r[i][2] * v[2];
    }
};
Aff AffIdentity() { Aff a = {}; a.r[0][0] = a.r[1][1] = a.r[2][2] = 1; return a; }
Aff AffFromMtxF(const MtxF& m) {
    Aff a;
    a.r[0][0] = m.xx; a.r[0][1] = m.xy; a.r[0][2] = m.xz; a.t[0] = m.xw;
    a.r[1][0] = m.yx; a.r[1][1] = m.yy; a.r[1][2] = m.yz; a.t[1] = m.yw;
    a.r[2][0] = m.zx; a.r[2][1] = m.zy; a.r[2][2] = m.zz; a.t[2] = m.zw;
    return a;
}
// a after b: (a * b)(v) = a(b(v)).
Aff AffMul(const Aff& a, const Aff& b) {
    Aff o = {};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) o.r[i][j] = a.r[i][0] * b.r[0][j] + a.r[i][1] * b.r[1][j] + a.r[i][2] * b.r[2][j];
        o.t[i] = a.r[i][0] * b.t[0] + a.r[i][1] * b.t[1] + a.r[i][2] * b.t[2] + a.t[i];
    }
    return o;
}
bool AffInverse(const Aff& a, Aff& out) {
    const float (*m)[3] = a.r;
    const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (std::fabs(det) < 1e-12f) return false;
    const float d = 1.0f / det;
    out.r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * d; out.r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * d; out.r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * d;
    out.r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * d; out.r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * d; out.r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * d;
    out.r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * d; out.r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * d; out.r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * d;
    for (int i = 0; i < 3; i++) out.t[i] = -(out.r[i][0] * a.t[0] + out.r[i][1] * a.t[1] + out.r[i][2] * a.t[2]);
    return true;
}
// The smallest rotation that turns unit vector a onto unit vector b (Rodrigues), as a 3x3.
void RotationBetween(const float a[3], const float b[3], float r[3][3]) {
    float v[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    const float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    const float s2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = i == j ? 1.0f : 0.0f;
    if (s2 < 1e-10f) {
        if (c > 0) return;
        // opposite: turn half way round about any axis at right angles to a
        float ax[3] = { std::fabs(a[0]) < 0.9f ? 1.0f : 0.0f, std::fabs(a[0]) < 0.9f ? 0.0f : 1.0f, 0.0f };
        float p[3] = { ax[1] * a[2] - ax[2] * a[1], ax[2] * a[0] - ax[0] * a[2], ax[0] * a[1] - ax[1] * a[0] };
        const float n = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        for (float& x : p) x /= n;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = 2 * p[i] * p[j] - (i == j ? 1.0f : 0.0f);
        return;
    }
    const float k = (1 - c) / s2;
    const float vx[3][3] = { { 0, -v[2], v[1] }, { v[2], 0, -v[0] }, { -v[1], v[0], 0 } };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float vv = 0;
            for (int l = 0; l < 3; l++) vv += vx[i][l] * vx[l][j];
            r[i][j] += vx[i][j] + k * vv;
        }
}
// v' = p + scale * R * (v - q): the model's point q goes to the skeleton's point p.
Aff AffPlace(const float p[3], const float q[3], float scale, const float R[3][3]) {
    Aff a;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) a.r[i][j] = scale * R[i][j];
        a.t[i] = p[i] - (a.r[i][0] * q[0] + a.r[i][1] * q[1] + a.r[i][2] * q[2]);
    }
    return a;
}
float Dist3(const float a[3], const float b[3]) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// The bind of one model to one skeleton: a transform per limb from the model's space into that limb's space.
struct SkinBind {
    Aff bind[sk::kLimbCount];
    bool ok = false;
};
std::unordered_map<uint64_t, SkinBind> gSkinBinds;   // (skeleton, model) -> bind

uint64_t SkinBindKey(const void* skeleton, const sk::Model* model) {
    return reinterpret_cast<uint64_t>(skeleton) * 1000003ull ^ reinterpret_cast<uint64_t>(model);
}

// Works the bind out: see the top of this section.
bool ComputeSkinBind(void** skeleton, int limbCount, const sk::Model& model, SkinBind& out) {
    using namespace sk;
    if (skeleton == nullptr || limbCount < kLimbCount + 1) return false;
    // the skeleton's tree (parents from the child/sibling links; the limbs are listed parents first)
    int parent[kLimbCount];
    const LodLimb* limbs[kLimbCount];
    for (int i = 0; i < kLimbCount; i++) {
        limbs[i] = static_cast<const LodLimb*>(SEGMENTED_TO_VIRTUAL(skeleton[i]));
        if (limbs[i] == nullptr) return false;
        parent[i] = -1;
    }
    for (int i = 0; i < kLimbCount; i++) {
        int c = limbs[i]->child;
        for (int guard = 0; c != LIMB_DONE && c < kLimbCount && guard < kLimbCount; guard++) {
            parent[c] = i;
            c = limbs[c]->sibling;
        }
    }
    // the standing pose, through the game's own matrix code so the numbers match what it draws
    const auto& stand = royale::link_anims::kLand[sizeof(royale::link_anims::kLand) / sizeof(royale::link_anims::kLand[0]) - 1];
    Aff S[kLimbCount];
    float P[kLimbCount][3];
    Matrix_Push();
    for (int i = 0; i < kLimbCount; i++) {
        MtxF m;
        if (i == 0) {
            SkinMatrix_Clear(&m);
        } else {
            if (parent[i] < 0 || parent[i] >= i) { Matrix_Pop(); return false; }
            Aff pa = S[parent[i]];
            m.xx = pa.r[0][0]; m.xy = pa.r[0][1]; m.xz = pa.r[0][2]; m.xw = pa.t[0];
            m.yx = pa.r[1][0]; m.yy = pa.r[1][1]; m.yz = pa.r[1][2]; m.yw = pa.t[1];
            m.zx = pa.r[2][0]; m.zy = pa.r[2][1]; m.zz = pa.r[2][2]; m.zw = pa.t[2];
            m.wx = m.wy = m.wz = 0; m.ww = 1;
        }
        Matrix_Put(&m);
        Vec3f pos = i == 0 ? Vec3f{ 0.0f, static_cast<float>(stand.rootY), 0.0f }
                           : Vec3f{ static_cast<float>(limbs[i]->jointPos.x), static_cast<float>(limbs[i]->jointPos.y), static_cast<float>(limbs[i]->jointPos.z) };
        Vec3s rot = { stand.rot[i][0], stand.rot[i][1], stand.rot[i][2] };
        Matrix_TranslateRotateZYX(&pos, &rot);
        Matrix_Get(&m);
        S[i] = AffFromMtxF(m);
        for (int k = 0; k < 3; k++) P[i][k] = S[i].t[k];
    }
    Matrix_Pop();

    const Anchor* A = model.anchors;
    auto apt = [&](int i) { return &A[i].x; };
    auto adir = [&](int i) { return &A[i].dx; };
    const int chain[][2] = { { kLThigh, kLShin }, { kLShin, kLFoot }, { kRThigh, kRShin }, { kRShin, kRFoot },
                             { kLShoulder, kLForearm }, { kLForearm, kLHand }, { kRShoulder, kRForearm }, { kRForearm, kRHand } };
    for (const auto& c : chain) if (!A[c[0]].have || !A[c[1]].have) return false;
    if (!A[kHead].have) return false;

    // the trunk: one similarity transform, the model's shoulders and hips onto the skeleton's
    float cg[3], cm[3], hg[3], hm[3];
    for (int k = 0; k < 3; k++) {
        cg[k] = 0.5f * (P[kLShoulder][k] + P[kRShoulder][k]); cm[k] = 0.5f * (apt(kLShoulder)[k] + apt(kRShoulder)[k]);
        hg[k] = 0.5f * (P[kLThigh][k] + P[kRThigh][k]);       hm[k] = 0.5f * (apt(kLThigh)[k] + apt(kRThigh)[k]);
    }
    const float trunkModel = Dist3(cm, hm);
    if (trunkModel < 1e-4f) return false;
    const float st = Dist3(cg, hg) / trunkModel;
    float I[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    Aff T[kLimbCount];
    const Aff trunk = AffPlace(cg, cm, st, I);
    for (int i = 0; i < kLimbCount; i++) T[i] = trunk;

    // limbs with a bone to the next joint: scaled to the bone's length and turned from the model's pose to the skeleton's
    float k[kLimbCount];
    float R[kLimbCount][3][3];
    for (int i = 0; i < kLimbCount; i++) { k[i] = st; for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) R[i][a][b] = I[a][b]; }
    for (const auto& c : chain) {
        const int i = c[0], j = c[1];
        float g[3];
        const float lg = Dist3(P[j], P[i]), lm = Dist3(apt(j), apt(i));
        if (lg < 1e-3f || lm < 1e-4f) return false;
        for (int a = 0; a < 3; a++) g[a] = (P[j][a] - P[i][a]) / lg;
        RotationBetween(adir(i), g, R[i]);
        k[i] = lg / lm;
        T[i] = AffPlace(P[i], apt(i), k[i], R[i]);
    }
    // hands follow their forearm; feet their shin (and stand on the ground: squashed or stretched to the skeleton's ankle height)
    T[kLHand] = AffPlace(P[kLHand], apt(kLHand), k[kLForearm], R[kLForearm]);
    T[kRHand] = AffPlace(P[kRHand], apt(kRHand), k[kRForearm], R[kRForearm]);
    for (int side = 0; side < 2; side++) {
        const int foot = side ? kRFoot : kLFoot, shin = side ? kRShin : kLShin;
        float fwd[3] = { 0, 0, 1 }, d[3] = { adir(foot)[0], 0.0f, adir(foot)[2] };
        const float dn = std::sqrt(d[0] * d[0] + d[2] * d[2]);
        float Rf[3][3];
        if (dn > 1e-3f) { d[0] /= dn; d[2] /= dn; RotationBetween(d, fwd, Rf); } else { for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) Rf[a][b] = I[a][b]; }
        Aff f = AffPlace(P[foot], apt(foot), k[shin], Rf);
        const float modelAnkle = k[shin] * (apt(foot)[1] - model.floorY);
        if (modelAnkle > 1e-3f && P[foot][1] > 1e-3f) {
            const float kv = std::clamp(P[foot][1] / modelAnkle, 0.5f, 2.0f);   // the soles meet the floor
            Aff squash = AffIdentity();
            squash.r[1][1] = kv;
            squash.t[1] = P[foot][1] * (1 - kv);
            f = AffMul(squash, f);
        }
        T[foot] = f;
    }
    // the cap's tail: turned from the way the model wears it to the way it should hang, about its own root
    if (A[kHat].have) {
        float base[3], Rh[3][3], hd[3] = { model.hatDir[0], model.hatDir[1], model.hatDir[2] };
        const float hn = std::sqrt(hd[0] * hd[0] + hd[1] * hd[1] + hd[2] * hd[2]);
        if (hn > 1e-4f) {
            for (float& x : hd) x /= hn;
            trunk.Apply(apt(kHat), base);
            RotationBetween(adir(kHat), hd, Rh);
            T[kHat] = AffPlace(base, apt(kHat), st, Rh);
        }
    }
    for (int i = 0; i < kLimbCount; i++) {
        Aff inv;
        if (!AffInverse(S[i], inv)) return false;
        out.bind[i] = AffMul(inv, T[i]);
    }
    out.ok = true;
    return true;
}

const SkinBind* SkinBindFor(const Player* player, const sk::Model& model) {
    if (player == nullptr) return nullptr;
    void** skeleton = player->skelAnime.skeleton;
    const uint64_t key = SkinBindKey(skeleton, &model);
    auto it = gSkinBinds.find(key);
    if (it != gSkinBinds.end()) return it->second.ok ? &it->second : nullptr;
    SkinBind& b = gSkinBinds[key];
    ComputeSkinBind(skeleton, player->skelAnime.limbCount, model, b);
    return b.ok ? &b : nullptr;
}
void ForgetSkinBinds() { gSkinBinds.clear(); }

// One pass over one Link's limbs (the game draws a Player's skeleton limb by limb, root first, torso last).
struct SkinPass {
    const sk::Model* model = nullptr;
    const SkinBind* bind = nullptr;
    const Player* player = nullptr;
    Aff limb[sk::kLimbCount];
    uint32_t seen = 0;
    bool keep[sk::kLimbCount] = {};   // the game's own display list stays for this limb (a hand with something in it, the sheath)
    bool pause = false;
};
SkinPass gSkinPass;
int gSkinDrawsThisFrame = 0;   // shown on the Graphics page

// Which model a Player actor wears: the local player's choice, or what the other player told the server.
int SkinModelIndexOf(PlayState* play, const Player* player) {
    if (player == nullptr || player == GET_PLAYER(play)) return gLocalSkinModel;
    const royale::PuppetState* st = StateOf(reinterpret_cast<const Actor*>(player));
    return st ? st->skinModel : 0;
}

void DrawSkinModel(PlayState* play, const SkinPass& pass) {
    Feat("player skins: draw");
    const sk::Model& m = *pass.model;
    GfxLayer layer(play, GfxLayerId::Characters);
    Vtx* vtx = static_cast<Vtx*>(FrameAlloc(play, sizeof(Vtx) * m.vertCount));
    if (vtx == nullptr) return;
    Aff K[sk::kLimbCount];
    for (int i = 0; i < sk::kLimbCount; i++) K[i] = AffMul(pass.limb[i], pass.bind->bind[i]);
    // the sun and the ambient light of the scene, baked into the vertex colours (as the allies and the bus are lit)
    const auto& ls = play->envCtx.lightSettings;
    float L[3] = { ls.light1Dir[0] / 127.0f, ls.light1Dir[1] / 127.0f, ls.light1Dir[2] / 127.0f };
    const float ln = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    if (ln > 1e-3f) for (float& x : L) x /= ln; else { L[0] = 0.35f; L[1] = 0.82f; L[2] = 0.45f; }
    float sun[3], amb[3];
    for (int c = 0; c < 3; c++) { sun[c] = ls.light1Color[c] / 255.0f; amb[c] = ls.ambientColor[c] / 255.0f; }
    if (pass.pause || (sun[0] + sun[1] + sun[2] < 0.05f && amb[0] + amb[1] + amb[2] < 0.05f)) { sun[0] = sun[1] = sun[2] = 0.75f; amb[0] = amb[1] = amb[2] = 0.45f; }
    const float* c0 = pass.limb[sk::kRoot].t;   // vertices go to the graphics chip relative to the root, in quarter units
    constexpr float kSub = 4.0f;
    for (int i = 0; i < m.vertCount; i++) {
        const sk::Vert& v = m.verts[i];
        const float p[3] = { v.x, v.y, v.z }, n[3] = { v.nx / 127.0f, v.ny / 127.0f, v.nz / 127.0f };
        float pa[3], pb[3], na[3], nb[3];
        const float w = v.w0 / 255.0f;
        K[v.b0].Apply(p, pa); K[v.b0].Rotate(n, na);
        if (v.b1 != v.b0 && w < 0.999f) { K[v.b1].Apply(p, pb); K[v.b1].Rotate(n, nb); } else { for (int k = 0; k < 3; k++) { pb[k] = pa[k]; nb[k] = na[k]; } }
        float po[3], no[3];
        for (int k = 0; k < 3; k++) { po[k] = pa[k] * w + pb[k] * (1 - w); no[k] = na[k] * w + nb[k] * (1 - w); }
        const float nn = std::sqrt(no[0] * no[0] + no[1] * no[1] + no[2] * no[2]);
        if (nn > 1e-6f) for (float& x : no) x /= nn;
        const float d = std::max(0.0f, no[0] * L[0] + no[1] * L[1] + no[2] * L[2]);
        Vtx& o = vtx[i];
        for (int k = 0; k < 3; k++) o.v.ob[k] = static_cast<s16>(std::lround(std::clamp((po[k] - c0[k]) * kSub, -32000.0f, 32000.0f)));
        o.v.flag = 0;
        o.v.tc[0] = v.s;
        o.v.tc[1] = v.t;
        for (int c = 0; c < 3; c++) o.v.cn[c] = static_cast<u8>(255.0f * std::clamp(amb[c] + sun[c] * d, 0.0f, 1.0f));
        o.v.cn[3] = 255;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING);              // the light is already in the vertex colours
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIDECALA, G_CC_PASS2);   // the texture times the vertex colour
    Matrix_Translate(c0[0], c0[1], c0[2], MTXMODE_NEW);
    Matrix_Scale(1.0f / kSub, 1.0f / kSub, 1.0f / kSub, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    int loaded = -1;
    for (int b = 0; b < m.batchCount; b++) {
        const sk::Batch& bt = m.batches[b];
        if (bt.limb < sk::kLimbCount && pass.keep[bt.limb]) continue;   // the game draws its own limb there this pass
        if (bt.tex != loaded) {
            loaded = bt.tex;
            gDPLoadTextureTile(POLY_OPA_DISP++, m.textures[bt.tex], G_IM_FMT_RGBA, G_IM_SIZ_16b, sk::kTexW, sk::kTexH, 0, 0, sk::kTexW - 1, sk::kTexH - 1, 0,
                               G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        }
        gSPVertex(POLY_OPA_DISP++, reinterpret_cast<uintptr_t>(&vtx[bt.firstVert]), bt.vertCount, 0);
        const int end = bt.firstTri + bt.triCount;
        int t = bt.firstTri;
        for (; t + 1 < end; t += 2)
            gSP2Triangles(POLY_OPA_DISP++, m.tris[t][0], m.tris[t][1], m.tris[t][2], 0, m.tris[t + 1][0], m.tris[t + 1][1], m.tris[t + 1][2], 0);
        if (t < end) gSP1Triangle(POLY_OPA_DISP++, m.tris[t][0], m.tris[t][1], m.tris[t][2], 0);
    }
    gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING);
    CLOSE_DISPS(play->state.gfxCtx);
    gSkinDrawsThisFrame++;
}

// Patch 0031: every limb of every Link the game draws, with its final position, rotation and display list. pause = 1 for the pause and
// menu pictures (no Player: the local player's own skin).
void OnPlayerSkinLimb(void* playPtr, void* playerPtr, int32_t limbIndex, void* dListPtr, void* posPtr, void* rotPtr, int32_t pause) {
    PlayState* play = static_cast<PlayState*>(playPtr);
    const Player* player = static_cast<const Player*>(playerPtr);
    Gfx** dList = static_cast<Gfx**>(dListPtr);
    Vec3f* pos = static_cast<Vec3f*>(posPtr);
    Vec3s* rot = static_cast<Vec3s*>(rotPtr);
    if (play == nullptr || dList == nullptr || pos == nullptr || rot == nullptr) return;
    const int i = limbIndex - 1;   // PLAYER_LIMB_ROOT is 1
    if (i < 0 || i >= sk::kLimbCount) return;
    SkinPass& pass = gSkinPass;
    if (i == sk::kRoot) {
        pass = SkinPass{};
        if (!DebugOn(kDbgSkins)) return;
        const Player* skel = player != nullptr ? player : GET_PLAYER(play);
        const sk::Model* model = SkinModelOf(SkinModelIndexOf(play, player));
        if (model == nullptr || skel == nullptr) return;
        if (!pause && player != nullptr) {
            const Vec3f& e = play->view.eye;
            const float dx = player->actor.world.pos.x - e.x, dy = player->actor.world.pos.y - e.y, dz = player->actor.world.pos.z - e.z;
            if (dx * dx + dy * dy + dz * dz > kSkinFarDistance * kSkinFarDistance) return;
        }
        pass.bind = SkinBindFor(skel, *model);
        if (pass.bind == nullptr) return;
        pass.model = model;
        pass.player = player;
        pass.pause = pause != 0;
        // the game keeps the hands while they hold something (the sword and shield are part of those display lists) and the sheath always
        if (player != nullptr) {
            pass.keep[sk::kLHand] = player->leftHandType != PLAYER_MODELTYPE_LH_OPEN && player->leftHandType != PLAYER_MODELTYPE_LH_CLOSED;
            pass.keep[sk::kRHand] = player->rightHandType != PLAYER_MODELTYPE_RH_OPEN && player->rightHandType != PLAYER_MODELTYPE_RH_CLOSED;
        } else {
            pass.keep[sk::kLHand] = pass.keep[sk::kRHand] = true;
        }
        pass.keep[sk::kSheath] = true;
    }
    if (pass.model == nullptr) return;
    MtxF m;
    Matrix_Push();
    Matrix_TranslateRotateZYX(pos, rot);
    Matrix_Get(&m);
    Matrix_Pop();
    pass.limb[i] = AffFromMtxF(m);
    pass.seen |= 1u << i;
    gSkinStats.limbsSeen++;
    if (!pass.keep[i]) *dList = nullptr;
    if (pass.seen == (1u << sk::kLimbCount) - 1u) {
        DrawSkinModel(play, pass);
        gSkinStats.drawn++;
        pass.model = nullptr;   // one draw per pass
    }
}

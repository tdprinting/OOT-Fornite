// Layer 2b (scripts/test_mod_functions.py): Skins.inc's affine maths that binds and poses a skin model.
// EXTRACT_SPAN mod/Royale/features/Skins.inc  struct Aff { | };
// EXTRACT mod/Royale/features/Skins.inc  AffIdentity AffMul AffInverse RotationBetween
#include <cassert>
#include <cmath>
#include <initializer_list>
// @EXTRACTED
static bool Near(const Aff& a, const Aff& b, float eps = 1e-4f) {
    for (int i = 0; i < 3; i++) {
        if (std::fabs(a.t[i] - b.t[i]) > eps) return false;
        for (int j = 0; j < 3; j++) if (std::fabs(a.r[i][j] - b.r[i][j]) > eps) return false;
    }
    return true;
}
int main() {
    const Aff I = AffIdentity();
    // A rotation (30 degrees about y), a scale and a translation.
    Aff A = {};
    const float c = std::cos(0.5236f), s = std::sin(0.5236f);
    A.r[0][0] = c * 1.5f; A.r[0][2] = s; A.r[1][1] = 0.8f; A.r[2][0] = -s * 1.5f; A.r[2][2] = c;
    A.t[0] = 12; A.t[1] = -3; A.t[2] = 40;
    Aff inv;
    assert(AffInverse(A, inv));
    assert(Near(AffMul(A, inv), I) && Near(AffMul(inv, A), I));
    assert(Near(AffMul(I, A), A) && Near(AffMul(A, I), A));
    Aff flat = {};   // no inverse
    assert(!AffInverse(flat, inv));
    // a after b: applying the product equals applying b then a.
    float v[3] = {1, 2, 3}, ab[3], bThen[3], aOfB[3];
    AffMul(A, inv).Apply(v, ab);
    inv.Apply(v, bThen); A.Apply(bThen, aOfB);
    for (int i = 0; i < 3; i++) assert(std::fabs(ab[i] - aOfB[i]) < 1e-4f && std::fabs(ab[i] - v[i]) < 1e-4f);
    // RotationBetween: v to v is no turn; x to y turns x onto y; opposite vectors turn half way round.
    float r[3][3];
    const float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, nx[3] = {-1, 0, 0};
    RotationBetween(x, x, r);
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) assert(std::fabs(r[i][j] - (i == j)) < 1e-6f);
    for (const float* b : {y, nx}) {
        RotationBetween(x, b, r);
        for (int i = 0; i < 3; i++) assert(std::fabs(r[i][0] * x[0] + r[i][1] * x[1] + r[i][2] * x[2] - b[i]) < 1e-5f);
        float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
        assert(std::fabs(det - 1) < 1e-4f);   // a rotation, not a mirror
    }
    return 0;
}

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include "basic_pitch_model.h"

// Note finding with Basic Pitch, Spotify's open-source note-transcription model (Apache License 2.0), run natively. It hears
// mono 22050 Hz audio and says, for every 256-sample frame (about 86 a second) and each of the 88 piano keys from A0, how likely
// that key is sounding (frames) and how likely a note of it starts there (onsets).
//
// The model is a small TensorFlow graph (a constant-Q transform built from convolutions, log scaling, harmonic stacking and a few
// convolution layers). basic_pitch_model.h holds it as a list of ops with their weights; this file runs that list with plain
// float32 maths, so the numbers match TensorFlow.js to about 0.0002. Run() cuts the audio into overlapping two-second windows
// exactly like the JS tool (tools/song-to-oot.html, BP.run, itself a port of @spotify/basic-pitch's inference.ts).
//
// Safe to call from any thread: the model is decoded once (a function-local static) and never changed after that.
namespace royale::bp {

struct Output {
    std::vector<std::array<float, 88>> frames, onsets;
};

namespace detail {

constexpr int kRate = 22050, kHop = 256, kFps = kRate / kHop; // 86 frames a second
constexpr int kWindow = kRate * 2 - kHop;                     // 43844 samples the model takes at once
constexpr int kOverlapFrames = 30, kHalfOverlap = kOverlapFrames / 2, kOverlapLen = kOverlapFrames * kHop;
constexpr int kWindowStep = kWindow - kOverlapLen;

struct Tensor {
    std::vector<int> shape;
    std::vector<float> data;
};

inline size_t Count(const std::vector<int>& shape) {
    size_t n = 1;
    for (int d : shape) n *= static_cast<size_t>(d);
    return n;
}

// Row-major strides, in elements.
inline std::vector<size_t> Strides(const std::vector<int>& shape) {
    std::vector<size_t> s(shape.size(), 1);
    for (int i = static_cast<int>(shape.size()) - 2; i >= 0; i--) s[i] = s[i + 1] * static_cast<size_t>(shape[i + 1]);
    return s;
}

inline float HalfToFloat(uint16_t h) {
    const int e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp(static_cast<float>(m), -24);
    else if (e == 31) v = m ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else v = std::ldexp(static_cast<float>(m | 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}

// Builds an output by picking, along each dim, a list of input indices (-1 = a zero, for padding). Used for slices and pads.
inline Tensor Gather(const Tensor& x, const std::vector<std::vector<int>>& pick) {
    Tensor out;
    for (const auto& p : pick) out.shape.push_back(static_cast<int>(p.size()));
    out.data.resize(Count(out.shape));
    const std::vector<size_t> st = Strides(x.shape);
    const int rank = static_cast<int>(pick.size());
    if (out.data.empty()) return out;
    if (rank == 0) { out.data[0] = x.data[0]; return out; }
    float* o = out.data.data();
    // walk every output element in order, carrying the input offset of each dim
    std::function<void(int, size_t, bool)> walk = [&](int d, size_t base, bool zero) {
        for (int idx : pick[d]) {
            const bool z = zero || idx < 0;
            const size_t at = z ? 0 : base + static_cast<size_t>(idx) * st[d];
            if (d + 1 == rank) *o++ = z ? 0.0f : x.data[at];
            else walk(d + 1, at, z);
        }
    };
    walk(0, 0, false);
    return out;
}

inline std::vector<int> Ints(const Tensor& t) {
    std::vector<int> v;
    for (float f : t.data) v.push_back(static_cast<int>(std::lround(f)));
    return v;
}

inline Tensor Transpose(const Tensor& x, const std::vector<int>& perm) {
    const int rank = static_cast<int>(perm.size());
    const std::vector<size_t> inStride = Strides(x.shape);
    Tensor out;
    std::vector<size_t> st(rank);
    for (int i = 0; i < rank; i++) { out.shape.push_back(x.shape[perm[i]]); st[i] = inStride[perm[i]]; }
    out.data.resize(Count(out.shape));
    if (out.data.empty()) return out;
    std::vector<int> idx(rank, 0);
    size_t at = 0;
    for (float& o : out.data) {
        o = x.data[at];
        for (int d = rank - 1; d >= 0; d--) { // step the output index like an odometer
            at += st[d];
            if (++idx[d] < out.shape[d]) break;
            at -= st[d] * static_cast<size_t>(out.shape[d]);
            idx[d] = 0;
        }
    }
    return out;
}

// TensorFlow's StridedSlice without the ellipsis mask (the model never uses it).
inline Tensor StridedSlice(const Tensor& x, const std::vector<int>& begin, const std::vector<int>& end, const std::vector<int>& step,
                           int beginMask, int endMask, int newAxisMask, int shrinkMask) {
    std::vector<std::vector<int>> pick;
    std::vector<int> shape;
    size_t dim = 0;
    for (size_t i = 0; i < begin.size(); i++) {
        if (newAxisMask & (1 << i)) { shape.push_back(1); continue; }
        const int n = x.shape[dim], s = step[i];
        std::vector<int> p;
        if (shrinkMask & (1 << i)) {
            p.push_back(begin[i] < 0 ? begin[i] + n : begin[i]);
        } else if (s > 0) {
            int b = (beginMask & (1 << i)) ? 0 : (begin[i] < 0 ? begin[i] + n : begin[i]);
            int e = (endMask & (1 << i)) ? n : (end[i] < 0 ? end[i] + n : end[i]);
            b = std::clamp(b, 0, n), e = std::clamp(e, 0, n);
            for (int k = b; k < e; k += s) p.push_back(k);
        } else {
            int b = (beginMask & (1 << i)) ? n - 1 : (begin[i] < 0 ? begin[i] + n : begin[i]);
            int e = (endMask & (1 << i)) ? -1 : (end[i] < 0 ? end[i] + n : end[i]);
            b = std::clamp(b, -1, n - 1), e = std::clamp(e, -1, n - 1);
            for (int k = b; k > e; k += s) p.push_back(k);
        }
        if (!(shrinkMask & (1 << i))) shape.push_back(static_cast<int>(p.size()));
        pick.push_back(std::move(p));
        dim++;
    }
    for (; dim < x.shape.size(); dim++) { // dims past the slice spec are kept whole
        std::vector<int> p(x.shape[dim]);
        for (int k = 0; k < x.shape[dim]; k++) p[k] = k;
        shape.push_back(x.shape[dim]);
        pick.push_back(std::move(p));
    }
    Tensor out = Gather(x, pick);
    out.shape = shape; // new axes and shrunk dims do not change the element order
    return out;
}

// Pad (zeros) and MirrorPad (mode 0 reflect, 1 symmetric); paddings is [rank][2].
inline Tensor Pad(const Tensor& x, const std::vector<int>& paddings, int mirror) {
    std::vector<std::vector<int>> pick(x.shape.size());
    for (size_t d = 0; d < x.shape.size(); d++) {
        const int n = x.shape[d];
        for (int i = -paddings[2 * d]; i < n + paddings[2 * d + 1]; i++) {
            int k = i;
            if (mirror < 0) k = (i < 0 || i >= n) ? -1 : i;
            else if (i < 0) k = mirror == 0 ? -i : -i - 1;
            else if (i >= n) k = mirror == 0 ? 2 * n - 2 - i : 2 * n - 1 - i;
            pick[d].push_back(k);
        }
    }
    return Gather(x, pick);
}

// Pack (a new axis) and ConcatV2 (an existing one): copy each input's block per outer index.
inline Tensor Join(const std::vector<const Tensor*>& xs, int axis, bool newAxis) {
    std::vector<int> shape = xs[0]->shape;
    const int rank = static_cast<int>(shape.size()) + (newAxis ? 1 : 0);
    if (axis < 0) axis += rank;
    Tensor out;
    out.shape = shape;
    if (newAxis) out.shape.insert(out.shape.begin() + axis, static_cast<int>(xs.size()));
    else {
        out.shape[axis] = 0;
        for (const Tensor* t : xs) out.shape[axis] += t->shape[axis];
    }
    size_t outer = 1;
    for (int d = 0; d < axis; d++) outer *= static_cast<size_t>(shape[d]);
    out.data.resize(Count(out.shape));
    float* o = out.data.data();
    for (size_t i = 0; i < outer; i++)
        for (const Tensor* t : xs) {
            const size_t block = outer ? t->data.size() / outer : 0;
            std::memcpy(o, t->data.data() + i * block, block * sizeof(float));
            o += block;
        }
    return out;
}

// Element-wise binary op with numpy-style broadcasting.
template <class F> Tensor Binary(const Tensor& a, const Tensor& b, F f) {
    Tensor out;
    if (a.shape == b.shape) {
        out.shape = a.shape;
        out.data.resize(a.data.size());
        for (size_t i = 0; i < a.data.size(); i++) out.data[i] = f(a.data[i], b.data[i]);
        return out;
    }
    const size_t rank = std::max(a.shape.size(), b.shape.size());
    std::vector<int> as(rank, 1), bs(rank, 1);
    std::copy(a.shape.begin(), a.shape.end(), as.begin() + (rank - a.shape.size()));
    std::copy(b.shape.begin(), b.shape.end(), bs.begin() + (rank - b.shape.size()));
    out.shape.resize(rank);
    for (size_t d = 0; d < rank; d++) out.shape[d] = std::max(as[d], bs[d]);
    const std::vector<size_t> ast = Strides(as), bst = Strides(bs);
    std::vector<size_t> sa(rank), sb(rank); // broadcast dims step by 0
    for (size_t d = 0; d < rank; d++) { sa[d] = as[d] == 1 ? 0 : ast[d]; sb[d] = bs[d] == 1 ? 0 : bst[d]; }
    out.data.resize(Count(out.shape));
    if (out.data.empty()) return out;
    std::vector<int> idx(rank, 0);
    size_t ia = 0, ib = 0;
    for (float& o : out.data) {
        o = f(a.data[ia], b.data[ib]);
        for (int d = static_cast<int>(rank) - 1; d >= 0; d--) {
            ia += sa[d], ib += sb[d];
            if (++idx[d] < out.shape[d]) break;
            ia -= sa[d] * static_cast<size_t>(out.shape[d]), ib -= sb[d] * static_cast<size_t>(out.shape[d]);
            idx[d] = 0;
        }
    }
    return out;
}

// Sum, Min or Max over some axes.
template <class F> Tensor Reduce(const Tensor& x, std::vector<int> axes, bool keepDims, float init, F f) {
    const int rank = static_cast<int>(x.shape.size());
    std::vector<bool> reduced(rank, false);
    for (int a : axes) reduced[a < 0 ? a + rank : a] = true;
    std::vector<int> kept(rank);
    Tensor out;
    for (int d = 0; d < rank; d++) {
        kept[d] = reduced[d] ? 1 : x.shape[d];
        if (!reduced[d] || keepDims) out.shape.push_back(kept[d]);
    }
    const std::vector<size_t> ks = Strides(kept);
    std::vector<size_t> so(rank);
    for (int d = 0; d < rank; d++) so[d] = reduced[d] ? 0 : ks[d];
    out.data.assign(Count(kept), init);
    if (x.data.empty()) return out;
    std::vector<int> idx(rank, 0);
    size_t at = 0;
    for (float v : x.data) {
        out.data[at] = f(out.data[at], v);
        for (int d = rank - 1; d >= 0; d--) {
            at += so[d];
            if (++idx[d] < x.shape[d]) break;
            at -= so[d] * static_cast<size_t>(x.shape[d]);
            idx[d] = 0;
        }
    }
    return out;
}

// NHWC convolution with HWIO weights, then optionally a bias and a ReLU. Done in tiles of kTile output pixels: the patches under a
// tile are copied into columns (im2col), then each output channel is a sum of weight times column. The inner loops run along the
// tile with a fixed length into local sums, which compilers vectorize even at -O2.
inline Tensor Conv2D(const Tensor& x, const Tensor& w, const Tensor* bias, int sh, int sw, bool same, bool relu) {
    constexpr int kTile = 64;
    const int N = x.shape[0], H = x.shape[1], W = x.shape[2], C = x.shape[3];
    const int KH = w.shape[0], KW = w.shape[1], CO = w.shape[3];
    int outH = (H - KH) / sh + 1, outW = (W - KW) / sw + 1, padT = 0, padB = 0, padL = 0, padR = 0;
    if (same) {
        outH = (H + sh - 1) / sh, outW = (W + sw - 1) / sw;
        const int padH = std::max((outH - 1) * sh + KH - H, 0), padW = std::max((outW - 1) * sw + KW - W, 0);
        padT = padH / 2, padB = padH - padT, padL = padW / 2, padR = padW - padL;
    }
    // a zero-padded copy of the input (SAME only), so copying patches needs no bounds checks
    const int Hp = H + padT + padB, Wp = W + padL + padR;
    std::vector<float> padded;
    if (Hp != H || Wp != W) {
        padded.assign(static_cast<size_t>(N) * Hp * Wp * C, 0.0f);
        for (int n = 0; n < N; n++)
            for (int h = 0; h < H; h++)
                std::memcpy(&padded[((static_cast<size_t>(n) * Hp + h + padT) * Wp + padL) * C], &x.data[(static_cast<size_t>(n) * H + h) * W * C],
                            static_cast<size_t>(W) * C * sizeof(float));
    }
    const float* src = padded.empty() ? x.data.data() : padded.data();

    Tensor out;
    out.shape = {N, outH, outW, CO};
    out.data.resize(Count(out.shape));
    const int K = KH * KW * C, total = outH * outW;
    std::vector<float> col(static_cast<size_t>(K) * kTile);
    std::vector<size_t> patchAt(K); // where each column row's value sits relative to the patch's corner
    for (int kh = 0, k = 0; kh < KH; kh++)
        for (int kw = 0; kw < KW; kw++)
            for (int c = 0; c < C; c++) patchAt[k++] = (static_cast<size_t>(kh) * Wp + kw) * C + c;
    size_t corner[kTile];
    for (int n = 0; n < N; n++) {
        const float* in = src + static_cast<size_t>(n) * Hp * Wp * C;
        for (int p0 = 0; p0 < total; p0 += kTile) {
            const int np = std::min(kTile, total - p0);
            for (int j = 0; j < kTile; j++) { // pixels past the end repeat the last one; their sums are not stored
                const int p = p0 + std::min(j, np - 1);
                corner[j] = (static_cast<size_t>(p / outW) * sh * Wp + static_cast<size_t>(p % outW) * sw) * C;
            }
            for (int k = 0; k < K; k++) {
                const float* from = in + patchAt[k];
                float* row = col.data() + static_cast<size_t>(k) * kTile;
                for (int j = 0; j < kTile; j++) row[j] = from[corner[j]];
            }
            float* o = out.data.data() + (static_cast<size_t>(n) * total + p0) * CO;
            auto store = [&](const float* acc, int c) {
                const float b = bias ? bias->data[c] : 0.0f;
                for (int j = 0; j < np; j++) o[static_cast<size_t>(j) * CO + c] = relu ? std::max(acc[j] + b, 0.0f) : acc[j] + b;
            };
            int co = 0;
            for (; co + 4 <= CO; co += 4) { // four output channels per pass over the columns
                float a0[kTile] = {}, a1[kTile] = {}, a2[kTile] = {}, a3[kTile] = {};
                for (int k = 0; k < K; k++) {
                    const float* wk = w.data.data() + static_cast<size_t>(k) * CO + co;
                    const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3];
                    const float* c = col.data() + static_cast<size_t>(k) * kTile;
                    for (int j = 0; j < kTile; j++) {
                        const float v = c[j];
                        a0[j] += w0 * v, a1[j] += w1 * v, a2[j] += w2 * v, a3[j] += w3 * v;
                    }
                }
                store(a0, co), store(a1, co + 1), store(a2, co + 2), store(a3, co + 3);
            }
            for (; co < CO; co++) {
                float a[kTile] = {};
                for (int k = 0; k < K; k++) {
                    const float wv = w.data[static_cast<size_t>(k) * CO + co];
                    const float* c = col.data() + static_cast<size_t>(k) * kTile;
                    for (int j = 0; j < kTile; j++) a[j] += wv * c[j];
                }
                store(a, co);
            }
        }
    }
    return out;
}

struct Node {
    Op op;
    std::vector<int> in, attr;
    int uses = 0; // how many later nodes read this one
};

struct Model {
    std::vector<Node> nodes;
    std::vector<Tensor> consts; // by node, filled for Const nodes only
};

// Decodes basic_pitch_model.h once.
inline const Model& GetModel() {
    static const Model model = [] {
        Model m;
        const int32_t* p = kProgram;
        for (int i = 0; i < kNodes; i++) {
            Node n;
            n.op = static_cast<Op>(*p++);
            n.in.assign(p + 1, p + 1 + *p), p += 1 + *p;
            n.attr.assign(p + 1, p + 1 + *p), p += 1 + *p;
            for (int j : n.in) m.nodes[j].uses++;
            m.nodes.push_back(std::move(n));
        }
        m.consts.resize(kNodes);
        for (int i = 0; i < kNodes; i++) {
            if (m.nodes[i].op != Op::Const) continue;
            const int32_t* row = kConsts + 8 * m.nodes[i].attr[0];
            Tensor& t = m.consts[i];
            t.shape.assign(row + 2, row + 2 + row[1]);
            t.data.resize(Count(t.shape));
            const float scale = std::ldexp(1.0f, -row[7]);
            for (size_t k = 0; k < t.data.size(); k++) {
                const size_t at = static_cast<size_t>(row[6]) + k;
                if (row[0] == 0) t.data[k] = static_cast<float>(kInts[at]);
                else if (row[0] == 1) t.data[k] = HalfToFloat(kHalves[at]) * scale;
                else std::memcpy(&t.data[k], &kFloats[at], sizeof(float));
            }
        }
        return m;
    }();
    return model;
}

// Runs the graph on one window of kWindow samples; returns the frames and onsets tensors, each [1, 172, 88].
inline std::pair<Tensor, Tensor> RunWindow(const Model& m, const float* window) {
    std::vector<Tensor> vals(m.nodes.size());
    std::vector<int> left(m.nodes.size());
    for (size_t i = 0; i < m.nodes.size(); i++) left[i] = m.nodes[i].uses + (static_cast<int>(i) == kFramesNode || static_cast<int>(i) == kOnsetsNode);
    auto get = [&](int i) -> const Tensor& { return m.nodes[i].op == Op::Const ? m.consts[i] : vals[i]; };
    // the first input, moved instead of copied when nothing else needs it (for the ops that only change the shape)
    auto take = [&](const Node& n) -> Tensor {
        const int i = n.in[0];
        if (m.nodes[i].op != Op::Const && left[i] == 1) return std::move(vals[i]);
        return get(i);
    };
    for (size_t ni = 0; ni < m.nodes.size(); ni++) {
        const Node& n = m.nodes[ni];
        Tensor& out = vals[ni];
        auto in = [&](int k) -> const Tensor& { return get(n.in[k]); };
        switch (n.op) {
        case Op::Input:
            out.shape = {1, kWindow, 1};
            out.data.assign(window, window + kWindow);
            break;
        case Op::Const: break;
        case Op::Identity: out = take(n); break;
        case Op::Transpose: out = Transpose(in(0), Ints(in(1))); break;
        case Op::ExpandDims: {
            int d = Ints(in(1))[0];
            out = take(n);
            if (d < 0) d += static_cast<int>(out.shape.size()) + 1;
            out.shape.insert(out.shape.begin() + d, 1);
            break;
        }
        case Op::Squeeze: {
            out = take(n);
            const int rank = static_cast<int>(out.shape.size());
            std::vector<bool> drop(rank, n.attr.empty());
            for (int d : n.attr) drop[d < 0 ? d + rank : d] = true;
            std::vector<int> shape;
            for (int d = 0; d < rank; d++)
                if (!(drop[d] && out.shape[d] == 1)) shape.push_back(out.shape[d]);
            out.shape = shape;
            break;
        }
        case Op::Reshape: {
            std::vector<int> shape = Ints(in(1));
            out = take(n);
            size_t known = 1;
            int unknown = -1;
            for (size_t d = 0; d < shape.size(); d++) {
                if (shape[d] < 0) unknown = static_cast<int>(d);
                else known *= static_cast<size_t>(shape[d]);
            }
            if (unknown >= 0) shape[unknown] = known ? static_cast<int>(out.data.size() / known) : 0;
            out.shape = shape;
            break;
        }
        case Op::StridedSlice:
            out = StridedSlice(in(0), Ints(in(1)), Ints(in(2)), Ints(in(3)), n.attr[0], n.attr[1], n.attr[2], n.attr[3]);
            break;
        case Op::Pad: out = Pad(in(0), Ints(in(1)), -1); break;
        case Op::MirrorPad: out = Pad(in(0), Ints(in(1)), n.attr[0]); break;
        case Op::Pack:
        case Op::ConcatV2: {
            const bool pack = n.op == Op::Pack;
            const size_t count = n.in.size() - (pack ? 0 : 1);
            std::vector<const Tensor*> xs;
            for (size_t k = 0; k < count; k++) xs.push_back(&in(static_cast<int>(k)));
            out = Join(xs, pack ? n.attr[0] : Ints(in(static_cast<int>(count)))[0], pack);
            break;
        }
        case Op::Shape:
            out.shape = {static_cast<int>(in(0).shape.size())};
            for (int d : in(0).shape) out.data.push_back(static_cast<float>(d));
            break;
        case Op::Neg:
        case Op::Square:
        case Op::Sqrt:
        case Op::Log:
        case Op::Sigmoid: {
            out = take(n);
            for (float& v : out.data) {
                switch (n.op) {
                case Op::Neg: v = -v; break;
                case Op::Square: v = v * v; break;
                case Op::Sqrt: v = std::sqrt(v); break;
                case Op::Log: v = std::log(v); break;
                default: v = 1.0f / (1.0f + std::exp(-v)); break;
                }
            }
            break;
        }
        case Op::Add: out = Binary(in(0), in(1), [](float a, float b) { return a + b; }); break;
        case Op::Sub: out = Binary(in(0), in(1), [](float a, float b) { return a - b; }); break;
        case Op::Mul: out = Binary(in(0), in(1), [](float a, float b) { return a * b; }); break;
        case Op::DivNoNan: out = Binary(in(0), in(1), [](float a, float b) { return b == 0.0f ? 0.0f : a / b; }); break;
        case Op::Sum: out = Reduce(in(0), Ints(in(1)), n.attr[0] != 0, 0.0f, [](float a, float b) { return a + b; }); break;
        case Op::Min:
            out = Reduce(in(0), Ints(in(1)), n.attr[0] != 0, std::numeric_limits<float>::infinity(), [](float a, float b) { return std::min(a, b); });
            break;
        case Op::Max:
            out = Reduce(in(0), Ints(in(1)), n.attr[0] != 0, -std::numeric_limits<float>::infinity(), [](float a, float b) { return std::max(a, b); });
            break;
        case Op::Conv2D:
            out = Conv2D(in(0), in(1), n.attr[3] >= 1 ? &in(2) : nullptr, n.attr[0], n.attr[1], n.attr[2] != 0, n.attr[3] >= 2);
            break;
        }
        for (int i : n.in) // free what nothing reads any more
            if (--left[i] == 0) vals[i] = Tensor{};
    }
    return {std::move(vals[kFramesNode]), std::move(vals[kOnsetsNode])};
}

} // namespace detail

// Frames and onsets for mono 22050 Hz audio: floor(n * 86 / 22050) rows of 88 keys (A0 up), each 0..1. progress is told how far
// along it is (0..1); returning false stops early and gives back the rows done so far.
inline Output Run(const float* audio, size_t n, const std::function<bool(float)>& progress = {}) {
    using namespace detail;
    const Model& m = GetModel();
    Output out;
    const size_t nOut = static_cast<size_t>(std::floor(static_cast<double>(n) * (static_cast<double>(kFps) / kRate)));
    // half an overlap of silence in front, then windows that step by kWindowStep and drop kHalfOverlap frames at each end
    const size_t padded = kOverlapLen / 2 + n;
    const size_t windows = (padded + kWindowStep - 1) / kWindowStep + 1;
    std::vector<float> chunk(kWindow);
    for (size_t w = 0; w < windows && out.frames.size() < nOut; w++) {
        if (progress && !progress(static_cast<float>(w) / static_cast<float>(windows))) return out;
        // chunk[i] = audio[w * kWindowStep + i - kOverlapLen / 2], zero outside the audio
        std::fill(chunk.begin(), chunk.end(), 0.0f);
        const long long start = static_cast<long long>(w * kWindowStep) - kOverlapLen / 2;
        for (int i = 0; i < kWindow; i++) {
            const long long at = start + i;
            if (at >= 0 && at < static_cast<long long>(n)) chunk[i] = audio[at];
        }
        const auto [fr, on] = RunWindow(m, chunk.data());
        const int rows = fr.shape[1], keys = fr.shape[2];
        for (int r = kHalfOverlap; r < rows - kHalfOverlap && out.frames.size() < nOut; r++) {
            std::array<float, 88> f{}, o{};
            for (int k = 0; k < keys && k < 88; k++) {
                f[k] = fr.data[static_cast<size_t>(r) * keys + k];
                o[k] = on.data[static_cast<size_t>(r) * keys + k];
            }
            out.frames.push_back(f);
            out.onsets.push_back(o);
        }
    }
    if (progress) progress(1.0f);
    return out;
}

} // namespace royale::bp

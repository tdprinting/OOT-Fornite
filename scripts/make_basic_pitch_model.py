#!/usr/bin/env python3
"""Turns Spotify's Basic Pitch TF.js graph model (model.json + its .bin shard) into shared/basic_pitch_model.h, which
shared/basic_pitch.h runs without a JSON parser or TensorFlow.

The header holds the graph as a flat list of ops (only what the note and onset outputs need, with repeated constants and
repeated ops merged), the small integer constants (shapes, slices, paddings) and the weights.

Weights are stored as float32 (the outputs then match TensorFlow.js to about 0.0002). --half stores them as float16 with a
power-of-two scale per tensor instead, for a header about a third smaller; but the model is touchy on quiet passages (its
log-scaled spectrogram magnifies tiny errors) and float16 weights put frames and onsets up to 0.14 away from TensorFlow.js.

Usage: python3 scripts/make_basic_pitch_model.py path/to/model.json [shared/basic_pitch_model.h] [--half]

Basic Pitch model (c) 2022 Spotify AB, Apache License 2.0.
"""
import base64
import json
import math
import os
import sys

import numpy as np

OPS = ["Input", "Const", "Identity", "Transpose", "ExpandDims", "Squeeze", "Reshape", "StridedSlice", "Pad", "MirrorPad",
       "Pack", "ConcatV2", "Shape", "Neg", "Square", "Sqrt", "Log", "Sigmoid", "Add", "Sub", "Mul", "DivNoNan", "Sum", "Min",
       "Max", "Conv2D"]
ALIASES = {"Placeholder": "Input", "AddV2": "Add", "_FusedConv2D": "Conv2D"}
OUTPUTS = ["Identity_1", "Identity_2"]  # note frames, onsets (as the JS run() asks for them)
KIND_INT, KIND_HALF, KIND_FLOAT = 0, 1, 2


def attr_int(node, key, default=0):
    a = node.get("attr", {}).get(key)
    return int(a["i"]) if a and "i" in a else default


def attr_str(node, key):
    a = node.get("attr", {}).get(key)
    return base64.b64decode(a["s"]).decode() if a and "s" in a else ""


def attr_ints(node, key):
    a = node.get("attr", {}).get(key)
    return [int(v) for v in a["list"].get("i", [])] if a and "list" in a else []


def attr_strs(node, key):
    a = node.get("attr", {}).get(key)
    return [base64.b64decode(v).decode() for v in a["list"].get("s", [])] if a and "list" in a else []


def op_attrs(node, op):
    """The integer attributes the C++ needs, in a fixed order per op."""
    if op == "Conv2D":
        strides, dil = attr_ints(node, "strides"), attr_ints(node, "dilations")
        assert attr_str(node, "data_format") in ("", "NHWC") and (not dil or dil == [1, 1, 1, 1])
        assert strides[0] == 1 and strides[3] == 1
        pad = attr_str(node, "padding")
        assert pad in ("SAME", "VALID")
        fused = attr_strs(node, "fused_ops")
        assert fused in ([], ["BiasAdd"], ["BiasAdd", "Relu"]), fused
        return [strides[1], strides[2], 1 if pad == "SAME" else 0, len(fused)]
    if op == "StridedSlice":
        assert attr_int(node, "ellipsis_mask") == 0
        return [attr_int(node, k) for k in ("begin_mask", "end_mask", "new_axis_mask", "shrink_axis_mask")]
    if op == "Pack":
        return [attr_int(node, "axis")]
    if op == "Squeeze":
        return attr_ints(node, "squeeze_dims")
    if op in ("Sum", "Min", "Max"):
        a = node.get("attr", {}).get("keep_dims")
        return [1 if a and a.get("b") else 0]
    if op == "MirrorPad":
        mode = attr_str(node, "mode")
        assert mode in ("REFLECT", "SYMMETRIC")
        return [0 if mode == "REFLECT" else 1]
    return []


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    use_half = "--half" in sys.argv
    if not args:
        sys.exit(__doc__)
    model_path = args[0]
    out_path = args[1] if len(args) > 1 else os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "shared", "basic_pitch_model.h"))

    model = json.load(open(model_path))
    manifest = model["weightsManifest"][0]
    blob = b"".join(open(os.path.join(os.path.dirname(model_path), p), "rb").read() for p in manifest["paths"])
    weights, off = {}, 0
    for w in manifest["weights"]:
        assert "quantization" not in w and w["dtype"] in ("float32", "int32")
        n = int(np.prod(w["shape"])) if w["shape"] else 1
        weights[w["name"]] = np.frombuffer(blob, dtype=np.float32 if w["dtype"] == "float32" else np.int32, count=n,
                                           offset=off).reshape(w["shape"])
        off += 4 * n

    nodes = {n["name"]: n for n in model["modelTopology"]["node"]}

    def inputs_of(n):
        return [i.split(":")[0] for i in n.get("input", []) if not i.startswith("^")]

    # keep only what the outputs need, in dependency order
    order, seen = [], set()

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        for i in inputs_of(nodes[name]):
            visit(i)
        order.append(name)

    for o in OUTPUTS:
        visit(o)

    # merge equal constants, then equal ops (the two harmonic stacks are the same slices of the same tensor)
    key_to_index, index_of, program, consts = {}, {}, [], []
    for name in order:
        n = nodes[name]
        op = ALIASES.get(n["op"], n["op"])
        assert op in OPS, op
        if op == "Const":
            a = weights[name]
            key = ("c", a.dtype.str, a.shape, a.tobytes())
            payload = a
        else:
            attrs = op_attrs(n, op)
            ins = tuple(index_of[i] for i in inputs_of(n))
            key = (op, ins, tuple(attrs))
            payload = (op, ins, attrs)
        if key in key_to_index:
            index_of[name] = key_to_index[key]
            continue
        idx = len(program)
        key_to_index[key] = index_of[name] = idx
        if op == "Const":
            program.append(("Const", (), [len(consts)]))
            consts.append(payload)
        else:
            program.append(payload)

    # constant storage
    ints, halves, floats, table = [], [], [], []
    for a in consts:
        assert a.ndim <= 4
        dims = list(a.shape) + [0] * (4 - a.ndim)
        flat = a.reshape(-1)
        if a.dtype == np.int32:
            table.append([KIND_INT, a.ndim] + dims + [len(ints), 0])
            ints.extend(int(v) for v in flat)
        elif not use_half:
            table.append([KIND_FLOAT, a.ndim] + dims + [len(floats), 0])
            floats.extend(int(v) for v in flat.view(np.uint32))
        else:
            # scale by 2^e so the largest value is in [1, 2): small values stay out of float16's subnormal range
            m = float(np.abs(flat).max())
            e = -int(math.floor(math.log2(m))) if m > 0 else 0
            h = (flat.astype(np.float64) * 2.0 ** e).astype(np.float16)
            table.append([KIND_HALF, a.ndim] + dims + [len(halves), e])
            halves.extend(int(v) for v in h.view(np.uint16))

    code = []
    for op, ins, attrs in program:
        code += [OPS.index(op), len(ins), *ins, len(attrs), *attrs]

    def array(ctype, name, values, per_line=24):
        if not values:
            values = [0]
        lines = [", ".join(str(v) for v in values[i:i + per_line]) for i in range(0, len(values), per_line)]
        return "inline constexpr %s %s[] = {\n    %s,\n};\n" % (ctype, name, ",\n    ".join(lines))

    out = []
    out.append("// Basic Pitch model (c) 2022 Spotify AB, Apache License 2.0 (https://github.com/spotify/basic-pitch,\n"
               "// http://www.apache.org/licenses/LICENSE-2.0). Converted from its TensorFlow.js graph model.\n"
               "// Generated by scripts/make_basic_pitch_model.py; do not edit by hand. Run by shared/basic_pitch.h.\n")
    out.append("#pragma once\n#include <cstdint>\n\nnamespace royale::bp {\n")
    out.append("// The ops the graph uses, in the order of the numbers in kProgram.\nenum class Op : int32_t {\n    %s,\n};\n"
               % ",\n    ".join(", ".join(OPS[i:i + 9]) for i in range(0, len(OPS), 9)))
    out.append("// The graph, one node after another: op, input count, inputs (earlier node numbers), attribute count, attributes.\n"
               "// Conv2D: stride h, stride w, SAME padding, fused ops (1 = add bias, 2 = add bias then ReLU).\n"
               "// StridedSlice: begin, end, new axis and shrink masks. Pack: axis. Squeeze: dims. Sum/Min/Max: keep dims.\n"
               "// MirrorPad: 0 = reflect, 1 = symmetric. Const: its row in kConsts.")
    out.append("inline constexpr int kNodes = %d;" % len(program))
    out.append("inline constexpr int kFramesNode = %d, kOnsetsNode = %d;" % (index_of[OUTPUTS[0]], index_of[OUTPUTS[1]]))
    out.append(array("int32_t", "kProgram", code, 32))
    out.append("// Constants: kind (0 = int in kInts, 1 = float16 in kHalves times 2^-scale, 2 = float32 bits in kFloats),\n"
               "// rank, 4 dims, offset, scale.")
    out.append("inline constexpr int kConstCount = %d;" % len(table))
    out.append(array("int32_t", "kConsts", [v for row in table for v in row], 8))
    out.append(array("int32_t", "kInts", ints, 32))
    out.append(array("uint16_t", "kHalves", halves, 24))
    out.append(array("uint32_t", "kFloats", floats, 12))
    out.append("} // namespace royale::bp\n")
    with open(out_path, "w", newline="\n") as f:
        f.write("\n".join(out))
    print("%s: %d nodes, %d constants (%d weights), %d bytes" % (out_path, len(program), len(table), len(halves) + len(floats),
                                                                  os.path.getsize(out_path)))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# Lists the operators, tensor shapes and weight types of a .tflite model with
# no third-party packages: a minimal FlatBuffers reader over the LiteRT
# schema fields it needs. Used for depth/REPORT.md's graph summary.
#   python3 -I depth/tools/tflite_ops.py assets/depth/zipdepth_512x288_fp16.tflite
import collections
import struct
import sys

# BuiltinOperator enum values (tensorflow/compiler/mlir/lite/schema/schema.fbs)
OPS = {0: "ADD", 1: "AVERAGE_POOL_2D", 2: "CONCATENATION", 3: "CONV_2D",
       4: "DEPTHWISE_CONV_2D", 5: "DEPTH_TO_SPACE", 6: "DEQUANTIZE", 9: "FULLY_CONNECTED",
       14: "LOGISTIC", 17: "MAX_POOL_2D", 18: "MUL", 19: "RELU", 21: "RELU6", 22: "RESHAPE",
       23: "RESIZE_BILINEAR", 25: "SOFTMAX", 26: "SPACE_TO_DEPTH", 28: "TANH",
       34: "PAD", 39: "TRANSPOSE", 40: "MEAN", 41: "SUB", 42: "DIV", 43: "SQUEEZE",
       45: "STRIDED_SLICE", 47: "EXP", 49: "SPLIT", 53: "CAST", 54: "PRELU", 55: "MAXIMUM",
       57: "MINIMUM", 61: "SLICE", 67: "TRANSPOSE_CONV", 74: "SUM", 75: "SQRT", 76: "RSQRT",
       77: "SHAPE", 78: "POW", 82: "REDUCE_MAX", 83: "PACK", 88: "UNPACK", 92: "SQUARE",
       97: "RESIZE_NEAREST_NEIGHBOR", 98: "LEAKY_RELU", 99: "SQUARED_DIFFERENCE",
       100: "MIRROR_PAD", 101: "ABS", 102: "SPLIT_V", 114: "QUANTIZE", 117: "HARD_SWISH",
       126: "BATCH_MATMUL", 150: "GELU"}
TYPES = {0: "f32", 1: "f16", 2: "i32", 3: "u8", 4: "i64", 9: "i8", 6: "bool"}


class Table:
    def __init__(self, buf, pos):
        self.buf, self.pos = buf, pos
        vt = pos - struct.unpack_from("<i", buf, pos)[0]
        self.vt = vt
        self.vlen = struct.unpack_from("<H", buf, vt)[0]

    def off(self, field):
        o = 4 + 2 * field
        if o >= self.vlen:
            return 0
        return struct.unpack_from("<H", self.buf, self.vt + o)[0]

    def scalar(self, field, fmt, default=0):
        o = self.off(field)
        return struct.unpack_from(fmt, self.buf, self.pos + o)[0] if o else default

    def ref(self, field):
        o = self.off(field)
        if not o:
            return None
        p = self.pos + o
        return p + struct.unpack_from("<I", self.buf, p)[0]

    def table(self, field):
        p = self.ref(field)
        return Table(self.buf, p) if p is not None else None

    def vec(self, field):
        p = self.ref(field)
        if p is None:
            return 0, 0
        return struct.unpack_from("<I", self.buf, p)[0], p + 4

    def tables(self, field):
        n, p = self.vec(field)
        return [Table(self.buf, p + 4 * i + struct.unpack_from("<I", self.buf, p + 4 * i)[0])
                for i in range(n)]

    def ints(self, field):
        n, p = self.vec(field)
        return list(struct.unpack_from("<%di" % n, self.buf, p)) if n else []

    def string(self, field):
        n, p = self.vec(field)
        return self.buf[p:p + n].decode("utf-8", "replace") if n else ""


def main(path):
    buf = open(path, "rb").read()
    model = Table(buf, struct.unpack_from("<I", buf, 0)[0])
    codes = []
    for oc in model.tables(1):
        dep = oc.scalar(0, "<b")
        code = oc.scalar(3, "<i")
        code = max(dep, code)
        name = OPS.get(code, "op%d" % code)
        if code == 32:
            name = "CUSTOM:" + oc.string(1)
        codes.append(name)
    sub = model.tables(2)[0]
    tensors = sub.tables(0)
    ops = sub.tables(3)
    count = collections.Counter(codes[op.scalar(0, "<I")] for op in ops)
    print("%s: %d operators" % (path, len(ops)))
    for name, n in count.most_common():
        print("  %-22s %d" % (name, n))
    for label, idx in (("input", sub.ints(1)), ("output", sub.ints(2))):
        for i in idx:
            t = tensors[i]
            print("  %s %s %s %s" % (label, t.string(3), t.ints(0),
                                     TYPES.get(t.scalar(1, "<b"), "?")))
    wt = collections.Counter(TYPES.get(t.scalar(1, "<b"), "?") for t in tensors
                             if t.scalar(2, "<I") and len(model.tables(4)) > t.scalar(2, "<I")
                             and model.tables(4)[t.scalar(2, "<I")].vec(0)[0] > 0)
    print("  constant tensors by type: %s" % dict(wt))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)

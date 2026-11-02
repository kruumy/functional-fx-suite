"""Convert After Effects presets (.ffx) from S_Shake, S_Flicker and S_BlurMoCurves to f_shake, f_flicker
and f_blurmocurves (stdlib only). Other effects in a preset are copied unchanged.

Usage: python -I sapphire_presets.py <preset.ffx or folder>... -o <out_dir>

Each converted effect starts from templates/<f_name>.ffx, a preset saved in AE with only that effect at its defaults,
so the param definitions are exactly what AE writes. Streams (values, keyframes, expressions) come from the reference
effect, matched by param label. Re-save a template whenever the effect's params change.
"""

import argparse
import copy
import struct
import sys
from pathlib import Path

TEMPLATES = Path(__file__).resolve().parent / "templates"

# reference match name -> (ours, {our label: reference label} where they differ)
EFFECTS = {
    "S_Shake": ("f_shake", {"Wrap": "Wrap X", "Mask": "Mask from Layer"}),
    "S_Flicker": ("f_flicker", {"Mask": "Mask from Layer"}),
    "S_BlurMoCurves": ("f_blurmocurves", {"Center": "Center XY", "Wrap": "Wrap X", "Mask": "Mask from Layer"}),
}

# the reference shows `normalize_scale N` params in pixels of a 1920 wide frame, value x 1920 / (2N) (its own
# defaults: X Rand Amp 0.2 -> 192, Blur Mask 0.05 -> 12); ours are frame units
PIXELS = {
    **{f"{axis} {kind} Amp": 960.0 for axis in "XYZ" for kind in ("Rand", "Wave")},
    "Blur Mask": 240.0,
}


class Chunk:
    def __init__(self, tag, data):
        self.tag, self.data = tag, data


class List:
    def __init__(self, tag, kind, children):
        self.tag, self.kind, self.children = tag, kind, children


def parse(b, off, end):
    nodes = []
    while off + 8 <= end:
        tag = b[off : off + 4].decode("latin1")
        size = struct.unpack_from(">I", b, off + 4)[0]
        body = off + 8
        if tag in ("LIST", "RIFX"):
            nodes.append(List(tag, b[body : body + 4].decode("latin1"), parse(b, body + 4, body + size)))
        else:
            nodes.append(Chunk(tag, b[body : body + size]))
        off = body + size + (size & 1)
    return nodes


def serialize(nodes):
    out = bytearray()
    for n in nodes:
        body = n.kind.encode("latin1") + serialize(n.children) if isinstance(n, List) else n.data
        out += n.tag.encode("latin1") + struct.pack(">I", len(body)) + body + b"\0" * (len(body) & 1)
    return bytes(out)


def read(path):
    b = Path(path).read_bytes()
    if b[:4] != b"RIFX" or b[8:12] != b"FaFX":
        raise ValueError(f"{path}: not an After Effects preset")
    end = 8 + struct.unpack_from(">I", b, 4)[0]
    # AE appends XMP metadata after the RIFX tree
    return parse(b, 0, end)[0], b[end + (end & 1) :]


def write(path, root, tail):
    Path(path).write_bytes(serialize([root]) + tail)


def lists(node, kind):
    return [c for c in node.children if isinstance(c, List) and c.kind == kind]


def match_name(chunk):
    return chunk.data.split(b"\0")[0].decode("latin1")


def renamed(chunk, name):
    return Chunk(chunk.tag, name.encode("latin1").ljust(len(chunk.data), b"\0"))


def text(chunk):
    """The string in a tdsn / fnam / pdnm chunk (a nested Utf8 chunk)."""
    inner = parse(chunk.data, 0, len(chunk.data))
    return inner[0].data.decode("utf-8") if inner and inner[0].tag == "Utf8" else ""


def with_text(chunk, s):
    return Chunk(chunk.tag, serialize([Chunk("Utf8", s.encode("utf-8"))]))


def display_name(name):
    """A reference default name ("S_Shake", "S_Shake 2") with our effect name, any other name unchanged."""
    for ref_name, (ours_name, _) in EFFECTS.items():
        if name == ref_name or name.startswith(ref_name + " "):
            return ours_name + name[len(ref_name) :]
    return name


def effect_of(sspc):
    """The effect match name of an sspc (one effect's description and streams)."""
    for c in lists(sspc, "tdgp")[0].children:
        if c.tag == "tdmn":
            return match_name(c).rsplit("-", 1)[0]
    return None


def labels(sspc):
    """{param label: param match name} from an sspc's param descriptions."""
    out, last = {}, None
    for c in lists(sspc, "parT")[0].children:
        if c.tag == "tdmn":
            last = match_name(c)
        elif c.tag == "pard":
            label = c.data[16:48].split(b"\0")[0].decode("latin1")
            if label:
                out.setdefault(label, last)
    return out


def streams(tdgp):
    """{match name: stream node} for the tdmn / value pairs of a tdgp."""
    out, it = {}, iter(tdgp.children)
    for c in it:
        if c.tag == "tdmn":
            out[match_name(c)] = next(it, None)
    return out


def value(tdbs):
    """The static value of a one-dimensional stream, None when keyframed."""
    for c in tdbs.children:
        if c.tag == "cdat" and len(c.data) >= 8:
            return struct.unpack_from(">d", c.data)[0]
    return None


def with_value(tdbs, v):
    out = copy.deepcopy(tdbs)
    for c in out.children:
        if c.tag == "cdat":
            c.data = struct.pack(">d", v) + c.data[8:]
    return out


def scaled(tdbs, k):
    """A one-dimensional stream with its value, keyframe values and keyframe speeds times k."""
    for c in tdbs.children:
        if c.tag == "cdat":
            c.data = struct.pack(">d", struct.unpack_from(">d", c.data)[0] * k) + c.data[8:]
        elif isinstance(c, List) and c.kind == "list":
            head = next(h.data for h in c.children if h.tag == "lhd3")
            keys, size = struct.unpack_from(">I", head, 8)[0], struct.unpack_from(">I", head, 16)[0]
            for d in (d for d in c.children if d.tag == "ldat"):
                data = bytearray(d.data)
                # a 48 byte key: flags, value, in speed, in influence, out speed, out influence
                for at in (i * size + o for i in range(keys) for o in (8, 16, 32)):
                    struct.pack_into(">d", data, at, struct.unpack_from(">d", data, at)[0] * k)
                d.data = bytes(data)
    return tdbs


def merged(ours, ref):
    """The reference stream's data with our label and slider range."""
    keep = {c.tag: c for c in ours.children if isinstance(c, Chunk) and c.tag in ("tdsn", "tdum", "tduM")}
    ranges = [c for c in ref.children if c.tag in ("tdum", "tduM") and c.tag not in keep]
    return List(ours.tag, ours.kind, [copy.deepcopy(keep.get(c.tag, c)) for c in ref.children if c not in ranges])


def convert(sspc, template, ref_labels, warn):
    """`ref_labels` come from the first instance: AE leaves the param descriptions of later ones of the same effect
    empty."""
    ref_name = effect_of(sspc)
    ours_name, renames = EFFECTS[ref_name]
    out = copy.deepcopy(template)
    our_labels = labels(out)
    ref_tdgp, out_tdgp = lists(sspc, "tdgp")[0], lists(out, "tdgp")[0]
    ref_streams = streams(ref_tdgp)

    def ref_stream(label):
        name = ref_labels.get(renames.get(label, label))
        return ref_streams.get(name) if name else None

    by_name = {name: label for label, name in our_labels.items()}
    children = out_tdgp.children
    for i, c in enumerate(children[:-1]):
        if c.tag != "tdmn":
            continue
        name = match_name(c)
        node = children[i + 1]
        if name == "ADBE Effect Built In Params":
            ref = ref_streams.get(name)
            if ref is not None:
                children[i + 1] = copy.deepcopy(ref)
            continue
        label = by_name.get(name)
        if label is None or not isinstance(node, List) or node.kind != "tdbs":
            continue
        if ours_name == "f_blurmocurves" and label == "Blur Only":
            # the reference's Effect popup: 1 Transform and Blur, 2 Blur Only
            ref = ref_stream("Effect")
            v = value(ref) if ref is not None else None
            if ref is not None and v is None:
                warn("Effect is keyframed; Blur Only keeps its default")
            if v is not None:
                children[i + 1] = with_value(node, 1.0 if round(v) == 2 else 0.0)
            continue
        ref = ref_stream(label)
        if ref is not None and isinstance(ref, List) and ref.kind == "tdbs":
            children[i + 1] = merged(node, ref)
            if label in PIXELS:
                scaled(children[i + 1], 1.0 / PIXELS[label])

    wrap_x, wrap_y = ref_stream("Wrap X"), ref_stream("Wrap Y")
    if wrap_x is not None and wrap_y is not None and value(wrap_x) != value(wrap_y):
        warn("Wrap X and Wrap Y differ; Wrap takes Wrap X")

    name = next(text(c) for c in ref_tdgp.children if c.tag == "tdsn")
    j = next(j for j, c in enumerate(out_tdgp.children) if c.tag == "tdsn")
    out_tdgp.children[j] = with_text(out_tdgp.children[j], display_name(name))
    if not lists(sspc, "parT")[0].children:
        lists(out, "parT")[0].children = []
    return out


def load_template(ours_name):
    path = TEMPLATES / f"{ours_name}.ffx"
    if not path.is_file():
        raise SystemExit(f"missing template {path}: save a preset of {ours_name} at its defaults from AE there")
    root, _ = read(path)
    besc = lists(root, "besc")[0]
    found = [s for s in lists(besc, "sspc") if effect_of(s) == ours_name]
    if len(found) != 1:
        raise SystemExit(f"{path}: expected exactly one {ours_name}")
    return found[0]


def convert_file(src, dst, templates):
    root, tail = read(src)
    besc = lists(root, "besc")[0]
    converted, ref_labels = [], {}

    def warn(msg):
        print(f"  warning: {src.name}: {msg}")

    for i, c in enumerate(besc.children):
        if isinstance(c, List) and c.kind == "sspc" and effect_of(c) in EFFECTS:
            ref_name = effect_of(c)
            ours_name = EFFECTS[ref_name][0]
            if ours_name not in templates:
                templates[ours_name] = load_template(ours_name)
            ref_labels.setdefault(ref_name, labels(c))
            besc.children[i] = convert(c, templates[ours_name], ref_labels[ref_name], warn)
            converted.append(ours_name)
    if not converted:
        return []

    # the selection paths (ADBE Effect Parade -> effect) and the display name after each
    for i, c in enumerate(besc.children):
        if isinstance(c, List) and c.kind == "tdsp":
            for tdsi in lists(c, "tdsi"):
                for j, t in enumerate(tdsi.children):
                    if t.tag == "tdmn" and match_name(t) in EFFECTS:
                        tdsi.children[j] = renamed(t, EFFECTS[match_name(t)][0])
        elif c.tag == "tdsn" and display_name(text(c)) != text(c):
            besc.children[i] = with_text(c, display_name(text(c)))

    dst.parent.mkdir(parents=True, exist_ok=True)
    write(dst, root, tail)
    return converted


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("inputs", nargs="+", type=Path, help="presets or folders of presets (searched recursively)")
    ap.add_argument("-o", "--out", required=True, type=Path, help="output folder; inputs are never overwritten")
    args = ap.parse_args(argv)

    jobs = []
    for p in args.inputs:
        if p.is_dir():
            jobs += [(f, args.out / f.relative_to(p)) for f in sorted(p.rglob("*.ffx"))]
        else:
            jobs.append((p, args.out / p.name))
    templates = {}
    count = 0
    for src, dst in jobs:
        if dst.resolve() == src.resolve():
            raise SystemExit(f"{dst}: output would overwrite the input")
        converted = convert_file(src, dst, templates)
        if converted:
            count += 1
            print(f"{src} -> {dst} ({', '.join(converted)})")
    print(f"{count} of {len(jobs)} presets converted")


if __name__ == "__main__":
    main(sys.argv[1:])

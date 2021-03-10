"""Exercise glTF and GLB import against explicitly positioned triangles."""

import argparse
import copy
import json
import struct
import subprocess
from pathlib import Path

from exr_read import read as exr


SCENE = """vesper 1 metres
camera observation 0 0 4 0 0 0 0 1 0 33 0 4 0 0
diffuse glow 0 0 0
emission glow 1 0 0
"""


def fixture():
    vertices = [(-.5, -.5, 0), (.5, -.5, 0), (.5, .5, 0), (-.5, .5, 0)]
    # Interleaved float position and normal, followed by float UV and uint16 indices.
    data = b"".join(struct.pack("<6f", *p, 0, 0, 1) for p in vertices)
    data += struct.pack("<8f6H", 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 2, 0, 2, 3)
    document = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"children": [1, 2], "translation": [0, .25, 0]},
                  {"mesh": 0, "translation": [-.75, 0, 0]},
                  {"mesh": 0, "translation": [.75, 0, 0], "scale": [-1, 1, 1]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}]}],
        "buffers": [{"byteLength": len(data), "uri": "mesh.bin"}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 96, "byteStride": 24},
                        {"buffer": 0, "byteOffset": 96, "byteLength": 32},
                        {"buffer": 0, "byteOffset": 128, "byteLength": 12}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
                       "min": [-.5, -.5, 0], "max": [.5, .5, 0]},
                      {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3"},
                      {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC2"},
                      {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"}],
    }
    return document, data


def glb(document, data):
    inline = copy.deepcopy(document)
    del inline["buffers"][0]["uri"]
    encoded = json.dumps(inline).encode()
    encoded += b" " * (-len(encoded) % 4)
    return (struct.pack("<4sIIII", b"glTF", 2, 28+len(encoded)+len(data), len(encoded), 0x4e4f534a) +
            encoded + struct.pack("<II", len(data), 0x004e4942) + data)


def reference():
    result = SCENE
    for x in (-.75, .75):
        a, b, c, d = [(x-.5, -.25), (x+.5, -.25), (x+.5, .75), (x-.5, .75)]
        for corners in ((a, b, c), (a, c, d)):
            points = " ".join(f"{p[0]} {p[1]} 0" for p in corners)
            result += f"triangle glow {points} 0 0 0 0\n"
    return result


def exercise(binary, out, gpu):
    out.mkdir(parents=True, exist_ok=False)
    document, data = fixture()
    (out / "mesh.bin").write_bytes(data)
    (out / "mesh.gltf").write_text(json.dumps(document))
    (out / "mesh.glb").write_bytes(glb(document, data))
    (out / "reference.vs").write_text(reference())
    for ext in ("gltf", "glb"):
        (out / f"{ext}.vs").write_text(SCENE + f"gltf glow mesh.{ext} 1 1 1 0 0 0 0 0 0 0\n")

    observations = {}
    for backend in ("cpu", "vulkan") if gpu else ("cpu",):
        results = []
        for name in ("reference", "gltf", "glb"):
            prefix = out / f"{backend}-{name}"
            result = subprocess.run([binary, "render", out / f"{name}.vs", "--backend", backend,
                                     "--width", "24", "--height", "16", "--spp", "8", "--depth", "0",
                                     "--prefix", prefix], capture_output=True, text=True, timeout=30, check=True)
            prefix.with_suffix(".log").write_text(result.stderr)
            shape, values, _ = exr(prefix.with_suffix(".exr"), ("X", "Y", "Z", "Samples"))
            if shape != (24, 16) or set(values["Samples"]) != {8} or max(values["Y"]) <= 0:
                raise ValueError("mesh render had wrong size, counts or emission")
            results.append(values)
        if results[0] != results[1] or results[0] != results[2]:
            raise ValueError(f"{backend} transformed glTF, GLB and literal geometry disagree")
        observations[backend] = sum(y > 0 for y in results[0]["Y"])

    negatives = []
    for name, section, index, field, value, message in (
        ("short-view", "bufferViews", 0, "byteLength", 20, "accessor"),
        ("index-offset", "accessors", 3, "byteOffset", 1, "accessor"),
        ("uri-escape", "buffers", 0, "uri", "../mesh.bin", "relative files"),
    ):
        bad = copy.deepcopy(document)
        bad[section][index][field] = value
        (out / "bad.gltf").write_text(json.dumps(bad))
        (out / "bad.vs").write_text(SCENE + "gltf glow bad.gltf 1 1 1 0 0 0 0 0 0 0\n")
        result = subprocess.run([binary, "inspect", out / "bad.vs"], capture_output=True, text=True, timeout=5)
        if result.returncode != 2 or message not in result.stderr:
            raise ValueError(f"malformed {name} was not rejected: {result.stderr}")
        negatives.append({"case": name, "exit": result.returncode, "reason": result.stderr.strip()})
    (out / "bad.gltf").write_text(json.dumps(document))
    (out / "mesh.bin").write_bytes(data[:-2] + b"\xff\x00")
    result = subprocess.run([binary, "inspect", out / "bad.vs"], capture_output=True, text=True, timeout=5)
    if result.returncode != 2 or "triangle index outside" not in result.stderr:
        raise ValueError(f"invalid triangle index was not rejected: {result.stderr}")
    negatives.append({"case": "index-range", "exit": result.returncode, "reason": result.stderr.strip()})
    (out / "mesh.bin").write_bytes(data)
    print(json.dumps({"visible_pixels": observations, "rejections": negatives}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--gpu", action="store_true")
    args = parser.parse_args()
    exercise(args.binary.resolve(), args.output, args.gpu)

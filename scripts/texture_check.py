"""Compare spectral texture filtering with independent linear-light expectations."""

import argparse
import json
import math
import struct
import subprocess
from pathlib import Path

from exr_read import read as exr
from mesh_check import fixture, glb


LEVELS = (0, 64, 128, 255)
SCENE = """vesper 1 metres
camera observation 0 0 4 0 0 0 0 1 0 33 0 4 0 0
diffuse matte .8 .8 .8
gltf matte mesh.glb 1 1 1 0 0 0 0 0 0 0
point 0 0 2 20 0 0
"""


def exercise(binary, out, gpu):
    out.mkdir(parents=True, exist_ok=False)
    document, data = fixture()
    data = data[:96] + struct.pack("<8f", *([.5] * 8)) + data[128:]
    (out / "mesh.glb").write_bytes(glb(document, data))
    colors = bytes(level for level in LEVELS for _ in range(3))
    (out / "levels.ppm").write_text("P3\n2 2\n255\n" + " ".join(map(str, colors)) + "\n")
    (out / "binary.ppm").write_bytes(b"P6\n2 2\n255\n" + colors)
    linear = sum(LEVELS) / (4 * 255)
    srgb = sum(((v/255 + .055)/1.055)**2.4 if v/255 > .04045 else v/(255*12.92) for v in LEVELS) / 4
    cases = {
        "flat": ("", 1),
        "linear": ("texture matte levels.ppm linear 1 1 0 0\n", linear),
        "binary": ("texture matte binary.ppm linear 1 1 0 0\n", linear),
        "srgb": ("texture matte levels.ppm srgb 1 1 0 0\n", srgb),
        "offset": ("texture matte levels.ppm linear 1 1 .25 .25\n", 1),
        "repeat": ("texture matte levels.ppm linear 1 1 .5 .5\n", linear),
    }
    results = {}
    for backend in ("cpu", "vulkan") if gpu else ("cpu",):
        values = {}
        for name, (directive, expected) in cases.items():
            scene = out / f"{name}.vs"
            scene.write_text(SCENE + directive)
            prefix = out / f"{backend}-{name}"
            command = [binary, "render", scene, "--backend", backend, "--width", "24", "--height", "16",
                       "--spp", "12", "--depth", "1", "--prefix", prefix]
            if name == "linear":
                command += ["--checkpoint", prefix.with_suffix(".vfilm")]
            proc = subprocess.run(command, capture_output=True, text=True, timeout=30, check=True)
            prefix.with_suffix(".log").write_text(proc.stderr)
            shape, channels, _ = exr(prefix.with_suffix(".exr"), ("X", "Y", "Z", "Samples"))
            if shape != (24, 16) or set(channels["Samples"]) != {12}:
                raise ValueError("texture fixture rendered the wrong work")
            if name == "flat":
                reference = channels
                if max(reference["Y"]) <= 0:
                    raise ValueError("unlit texture fixture")
            maximum = 0
            for channel in ("X", "Y", "Z"):
                for a, b in zip(reference[channel], channels[channel]):
                    if not math.isclose(a * expected, b, rel_tol=3e-6, abs_tol=1e-8):
                        raise ValueError(f"{backend} {name} filtering mismatch for {channel}: {a} vs {b}")
                    if a > 0:
                        maximum = max(maximum, abs(b / a - expected))
            values[name] = maximum
        changed = command + ["--resume", out / f"{backend}-linear.vfilm"]
        changed[2] = out / "srgb.vs"
        rejected = subprocess.run(changed, capture_output=True, text=True, timeout=30)
        if rejected.returncode != 1 or "mismatch" not in rejected.stderr:
            raise ValueError("checkpoint reused samples across texture interpretations")
        results[backend] = {"maximum_factor_errors": values, "rejected_changed_texture": rejected.returncode}
    (out / "binary.ppm").write_bytes(b"P6\n2 2\n255\n" + colors[:-1])
    truncated = subprocess.run([binary, "inspect", out / "binary.vs"], capture_output=True, text=True, timeout=5)
    (out / "binary.ppm").write_bytes(b"P6\n2 2\n255\n" + colors)
    if truncated.returncode != 2 or "truncated" not in truncated.stderr:
        raise ValueError("truncated texture was accepted")
    print(json.dumps({"backend_results": results, "rejected_truncated_texture": truncated.returncode,
                      "linear_factor": linear, "srgb_factor": srgb}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--gpu", action="store_true")
    args = parser.parse_args()
    exercise(args.binary.resolve(), args.output, args.gpu)

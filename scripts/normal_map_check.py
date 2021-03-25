"""Check bounded diffuse normal-map response and continuation sampling."""

import argparse
import json
import math
import subprocess
from pathlib import Path

from exr_read import read as exr


Z = math.sqrt(3)/2
DIRECT = {"flat": 1, "neutral": 1, "toward": Z, "away": Z*(Z-.25), "mirrored": Z}
GEOMETRY = """diffuse patch .8 .8 .8
triangle patch -4 -3 0 4 -3 0 -4 8 0 0 0 0 0
"""
CAMERA = "vesper 1 metres\ncamera eye 0 0 1 0 0 0 0 1 0 .00001 0 1 0 0\n"


def hemisphere():
    """Midpoint solid-angle integration of the projected diffuse normal lobe."""
    n = 384
    total = 0.
    for i in range(n):
        z = (i+.5)/n
        radial = math.sqrt(1-z*z)
        total += math.fsum(max(0,min(z,Z*z+.5*radial*math.cos(2*math.pi*(j+.5)/n))) for j in range(n))
    return Z * 2 * total / (n*n)


def exercise(binary, out, gpu):
    out.mkdir(parents=True, exist_ok=False)
    (out / "tilt.ppm").write_text("P3\n1 1\n2\n2 1 2\n")
    configs = {
        "flat": ("", -1, False),
        "flat-toward": ("", 1, False),
        "neutral": ("normal_map patch tilt.ppm 1 1 0 0 0\n", -1, False),
        "toward": ("normal_map patch tilt.ppm 1 1 0 0 1\n", 1, False),
        "away": ("normal_map patch tilt.ppm 1 1 0 0 1\n", -1, False),
        "mirrored": ("normal_map patch tilt.ppm -1 1 0 0 1\n", -1, False),
        "sampled-flat": ("", 0, True),
        "sampled-tilt": ("normal_map patch tilt.ppm 1 1 0 0 1\n", 0, True),
    }
    for name, (directive, x, sampled) in configs.items():
        light = "environment 1\n" if sampled else f"point {x} 0 2 20 0 0\n"
        (out / f"{name}.vs").write_text(CAMERA + GEOMETRY + directive + light)
    rows = {}
    for backend in ("cpu", "vulkan") if gpu else ("cpu",):
        outputs = {}
        for name, (_, _, sampled) in configs.items():
            prefix = out / f"{backend}-{name}"
            spp = 131072 if sampled else 128
            command = [binary, "render", out / f"{name}.vs", "--backend", backend, "--width", "1",
                       "--height", "1", "--depth", "1", "--spp", str(spp), "--seed", "47", "--prefix", prefix]
            if sampled:
                command += ["--no-nee"]
            result = subprocess.run(command, capture_output=True, text=True, check=True, timeout=60)
            prefix.with_suffix(".log").write_text(result.stderr)
            shape, values, _ = exr(prefix.with_suffix(".exr"), ("Y", "Samples"))
            if shape != (1,1) or values["Samples"] != [spp] or values["Y"][0] <= 0:
                raise ValueError("normal map fixture had invalid or empty work")
            outputs[name] = values["Y"][0]
        ratios = {name: outputs[name]/outputs["flat-toward" if name=="toward" else "flat"] for name in DIRECT}
        for name, expected in DIRECT.items():
            if abs(ratios[name]-expected) > 2e-6:
                raise ValueError(f"{backend} incorrect normal factor for {name}: {ratios[name]} vs {expected}")
        sampled = outputs["sampled-tilt"]/outputs["sampled-flat"]
        if abs(sampled-hemisphere()) > .006:
            raise ValueError(f"{backend} sampled diffuse lobe disagrees with quadrature: {sampled}")
        rows[backend] = {"direct": ratios, "sampled": sampled}
    print(json.dumps({"observed": rows, "sampled_expected": hemisphere()}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--gpu", action="store_true")
    args = parser.parse_args()
    exercise(args.binary.resolve(), args.output, args.gpu)

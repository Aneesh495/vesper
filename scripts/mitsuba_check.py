"""Optional external spectral comparison on two analytically integrable scenes."""

import argparse
import csv
import hashlib
import json
import math
import subprocess
from pathlib import Path

import drjit as dr
import mitsuba as mi
import numpy as np

from exr_read import read as exr


ROOT = Path(__file__).resolve().parent.parent
SEEDS = (11, 19, 47)
BUDGETS = (16384, 131072)
CASES = {"panel": (.5, 2.0), "wide-panel": (.875, 1.5)}
REFLECTANCE, EMISSION = .6, 2.1


def expected(half, distance):
    n = 512
    step = 2 * half / n
    centers = [(i + .5) * step - half for i in range(n)]
    return math.fsum(distance**2 / (distance**2 + x*x + y*y)**2 for x in centers for y in centers) * (
        REFLECTANCE * EMISSION * step**2 / math.pi)


def sensor():
    return {"type": "perspective", "fov": .01, "fov_axis": "y",
            "to_world": mi.ScalarTransform4f().look_at(origin=(0,0,1), target=(0,0,0), up=(0,1,0)),
            "film": {"type": "hdrfilm", "width": 1, "height": 1, "pixel_format": "luminance",
                     "component_format": "float32", "rfilter": {"type": "box"}},
            "sampler": {"type": "independent"}}


def rectangle(half, height, front):
    t = mi.ScalarTransform4f
    direction = t().rotate((1,0,0), 180) if not front else t()
    return {"type": "rectangle", "to_world": t().translate((0,0,height)) @ direction @ t().scale((half,half,1))}


def external(half, distance):
    root = {"type": "scene", "sensor": sensor(), "integrator": {"type": "path", "max_depth": 2}}
    patch = rectangle(3, 0, True)
    if half is None:
        patch["emitter"] = {"type": "area", "radiance": {"type": "uniform", "value": 1.0}}
    else:
        patch["bsdf"] = {"type": "diffuse", "reflectance": {"type": "uniform", "value": REFLECTANCE}}
        lamp = rectangle(half, distance, False)
        lamp["emitter"] = {"type": "area", "radiance": {"type": "uniform", "value": EMISSION}}
        root["lamp"] = lamp
    root["patch"] = patch
    return mi.load_dict(root)


def measure(scene, path, seed, spp):
    image = mi.render(scene, seed=seed, spp=spp)
    mi.Bitmap(image).write(str(path))
    return float(np.asarray(image).reshape(-1)[0])


def quad(material, half, z, front):
    a, b, c, d = [(-half,-half,z),(half,-half,z),(half,half,z),(-half,half,z)]
    corners = ((a,b,c),(a,c,d)) if front else ((a,c,b),(a,d,c))
    return "".join(f"triangle {material} " + " ".join(str(v) for p in pts for v in p) +
                   " 0 0 0 0\n" for pts in corners)


def vesper_scene(path, half, distance):
    path.write_text("""vesper 1 metres
camera axial 0 0 1 0 0 0 0 1 0 .01 0 1 0 0
diffuse patch .6 .6 .6
diffuse light 0 0 0
emission light 2.1 0 0
""" + quad("patch", 3, 0, True) + quad("light", half, distance, False))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    binary, out = args.binary.resolve(), args.output
    if (mi.__version__, dr.__version__, np.__version__) != ("3.9.0", "1.4.0", "2.2.6"):
        raise SystemExit("expected Mitsuba 3.9.0, Dr.Jit 1.4.0 and NumPy 2.2.6")
    mi.set_variant("scalar_spectral")
    mi.set_log_level(mi.LogLevel.Warn)
    out.mkdir(parents=True, exist_ok=False)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    with (ROOT / "data/CIE_xyz_1931_2deg.csv").open() as f:
        ybar = [float(row[2]) for row in csv.reader(f)]
    observer_gain = math.fsum(ybar[1:-1]) + .5*(ybar[0]+ybar[-1])
    calibration = external(None, 0)
    rows = []
    ok = True
    for name, (half, distance) in CASES.items():
        analytic = expected(half, distance)
        fixture = out / f"{name}.vs"
        vesper_scene(fixture, half, distance)
        scene = external(half, distance)
        for spp in BUDGETS:
            for seed in SEEDS:
                prefix = out / f"{name}-{spp}-{seed}"
                lamp = measure(scene, prefix.with_suffix(".mitsuba.exr"), seed, spp)
                gain = measure(calibration, prefix.with_suffix(".calibration.exr"), seed ^ 0x9e37, spp)
                cmd = [binary, "render", fixture, "--width", "1", "--height", "1", "--spp", str(spp),
                       "--seed", str(seed), "--depth", "1", "--prefix", prefix]
                done = subprocess.run(cmd, capture_output=True, text=True, check=True, timeout=60)
                prefix.with_suffix(".vesper.json").write_text(done.stdout)
                _, values, _ = exr(prefix.with_suffix(".exr"), ("Y", "Samples"))
                if values["Samples"] != [spp] or not (math.isfinite(gain) and gain > 0):
                    raise ValueError("invalid calibration or useful work")
                v_y = values["Y"][0]
                v, m = v_y / observer_gain, lamp / gain
                if not all(math.isfinite(x) for x in (v,m)):
                    raise ValueError("non-finite normalized measurement")
                if spp == BUDGETS[-1]:
                    ok &= max(abs(v-analytic), abs(m-analytic), abs(v-m)) <= .02*analytic
                rows.append({"case": name, "seed": seed, "spp": spp, "expected": analytic,
                             "vesper_raw_y": v_y, "mitsuba_raw_y": lamp, "mitsuba_calibration_y": gain,
                             "vesper_normalized": v, "mitsuba_normalized": m, "vesper_argv": list(map(str, cmd))})
    report = {"source": commit, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "mitsuba": mi.__version__, "drjit": dr.__version__, "numpy": np.__version__,
              "variant": "scalar_spectral", "observer_y_integral": observer_gain, "rows": rows,
              "high_spp_relative_tolerance": .02, "passed": ok}
    (out / "results.json").write_text(json.dumps(report, allow_nan=False, indent=2) + "\n")
    print(out / "results.json")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())

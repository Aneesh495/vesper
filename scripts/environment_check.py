"""Check importance-sampled directional environments against a cosine integral."""

import argparse
import csv
import json
import math
import subprocess
from pathlib import Path

from exr_read import read as exr


CASES = {"sky": (.1, 3., 26.), "pure-cap": (0., 9., 17.)}
SEEDS = (11, 19, 47)
BUDGETS = (131072, 1048576)
ROOT = Path(__file__).resolve().parent.parent


def exercise(binary, output, gpu):
    output.mkdir(parents=True, exist_ok=False)
    with (ROOT / "data/CIE_xyz_1931_2deg.csv").open() as f:
        ybar = [float(row[2]) for row in csv.reader(f)]
    gain = math.fsum(ybar) - .5*(ybar[0]+ybar[-1])
    rows = []
    for case, (base, peak, angle) in CASES.items():
        expected = .6*(base + peak*math.sin(math.radians(angle))**2)
        fixture = output / f"{case}.vs"
        fixture.write_text("""vesper 1 metres
camera observation 0 0 1 0 0 0 0 1 0 .00001 0 1 0 0
diffuse matte .6 .6 .6
triangle matte -4 -3 0 4 -3 0 -4 8 0 0 0 0 0
point 1 0 2 0 0 0
""" + f"environment_cap {base} {peak} 0 0 1 {angle}\n")
        for spp in BUDGETS:
            for seed in SEEDS:
                for backend in ("cpu", "vulkan") if gpu else ("cpu",):
                    for mode in ("mis", "bsdf"):
                        prefix = output / f"{case}-{backend}-{mode}-{seed}-{spp}"
                        cmd = [binary, "render", fixture, "--backend", backend, "--width", "1", "--height", "1",
                               "--spp", str(spp), "--depth", "1", "--seed", str(seed), "--prefix", prefix]
                        if mode == "bsdf":
                            cmd.append("--no-nee")
                        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60, check=True)
                        prefix.with_suffix(".log").write_text(result.stderr)
                        shape, raw, _ = exr(prefix.with_suffix(".exr"), ("Samples", "Y"))
                        observed = raw["Y"][0]/gain
                        if shape != (1,1) or raw["Samples"] != [spp] or not math.isfinite(observed):
                            raise ValueError("missing environment fixture samples or radiance")
                        if spp == BUDGETS[-1] and abs(observed-expected) > .012*expected:
                            raise ValueError(f"{case}, {backend} {mode} seed {seed}: {observed} vs {expected}")
                        rows.append({"case": case, "backend": backend, "mode": mode, "seed": seed, "spp": spp,
                                     "observed": observed, "expected": expected})
    print(json.dumps(rows))


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("binary", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--gpu", action="store_true")
    a = p.parse_args()
    exercise(a.binary.resolve(), a.output, a.gpu)

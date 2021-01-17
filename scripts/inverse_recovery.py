"""Synthetic measurements from ideal calibrated diffuse, emission and slab experiments."""

import argparse
import json
import math
import random
import subprocess
from pathlib import Path


SEEDS = (11, 19, 47)
T = (0.0, 0.12, 0.24, 0.41, 0.57, 0.74, 0.87, 1.0)


def experiment(case, seed):
    rng = random.Random(seed + 409 * case)
    if case < 4:
        kind = "reflectance"
        truth = [0.22 + 0.075 * case, 0.60 - 0.07 * case, 0.36 + 0.06 * case]
    elif case < 8:
        kind = "emitter"
        truth = [0.8 + 0.16 * (case - 4), 1.5 - 0.14 * (case - 4), 0.64 + 0.18 * (case - 4)]
    elif case < 12:
        kind = "extinction"
        truth = [0.24 + 0.24 * (case - 8)]
    elif case == 12:
        kind = "product"
        truth = [0.5, 1.4]
    elif case == 13:
        kind = "reflectance"
        truth = [0.31, 0.47, 0.64]
    elif case == 14:
        kind = "extinction"
        truth = [0.72]
    else:
        kind = "reflectance"
        truth = [0.45, 0.43, 0.57]
    rows = []
    for i, t in enumerate(T):
        # Normal-incidence Lambertian calibration, direct emitter gain, or slab length.
        gain = (0.9 + 0.06 * i) / (math.pi * (0.7 + 0.07 * i) ** 2) if kind == "reflectance" else 0.9 + 0.06 * i
        distance = 0.0 if case == 14 else 0.35 + i * 0.31
        basis = ((1 - t) ** 2, 2 * t * (1 - t), t * t) if case != 13 else (0.2, 0.4, 0.4)
        if kind == "extinction":
            expected = gain * math.exp(-truth[0] * distance)
        elif kind == "product":
            expected = gain * truth[0] * truth[1]
        else:
            expected = gain * sum(x * b for x, b in zip(truth, basis))
        sigma = 0.0007 if kind == "reflectance" else 0.0011
        measured = expected + rng.gauss(0, sigma)
        if case == 15 and i % 2:
            measured += 0.07
        rows.append((gain, distance, *basis, measured, sigma))
    return kind, truth, rows, [0.15 + 0.8 * rng.random() for _ in range(3)]


def run(binary, output):
    output.mkdir(parents=True, exist_ok=False)
    with (output / "fits.jsonl").open("w") as log:
        for case in range(16):
            for seed in SEEDS if case < 12 else SEEDS[:1]:
                kind, truth, rows, initial = experiment(case, seed)
                file = output / f"calibration-{case:02d}-{seed}.csv"
                data = "\n".join(",".join(format(x, ".17g") for x in row) for row in rows)
                file.write_text(f"vesper-inverse 1 {kind}\n{data}\n")
                args = [str(binary), str(file), *map(str, initial)]
                result = json.loads(subprocess.check_output(args, text=True, timeout=5))
                record = {"case": case, "seed": seed, "truth": truth, "initial": initial,
                          "calibration": file.name, "fit": result}
                log.write(json.dumps(record, allow_nan=False) + "\n")
                if case < 12:
                    error = max(abs(got - expected) / expected for got, expected in zip(result["parameters"], truth))
                    if result["status"] != "identified" or error > 0.05:
                        raise ValueError(f"case {case} seed {seed} failed, relative error {error}")
                else:
                    expected = "unidentifiable" if case < 15 else "model_mismatch"
                    if result["status"] != expected or any(x is not None for x in result["standard_error"]):
                        raise ValueError(f"case {case} returned false certainty")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    options = parser.parse_args()
    run(options.binary.resolve(), options.output)

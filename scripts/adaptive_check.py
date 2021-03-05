"""Verify adaptive allocation and replay unchanged primary sample streams."""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from exr_read import read as exr


WIDTH, HEIGHT, CAP = 24, 18, 128
PROBES = (77, 228, 320)
CHANNELS = ("X", "Y", "Z", "VarianceY", "Samples")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def exercise(binary, output, backend):
    output.mkdir(parents=True, exist_ok=False)
    base = [str(binary), "render", "scenes/afterglow.vs", "--backend", backend,
            "--width", str(WIDTH), "--height", str(HEIGHT), "--seed", "19",
            "--bins", "64", "--step-ns", "1"]
    adaptive = ["--spp", str(CAP), "--adaptive-rel", ".45"]

    def render(name, options):
        prefix = output / name
        result = subprocess.run(base + ["--prefix", str(prefix), *options],
                                capture_output=True, text=True, timeout=60, check=True)
        prefix.with_suffix(".json").write_text(result.stdout)
        prefix.with_suffix(".log").write_text(result.stderr)
        return json.loads(result.stdout)

    complete = output / "adaptive.vfilm"
    partial = output / "partial.vfilm"
    resumed = output / "resumed.vfilm"
    stats = render("adaptive", adaptive + ["--checkpoint", str(complete)])
    shape, values, _ = exr(output / "adaptive.exr", CHANNELS)
    counts = values["Samples"]
    if shape != (WIDTH, HEIGHT) or not 16 == min(counts) < max(counts) <= CAP:
        raise ValueError("invalid adaptive sample budget")
    if not stats["pilot_samples"] == WIDTH * HEIGHT * 16 or stats["samples"] != sum(counts):
        raise ValueError("pilot and retained sample counts were not separate")
    if stats["samples"] + stats["pilot_samples"] >= WIDTH * HEIGHT * CAP:
        raise ValueError("allocation did not reduce total work for this study")
    for pixel in PROBES:
        x, y = pixel % WIDTH, pixel // WIDTH
        fixed = render(f"pixel-{pixel}", ["--spp", str(counts[pixel]), "--region", str(x), str(y),
                                          "1", "1", "--pixel", str(pixel)])
        _, actual, _ = exr(output / f"pixel-{pixel}.exr", CHANNELS)
        if fixed["pilot_samples"] or any(actual[c][pixel] != values[c][pixel] for c in CHANNELS):
            raise ValueError(f"primary samples changed at pixel {pixel}")
    render("partial", adaptive + ["--region", "0", "0", str(WIDTH), "7", "--checkpoint", str(partial)])
    render("resumed", adaptive + ["--resume", str(partial), "--checkpoint", str(resumed)])
    if digest(complete) != digest(resumed):
        raise ValueError("adaptive resume changed film bytes")
    print(json.dumps({"backend": backend, "samples": stats["samples"], "pilot_samples": stats["pilot_samples"],
                      "fixed_cap_samples": WIDTH * HEIGHT * CAP, "min_spp": min(counts), "max_spp": max(counts),
                      "probes": PROBES, "checkpoint_sha256": digest(complete)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--backend", choices=("cpu", "vulkan"), default="cpu")
    options = parser.parse_args()
    exercise(options.binary.resolve(), options.output, options.backend)

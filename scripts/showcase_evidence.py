"""Lock a long render to source and independently verify each completed film."""

import argparse
import json
import math
import struct
from pathlib import Path

from campaign import digest, git
from exr_read import read as exr


WIDTH, HEIGHT, SPP = 1920, 1080, 1024
SCENES = ("afterglow", "veil", "prism", "iris", "shutter", "array")


def require(test, message):
    if not test:
        raise ValueError(message)


def identity(binary):
    require(not git("status", "--porcelain"), "showcase evidence requires a clean source checkout")
    commit = git("rev-parse", "HEAD")
    return {"commit": commit, "binary_sha256": digest(binary), "scenes": list(SCENES),
            "width": WIDTH, "height": HEIGHT, "spp": SPP, "seed": 1789,
            "depth": 8, "backend": "vulkan", "sampler": "stratified"}


def check_film(root, name, attempt):
    prefix = root / name
    report = json.loads(attempt.read_text())
    require(report["backend"] == "vulkan" and report["adaptive_rel"] == 0 and not report["interrupted"],
            "renderer did not complete the frozen Vulkan workload")
    image = prefix.with_suffix(".exr")
    shape, raw, metadata = exr(image, ("Samples", "Y"))
    require(shape == (WIDTH, HEIGHT) and set(raw["Samples"]) == {SPP}, "missing full per-pixel sample counts")
    require(f"scene={report['scene']};" in metadata and
            ";backend=1;seed=1789;sampler=1;wavelength=1;depth=8;" in metadata and
            math.isclose(raw["Y"][report["pixel"]], report["pixel_y"], rel_tol=1e-6, abs_tol=1e-9),
            "showcase scene, integrator or raw radiance mismatch")
    checkpoint = prefix.with_suffix(".vfilm")
    require(checkpoint.stat().st_size == 80 + WIDTH * HEIGHT * 84 + 8, "incorrect full checkpoint size")
    with checkpoint.open("rb") as f:
        header = f.read(80)
    require(header.startswith(b"VSPFILM\0") and struct.unpack_from("<Q", header, 48)[0] == int(report["scene"], 16) and
            struct.unpack_from("<Q", header, 56)[0] != 0, "checkpoint scene or device identity mismatch")
    files = {p.name: digest(p) for p in (image, checkpoint, prefix.with_suffix(".ppm"), attempt,
                                        Path(str(attempt) + ".log"))}
    result = {"scene": name, "scene_id": report["scene"], "width": WIDTH, "height": HEIGHT,
              "spp": SPP, "verified_primary_samples": WIDTH*HEIGHT*SPP, "mean_y": math.fsum(raw["Y"])/(WIDTH*HEIGHT),
              "useful_path_segments_this_attempt": report["rays"], "render_seconds_this_attempt": report["seconds"],
              "sha256": files}
    prefix.with_suffix(".verified.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    print(json.dumps(result))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scene", choices=SCENES)
    parser.add_argument("--attempt", type=Path)
    args = parser.parse_args()
    if bool(args.scene) != bool(args.attempt):
        parser.error("scene and attempt are required together")
    lock = args.output / "source.json"
    current = identity(args.binary)
    if lock.exists():
        require(json.loads(lock.read_text()) == current, "showcase output belongs to another source or binary")
    else:
        require(not args.scene, "showcase source lock is missing")
        with lock.open("x") as f:
            json.dump(current, f, indent=2)
    if args.scene:
        check_film(args.output, args.scene, args.attempt)


if __name__ == "__main__":
    main()

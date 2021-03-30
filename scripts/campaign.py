"""Execute bounded diagnostic work, preserving raw outputs and command failures."""

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SEEDS = (11, 19, 47)
SCENES = ("afterglow", "veil", "meridian")
TRANSIENT = ("afterglow", "veil", "prism", "meridian", "lantern", "shutter")


def dump(path, obj):
    path.write_text(json.dumps(obj, indent=2, sort_keys=True, allow_nan=False) + "\n")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def source_inputs():
    return {name: digest(ROOT / name) for name in git("ls-files").splitlines()}


def manifest(output):
    return {str(p.relative_to(output)): digest(p) for p in sorted(output.rglob("*"))
            if p.is_file() and p.name != "MANIFEST.json"}


class Campaign:
    def __init__(self, output, build):
        self.output = output
        self.build = build
        self.log = output / "COMMANDS.jsonl"
        self.env = dict(os.environ, VESPER_VALIDATION="1")
        if platform.system() == "Darwin" and Path("/opt/homebrew/lib/libVkLayer_khronos_validation.dylib").exists():
            self.env["DYLD_LIBRARY_PATH"] = "/opt/homebrew/lib"

    def run(self, name, cmd, timeout=300, validate=False):
        command = list(map(str, cmd))
        path = self.output / (name + ".stdout")
        err = self.output / (name + ".stderr")
        start = time.monotonic()
        with path.open("w") as stdout, err.open("w") as stderr:
            try:
                done = subprocess.run(command, cwd=ROOT, stdout=stdout, stderr=stderr,
                                      env=self.env if validate else os.environ, timeout=timeout)
                status = done.returncode
            except subprocess.TimeoutExpired:
                status = 124
        with self.log.open("a") as log:
            log.write(json.dumps({"name": name, "argv": command, "exit": status,
                                  "seconds": time.monotonic() - start, "validation": validate}) + "\n")
        if status:
            raise RuntimeError(f"{name} returned {status}. See {err}")

    def render(self, scene, backend, seed, spp, width, height, extra=(), name=None, validate=True):
        name = name or f"{scene}-{backend}-{seed}-{spp}"
        self.run(name, [self.build / "vesper", "render", f"scenes/{scene}.vs", "--backend", backend,
                        "--width", width, "--height", height, "--seed", seed, "--spp", spp,
                        "--prefix", self.output / name, *extra], validate=validate and backend == "vulkan")

    def execute(self):
        self.run("ctest", ["ctest", "--test-dir", self.build, "--output-on-failure"])
        self.run("geometry", [self.build / "vesper-geometry-check", "1000000"])
        self.run("mesh-import", [sys.executable, "scripts/mesh_check.py", self.build / "vesper",
                                 self.output / "mesh-import", "--gpu"], validate=True)
        self.run("texture-filter", [sys.executable, "scripts/texture_check.py", self.build / "vesper",
                                     self.output / "texture-filter", "--gpu"], validate=True)
        self.run("normal-maps", [sys.executable, "scripts/normal_map_check.py", self.build / "vesper",
                                   self.output / "normal-maps", "--gpu"], validate=True)
        self.run("environment", [sys.executable, "scripts/environment_check.py", self.build / "vesper",
                                  self.output / "environment", "--gpu"], validate=True)
        self.run("grid-contract", [self.build / "vesper", "inspect", "scenes/veil.vs"])
        self.run("numerical", [self.build / "vesper-numerical-check"])
        self.run("temporal", [self.build / "vesper-temporal-check"])
        self.run("benchmark", [self.build / "vesper-benchmark", "16"])
        self.run("inverse", [sys.executable, "scripts/inverse_recovery.py", self.build / "vesper-inverse",
                             self.output / "inverse"])
        self.run("recovery", [sys.executable, "scripts/recovery.py", self.build / "vesper",
                             self.output / "recovery"])
        self.run("gpu-recovery", [sys.executable, "scripts/recovery.py", self.build / "vesper",
                                 self.output / "gpu-recovery", "--backend", "vulkan"], validate=True)
        for backend in ("cpu", "vulkan"):
            name = "adaptive-" + backend
            self.run(name, [sys.executable, "scripts/adaptive_check.py", self.build / "vesper",
                            self.output / name, "--backend", backend], validate=backend=="vulkan")
        self.run("trace", [self.build / "vesper", "trace", "scenes/afterglow.vs", "--width", 24,
                            "--height", 18, "--pixel", 228, "--seed", 19, "--sample", 12])
        for scene in SCENES:
            for seed in SEEDS:
                for spp in (16, 128):
                    for backend in ("cpu", "vulkan"):
                        self.render(scene, backend, seed, spp, 24, 18)
        self.run("showcases", ["sh", "scripts/gallery.sh", self.build / "vesper",
                               self.output / "gallery", "cpu", 160, 100, 32])
        for scene in TRANSIENT:
            self.render(scene, "vulkan", 1789, 32, 256, 256, name=f"transient-{scene}",
                        extra=("--bins", 512, "--step-ns", ".125", "--checkpoint",
                               self.output / f"transient-{scene}.vfilm"))
        self.run("workbench", [self.build / "vesper-workbench", "scenes/afterglow.vs", "cpu",
                               "--smoke", self.output / "workbench.bmp"])
        self.run("gpu-workbench", [self.build / "vesper-workbench", "scenes/afterglow.vs", "vulkan",
                                   "--smoke", self.output / "gpu-workbench.bmp", "2"], validate=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--profile", choices=("diagnostic", "full"), required=True)
    parser.add_argument("--evidence-root", type=Path, default=ROOT / "evidence")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}", args.run_id):
        parser.error("invalid run ID")
    if git("status", "--porcelain"):
        parser.error("acceptance requires a clean source checkout")
    output = args.evidence_root.resolve() / args.run_id
    output.mkdir(parents=True, exist_ok=False)
    commit = git("rev-parse", "HEAD")
    remote = git("ls-remote", "origin", "refs/heads/main").split()
    if not remote or remote[0] != commit:
        parser.error("acceptance requires the verified published main tip")
    dump(output / "SOURCE_INPUTS.json", {"commit": commit, "files": source_inputs()})
    dump(output / "DATASETS.json", {"observer": {"source": "https://doi.org/10.25039/CIE.DS.xvudnb9b",
         "sha256": digest(ROOT / "data/CIE_xyz_1931_2deg.csv")}, "scenes": "authored synthetic"})
    build = args.build_dir.resolve()
    binaries = {p.name: digest(p) for p in build.glob("vesper*") if p.is_file()}
    dump(output / "ENVIRONMENT.json", {"platform": platform.platform(), "cpu_count": os.cpu_count(),
         "compiler": subprocess.check_output(["cc", "--version"], text=True).splitlines()[0],
         "binary_hashes": binaries, "build": str(build)})
    result = {"profile": args.profile, "run_id": args.run_id, "status": "failed", "commit": commit}
    status = 1
    try:
        if args.profile == "full":
            raise RuntimeError("full prerequisites remain open in docs/BUILD_STATUS.md")
        Campaign(output, build).execute()
        result["status"] = "executed"
        status = 0
    except (RuntimeError, OSError) as error:
        result["failure"] = str(error)
        print(error, file=sys.stderr)
    finally:
        dump(output / "ACCEPTANCE.json", result)
        dump(output / "MANIFEST.json", manifest(output))
    print(output)
    return status


if __name__ == "__main__":
    sys.exit(main())

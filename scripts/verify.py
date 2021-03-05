"""Independently verify the raw diagnostic bundle against the trusted checkout."""

import argparse
import csv
import hashlib
import json
import math
import re
import struct
import subprocess
import sys
from pathlib import Path

from exr_read import read as exr


ROOT = Path(__file__).resolve().parent.parent
SEEDS = (11, 19, 47)
STUDIES = ("afterglow", "veil", "meridian")
TRANSIENT = ("afterglow", "veil", "prism", "meridian", "lantern", "shutter")


def require(test, message):
    if not test:
        raise ValueError(message)


def hash_file(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def load(path):
    return json.loads(path.read_text())


def records(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def nearly(a, b, *, rel=1e-9, abs_=1e-12):
    return math.isclose(a, b, rel_tol=rel, abs_tol=abs_)


def source_and_manifest(bundle, build, require_full):
    acceptance = load(bundle / "ACCEPTANCE.json")
    require(not require_full or acceptance["profile"] == "full", "full acceptance is required for release")
    require(acceptance["profile"] == "diagnostic" and acceptance["status"] == "executed",
            "bundle does not contain an executed diagnostic campaign")
    manifest = load(bundle / "MANIFEST.json")
    paths = {str(path.relative_to(bundle)) for path in bundle.rglob("*")
             if path.is_file() and path != bundle / "MANIFEST.json"}
    require(paths == set(manifest), "missing or unexpected artifacts")
    for name in paths:
        path = bundle / name
        require(not path.is_symlink() and hash_file(path) == manifest[name], f"artifact integrity failure: {name}")
    source = load(bundle / "SOURCE_INPUTS.json")
    require(source["commit"] == acceptance["commit"] == git("rev-parse", "HEAD"),
            "bundle source differs from the trusted checkout")
    require(not git("status", "--porcelain"), "verification requires clean tracked source")
    tracked = git("ls-files").splitlines()
    require(set(tracked) == set(source["files"]), "source manifest file set mismatch")
    for name in tracked:
        require(source["files"][name] == hash_file(ROOT / name), f"source bytes changed: {name}")
    ds = load(bundle / "DATASETS.json")["observer"]
    require(ds["sha256"] == source["files"]["data/CIE_xyz_1931_2deg.csv"], "observer identity mismatch")
    env = load(bundle / "ENVIRONMENT.json")
    for name in ("vesper", "vesper-inverse"):
        require(hash_file(build / name) == env["binary_hashes"][name], f"exact replay binary mismatch: {name}")
    commands = {row["name"]: row for row in records(bundle / "COMMANDS.jsonl")}
    require(all(row["exit"] == 0 and row["seconds"] > 0 for row in commands.values()), "failed recorded command")
    require({"ctest", "geometry", "numerical", "temporal", "benchmark", "inverse",
             "recovery", "gpu-recovery", "adaptive-cpu", "adaptive-vulkan", "trace", "showcases", "workbench", "grid-contract"}.issubset(commands), "required command missing")
    return commands


def temporal(bundle):
    rows = records(bundle / "temporal.stdout")
    by_case = {row["case"]: row for row in rows if row["case"] != "square-mis"}
    require(len(by_case) == 21, "temporal case coverage mismatch")
    for i in range(15):
        r = by_case[f"{'direct' if i < 6 else 'bounce'}-{i}"]
        length = 4 + .13 * i
        if i >= 6:
            length += math.hypot(.14 * (i - 4), 1 + .03 * i)
        expected = length / 299792458
        require(nearly(r["step_s"], .125e-9, abs_=1e-18) and nearly(r["length_m"], length),
                "invalid temporal geometry or time-bin units")
        require(abs(r["arrival_s"] - expected) <= .0625e-9, "arrival omitted or changed a propagation segment")
        pulse = (.21 * (i % 3) + .5 * (.18 * (i % 4) + .11 * (i % 5))) * 1e-9
        require(abs(r["centroid_s"] - expected - pulse) <= .0625e-9, "incorrect pulse convolution")
        require(r["steady_y"] > 0 and nearly(r["bins_y"], r["steady_y"], rel=2e-6),
                "integrated bins differ from steady radiance")
    for i in range(5):
        r = by_case[f"glass-{i}"]
        nm = r["wavelength_nm"]
        length = 5 + 2 * (.36 + .04 * i) * (.25 + .08 * i + .006 * i / ((nm / 1000) ** 2))
        require(360 <= nm <= 830 and nearly(r["arrival_s"], length / 299792458, abs_=1e-14),
                "dispersive group delay mismatch")
    clip = by_case["clipped"]
    require(nearly(clip["bins_y"], .5, abs_=2e-7) and nearly(clip["excluded_y"], .5),
            "clipped energy mismatch")
    mis = [row for row in rows if row["case"] == "square-mis"]
    keys = {(row["nee"], row["seed"], row["samples"]) for row in mis}
    require(len(mis) == 12 and keys == {(n, s, p) for n in (0, 1) for s in SEEDS for p in (4096, 131072)},
            "MIS case coverage mismatch")
    expected = math.fsum(.6 * 2.1 * 4 / (math.pi * (4 + x*x + y*y) ** 2) for x in
        ((i + .5) / 512 - .5 for i in range(512)) for y in ((i + .5) / 512 - .5 for i in range(512))) / 512**2
    for row in mis:
        n = row["samples"]
        require(nearly(row["expected"], expected, abs_=1e-7) and row["m2"] >= 0, "MIS oracle mismatch")
        if n == 131072:
            require(abs(row["estimate"] - expected) <= 6 * math.sqrt(row["m2"] / ((n - 1) * n)) + .0001,
                    "MIS estimate outside frozen interval")


def spectral_volume(bundle, build):
    contract = load(bundle / "grid-contract.stdout")
    grid = contract["media"][1]
    values = [float(x) for x in (ROOT / "scenes/assets/veil.grid").read_text().split()]
    expected = max(values) * (0.13 + 1.75)
    require(grid["name"] == "veil" and grid["dimensions"] == [3, 3, 3] and len(values) == 27 and
            nearly(grid["density_max"], max(values), abs_=1e-7) and
            grid["majorant"] >= expected and nearly(grid["majorant"], 2.1) and
            nearly(grid["absorption_550"], .13) and nearly(grid["scattering_550"], 1.75),
            "invalid recorded density majorant or calibration")
    lines = list(csv.reader((bundle / "numerical.stdout").open()))
    require(len(lines) == 12, "missing spectral, BSDF or volume results")
    with (ROOT / "data/CIE_xyz_1931_2deg.csv").open() as f:
        values = [[float(v) for v in line[1:]] for line in csv.reader(f)]
    expect = [sum(row[c] for row in values[1:-1]) + (values[0][c] + values[-1][c]) / 2 for c in range(3)]
    for row in lines:
        if row[0] == "spectrum":
            require(math.dist(list(map(float, row[4:])), expect) < 2, "spectrum integral mismatch")
        elif row[0] == "bsdf":
            reflect, transmit, density, sampled, valid = map(float, row[2:])
            require(0 <= reflect + transmit <= 1.001 and abs(density - valid) < .013 and
                    abs(reflect + transmit - sampled) < .013, "BSDF mass mismatch")
        elif row[0] == "volume":
            tr, pass_, analytic = map(float, row[3:])
            expected = math.exp(-1.2)
            require(abs(tr - expected) < .007 and abs(pass_ - expected) < .008 and
                    nearly(analytic, expected), "attenuation integral mismatch")
        else:
            raise ValueError("unknown numerical experiment")
    trace = (bundle / "trace.stderr").read_text()
    m = re.search(r"wavelength_nm=(\S+) pdf_per_nm=(\S+)", trace)
    require(m is not None, "missing wavelength trace")
    nm, pdf = map(float, m.groups())
    expected = (1 / 470 + max(0, 1 - abs(nm - 550) / 100) / 100) / 2
    require(360 <= nm <= 830 and nearly(pdf, expected), "wrong wavelength density in path trace")
    cmd = [build / "vesper", "trace", "scenes/afterglow.vs", "--width", "24", "--height", "18",
           "--pixel", "228", "--seed", "19", "--sample", "12"]
    replay = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=5, check=True)
    require(replay.stdout == (bundle / "trace.stdout").read_text() and replay.stderr == trace,
            "bounded transport replay differs")


def inverse(bundle, build):
    root = bundle / "inverse"
    rows = records(root / "fits.jsonl")
    expected_keys = {(k, s) for k in range(16) for s in (SEEDS if k < 12 else SEEDS[:1])}
    require(len(rows) == len(expected_keys) and {(r["case"], r["seed"]) for r in rows} == expected_keys,
            "inverse case coverage mismatch")
    coordinates = (0, .12, .24, .41, .57, .74, .87, 1)
    for row in rows:
        case, seed, fit = row["case"], row["seed"], row["fit"]
        path = root / f"calibration-{case:02d}-{seed}.csv"
        require(row["calibration"] == path.name, "inverse calibration name mismatch")
        lines = path.read_text().splitlines()
        kind = ("reflectance" if case in (0, 1, 2, 3, 13, 15) else "emitter" if case < 8 else
                "product" if case == 12 else "extinction")
        require(lines[0] == f"vesper-inverse 1 {kind}" and len(lines) == 9, "invalid calibration shape")
        x = fit["parameters"]
        chi2 = 0
        for j, line in enumerate(lines[1:]):
            gain, distance, b0, b1, b2, observed, sigma = map(float, line.split(","))
            expected_gain = .9 + .06*j
            if kind == "reflectance":
                expected_gain /= math.pi * (.7 + .07*j)**2
            t = coordinates[j]
            basis = (.2, .4, .4) if case == 13 else (1 - 2*t + t*t, 2*t - 2*t*t, t*t)
            require(nearly(gain, expected_gain) and nearly(distance, 0 if case == 14 else .35 + .31*j) and
                    all(nearly(a, b) for a, b in zip((b0, b1, b2), basis)) and
                    nearly(sigma, .0007 if kind == "reflectance" else .0011), "altered measurement calibration")
            if kind == "product":
                model = gain * x[0] * x[1]
            elif kind == "extinction":
                model = gain / math.exp(distance * x[0])
            else:
                model = gain * math.fsum(a * b for a, b in zip(x, (b0, b1, b2)))
            chi2 += ((observed - model) / sigma) ** 2
        require(nearly(chi2, fit["chi2"], abs_=1e-8), "inverse residual mismatch")
        if case < 12:
            if case < 4:
                truth = [.22+.075*case, .60-.07*case, .36+.06*case]
            elif case < 8:
                truth = [.8+.16*(case-4), 1.5-.14*(case-4), .64+.18*(case-4)]
            else:
                truth = [.24+.24*(case-8)]
            require(fit["status"] == "identified" and fit["rank"] == len(truth) and len(x) == len(truth) and
                    all(abs(a-b)/b <= .05 for a,b in zip(x, truth)), "inverse recovery threshold failed")
        else:
            status = "unidentifiable" if case < 15 else "model_mismatch"
            require(fit["status"] == status and all(u is None for u in fit["standard_error"]),
                    "inverse result assigned false certainty")
        if seed == 11 and case in (0, 7, 10, 12, 15):
            cmd = [build / "vesper-inverse", path, *map(str, row["initial"])]
            replay = json.loads(subprocess.check_output(cmd, cwd=ROOT, text=True, timeout=5))
            require(replay == fit, "bounded inverse replay differs")


def image(bundle, name, width, height, spp, report=".stdout"):
    raw = load(bundle / (name + report))
    require(raw["samples"] == width * height * spp and raw["pixel_spp"] == spp and not raw["interrupted"],
            f"false useful sample count: {name}")
    shape, channels, meta = exr(bundle / (name + ".exr"), ("Y", "Samples", "ExcludedY"))
    require(shape == (width, height) and set(channels["Samples"]) == {spp}, "EXR sample count mismatch")
    require(f"scene={raw['scene']};" in meta and nearly(channels["Y"][raw["pixel"]], raw["pixel_y"], rel=1e-6),
            "raw EXR scene or mean mismatch")
    return raw, channels


def renders(bundle, commands):
    for scene in STUDIES:
        for seed in SEEDS:
            for spp in (16, 128):
                means = {}
                for backend in ("cpu", "vulkan"):
                    name = f"{scene}-{backend}-{seed}-{spp}"
                    data, channels = image(bundle, name, 24, 18, spp)
                    require(data["backend"] == backend and data["rays"] >= data["samples"],
                            "render used wrong backend or performed insufficient work")
                    if backend == "vulkan":
                        require(commands[name]["validation"] and "validation=1" in (bundle / (name + ".stderr")).read_text(),
                                "required validation-enabled Vulkan execution missing")
                    means[backend] = math.fsum(channels["Y"]) / (24*18)
                if spp == 128:
                    require(abs(means["cpu"] - means["vulkan"]) <= max(.005, .01 * means["cpu"]),
                            f"CPU and Vulkan estimates diverged for {scene} seed {seed}")
    for scene in TRANSIENT:
        name = "transient-" + scene
        raw, channels = image(bundle, name, 256, 256, 32)
        film = bundle / (name + ".vfilm")
        require(film.stat().st_size == 80 + 256*256*(84 + 512*12) + 8, "transient film dimensions mismatch")
        require(raw["backend"] == "vulkan" and commands[name]["validation"], "transient backend mismatch")
        with film.open("rb") as f:
            header = f.read(80)
            require(header[:8] == b"VSPFILM\0" and struct.unpack_from("<I", header, 8)[0] == 3 and
                    struct.unpack_from("<II", header, 12) == (256, 256) and
                    struct.unpack_from("<I", header, 44)[0] == 512 and
                    struct.unpack_from("<Q", header, 48)[0] == int(raw["scene"], 16) and
                    nearly(struct.unpack_from("<d", header, 72)[0], .125e-9, abs_=1e-18), "film scene or time contract mismatch")
            for i in (8192, raw["pixel"], 60000):
                f.seek(80 + 84*i)
                item = f.read(84)
                samples, = struct.unpack_from("<I", item, 80)
                x, y, z, m2, timed, excluded = struct.unpack_from("<6d", item)
                require(samples == 32 and nearly(y, channels["Y"][i], rel=1e-6), "false raw film sample count or mean")
                f.seek(80 + 256*256*84 + i*512*12)
                pixels = struct.unpack("<1536f", f.read(512*12))
                require(all(math.isfinite(v) and v >= 0 for v in pixels) and
                        nearly(math.fsum(pixels[1::3])/samples + excluded, y, rel=4e-6, abs_=1e-7),
                        "raw temporal bins and excluded energy do not reconcile")
    gallery = bundle / "gallery"
    require({p.stem for p in gallery.glob("*.exr")} == set(TRANSIENT) |
            {"iris", "furnace", "metalwork", "frost", "needle", "array"}, "showcase coverage missing")
    for file in gallery.glob("*.exr"):
        image(gallery, file.stem, 160, 100, 32, ".json")


def adaptive(bundle, backend):
    name = "adaptive-" + backend
    root = bundle / name
    report = load(bundle / (name + ".stdout"))
    stats = load(root / "adaptive.json")
    shape, pixels, _ = exr(root / "adaptive.exr", ("X", "Y", "Z", "VarianceY", "Samples"))
    counts = pixels["Samples"]
    require(shape == (24, 18) and stats["backend"] == report["backend"] == backend and
            stats["adaptive_rel"] == .45 and report["min_spp"] == min(counts) == 16 < max(counts) <= 128 and
            report["max_spp"] == max(counts) and stats["samples"] == report["samples"] == sum(counts) and
            stats["pilot_samples"] == report["pilot_samples"] == 24*18*16 and
            sum(counts) + stats["pilot_samples"] < 24*18*128,
            "adaptive allocation or pilot accounting mismatch")
    require(hash_file(root / "adaptive.vfilm") == hash_file(root / "resumed.vfilm") == report["checkpoint_sha256"],
            "adaptive recovery changed film bytes")
    require(report["probes"] == [77, 228, 320], "adaptive replay probe selection mismatch")
    for index in report["probes"]:
        _, fixed, _ = exr(root / f"pixel-{index}.exr", pixels)
        require(all(fixed[c][index] == pixels[c][index] for c in pixels), "adaptive primary sample mismatch")


def recovery(bundle, name, backend, samples):
    rec = load(bundle / (name + ".stdout"))
    paths = bundle / name
    require(rec["interrupted"]["backend"] == rec["resumed"]["backend"] == backend and
            rec["interrupted"]["interrupted"] and 0 < rec["interrupted"]["samples"] < samples and
            rec["resumed"]["samples"] + rec["interrupted"]["samples"] == samples and
            "mismatch" in rec["rejected_scene"] and "checksum" in rec["rejected_corruption"],
            f"{backend} recovery outcome mismatch")
    with (paths / "resumed.vfilm").open("rb") as f:
        header = f.read(80)
    require(struct.unpack_from("<Q", header, 48)[0] == int(rec["interrupted"]["scene"], 16) and
            rec["resumed"]["scene"] == rec["interrupted"]["scene"], "changed scene reused an existing film")
    require(hash_file(paths / "complete.vfilm") == hash_file(paths / "resumed.vfilm") == rec["sha256"],
            f"resumed {backend} result differs from uninterrupted result")
    if backend == "vulkan":
        require(struct.unpack_from("<Q", header, 56)[0] != 0 and
                struct.unpack_from("<I", header, 44)[0] == 128 and
                "validation=1" in (paths / "interrupted.log").read_text() and
                "mismatch" in rec["rejected_backend"] and "mismatch" in rec["rejected_device"],
                "GPU recovery device, gate or validation contract mismatch")


def work(bundle):
    geometry = load(bundle / "geometry.stdout")
    require(geometry["rays"] == 1_000_000 and geometry["edge_rays"] == 1025 and
            0 < geometry["hits"] < geometry["rays"] and geometry["max_distance_error_m"] <= 2e-10,
            "geometry oracle workload mismatch")
    cpu, gpu = sorted(records(bundle / "benchmark.stdout"), key=lambda r: r["backend"])
    require(cpu["backend"] == "cpu" and gpu["backend"] == "vulkan", "benchmark backend missing")
    for row in (cpu, gpu):
        useful = row["path_segments"] + row["shadow_segments"]
        require(row["triangles"] == 1_000_000 and row["samples"] == 512*256*16 and row["seconds"] > 0 and
                row["path_segments"] >= row["samples"] and row["shadow_segments"] > 0 and
                nearly(useful / row["seconds"], row["segments_per_s"], rel=1e-7), "benchmark workload or rate mismatch")
    adaptive(bundle, "cpu")
    adaptive(bundle, "vulkan")
    recovery(bundle, "recovery", "cpu", 192*128*96)
    recovery(bundle, "gpu-recovery", "vulkan", 96*64*64)
    require((bundle / "workbench.bmp").read_bytes().startswith(b"BM"), "workbench capture missing")
    return gpu["segments_per_s"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--require-full", action="store_true")
    a = parser.parse_args()
    bundle, build = a.evidence.resolve(), a.build_dir.resolve()
    try:
        commands = source_and_manifest(bundle, build, a.require_full)
        temporal(bundle)
        spectral_volume(bundle, build)
        inverse(bundle, build)
        renders(bundle, commands)
        rate = work(bundle)
    except (ValueError, OSError, KeyError, IndexError, TypeError, struct.error, subprocess.SubprocessError) as error:
        print(f"Vesper verification rejected bundle: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"status": "diagnostic_verified", "gpu_segments_per_s": rate,
                      "source": git("rev-parse", "HEAD"), "full_acceptance": False}))
    return 0


if __name__ == "__main__":
    sys.exit(main())

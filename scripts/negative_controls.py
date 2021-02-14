"""Mutate copies, rehash the affected artifacts and require semantic rejection."""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def rewrite(target, change):
    original = target.read_text()
    updated = change(original)
    if original == updated:
        raise RuntimeError(f"negative control did not change {target.name}")
    target.unlink()
    target.write_text(updated)


def temporal(field, update):
    def mutate(text):
        rows = text.splitlines()
        record = json.loads(rows[0])
        record[field] = update(record[field])
        rows[0] = json.dumps(record)
        return "\n".join(rows) + "\n"
    return mutate


def calibration(text):
    lines = text.splitlines()
    values = lines[1].split(",")
    values[0] = format(float(values[0]) * 1.1, ".17g")
    lines[1] = ",".join(values)
    return "\n".join(lines) + "\n"


def false_certainty(text):
    rows = [json.loads(line) for line in text.splitlines()]
    fit = next(r["fit"] for r in rows if r["case"] == 12)
    fit.update(status="identified", rank=2, standard_error=[.01, .01])
    return "\n".join(json.dumps(r) for r in rows) + "\n"


def sample_count(text):
    row = json.loads(text)
    row["pixel_spp"] += 1
    return json.dumps(row) + "\n"


def source_mismatch(text):
    row = json.loads(text)
    row["commit"] = "0" * 40
    return json.dumps(row) + "\n"


def bad_replay(text):
    rows = text.splitlines()
    rows[1] = rows[1].replace(",0,0,", ",4,0,", 1)
    if rows[1] == text.splitlines()[1]:
        rows[1] += ",0"
    return "\n".join(rows) + "\n"


def majorant(text):
    row = json.loads(text)
    row["media"][1]["majorant"] = .05
    return json.dumps(row) + "\\n"


def stale_film(target):
    data = bytearray(target.read_bytes())
    scene, = struct.unpack_from("<Q", data, 48)
    struct.pack_into("<Q", data, 48, scene ^ 1)
    checksum = 14695981039346656037
    for byte in data[:-8]:
        checksum = ((checksum ^ byte) * 1099511628211) & 0xffffffffffffffff
    struct.pack_into("<Q", data, len(data) - 8, checksum)
    target.unlink()
    target.write_bytes(data)


CASES = [
    ("wrong-time-units", "temporal.stdout", temporal("arrival_s", lambda x: x * 1e9), "arrival omitted or changed"),
    ("missing-camera-segment", "temporal.stdout", temporal("arrival_s", lambda x: x - 4 / 299792458), "arrival omitted or changed"),
    ("wavelength-pdf", "trace.stderr", lambda x: re.sub(r"pdf_per_nm=\S+", "pdf_per_nm=0.125", x, count=1), "wrong wavelength density"),
    ("wrong-calibration", "inverse/calibration-00-11.csv", calibration, "altered measurement calibration"),
    ("false-certainty", "inverse/fits.jsonl", false_certainty, "assigned false certainty"),
    ("false-sample-count", "afterglow-cpu-11-16.stdout", sample_count, "false useful sample count"),
    ("majorant", "grid-contract.stdout", majorant, "invalid recorded density majorant"),
    ("stale-film", "recovery/resumed.vfilm", stale_film, "changed scene reused an existing film"),
    ("wrong-source", "SOURCE_INPUTS.json", source_mismatch, "source differs"),
    ("changed-replay", "trace.stdout", bad_replay, "bounded transport replay differs"),
]


def run(bundle, build, output):
    verifier = Path(__file__).with_name("verify.py")
    with output.open("x") as log:
        for name, relative, change, reason in CASES + [
            ("corruption", "trace.stdout", lambda x: x + "corrupt\n", "artifact integrity failure"),
            ("missing-artifact", "trace.stdout", None, "missing or unexpected artifacts"),
        ]:
            with tempfile.TemporaryDirectory(prefix="vesper-control-", dir=bundle.parent) as tmp:
                copied = Path(tmp) / "bundle"
                shutil.copytree(bundle, copied, copy_function=os.link)
                target = copied / relative
                if change is stale_film:
                    stale_film(target)
                elif change:
                    rewrite(target, change)
                else:
                    target.unlink()
                if name not in ("corruption", "missing-artifact"):
                    manifest = copied / "MANIFEST.json"
                    checksums = json.loads(manifest.read_text())
                    checksums[relative] = hashlib.sha256(target.read_bytes()).hexdigest()
                    manifest.unlink()
                    manifest.write_text(json.dumps(checksums, indent=2) + "\n")
                argv = [sys.executable, str(verifier), str(copied), "--build-dir", str(build)]
                result = subprocess.run(argv, capture_output=True, text=True, timeout=180)
                log.write(json.dumps({"case": name, "argv": argv, "exit": result.returncode,
                                      "stderr": result.stderr.strip()}) + "\n")
                log.flush()
                if result.returncode == 0 or reason not in result.stderr:
                    raise ValueError(f"negative control {name}: {result.stderr}")
    print(f"Rejected {len(CASES)+2} altered bundles")


if __name__ == "__main__":
    cli = argparse.ArgumentParser()
    cli.add_argument("bundle", type=Path)
    cli.add_argument("--build-dir", type=Path, required=True)
    cli.add_argument("--output", type=Path, required=True)
    args = cli.parse_args()
    run(args.bundle.resolve(), args.build_dir.resolve(), args.output)

"""Drive the CLI through interruption, resume and mismatched state rejection."""

import argparse
import hashlib
import json
import os
import selectors
import signal
import struct
import subprocess
from pathlib import Path


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def execute(argv, status=0):
    done = subprocess.run(argv, capture_output=True, text=True, timeout=60)
    if done.returncode != status:
        raise RuntimeError(f"{argv}: status {done.returncode} {done.stderr}")
    return done


def interrupt_after_progress(argv):
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
        seen = bytearray()
        try:
            with selectors.DefaultSelector() as poll:
                poll.register(process.stderr, selectors.EVENT_READ)
                while b"progress rows=" not in seen:
                    if not poll.select(timeout=30):
                        raise RuntimeError("render did not report progress")
                    chunk = os.read(process.stderr.fileno(), 4096)
                    if not chunk:
                        raise RuntimeError(f"render stopped before progress: {seen.decode()}")
                    seen.extend(chunk)
            process.send_signal(signal.SIGINT)
            stdout, stderr = process.communicate(timeout=60)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    stderr = (seen + stderr).decode()
    if process.returncode != 130:
        raise ValueError(f"interrupted render returned {process.returncode}: {stderr}")
    return json.loads(stdout), stderr


def changed_device(path, data):
    altered = bytearray(data)
    identity, = struct.unpack_from("<Q", altered, 56)
    struct.pack_into("<Q", altered, 56, identity ^ 1)
    checksum = 14695981039346656037
    for byte in memoryview(altered)[:-8]:
        checksum = ((checksum ^ byte) * 1099511628211) & 0xffffffffffffffff
    struct.pack_into("<Q", altered, len(altered) - 8, checksum)
    path.write_bytes(altered)


def recovery(binary, output, backend):
    output.mkdir(parents=True, exist_ok=False)
    partial = output / "partial.vfilm"
    resumed = output / "resumed.vfilm"
    complete = output / "complete.vfilm"
    width, height, spp = (192, 128, 96) if backend == "cpu" else (96, 64, 64)
    argv = [str(binary), "render", "scenes/afterglow.vs", "--width", str(width), "--height", str(height),
            "--spp", str(spp), "--seed", "19", "--depth", "6", "--backend", backend,
            "--prefix", str(output / "frame")]
    if backend == "vulkan":
        argv += ["--bins", "128", "--step-ns", ".5"]
    total = width * height * spp
    interrupted, interrupt_log = interrupt_after_progress(argv + ["--checkpoint", str(partial), "--progress"])
    (output / "interrupted.log").write_text(interrupt_log)
    if not 0 < interrupted["samples"] < total or not interrupted["interrupted"]:
        raise ValueError("interrupted job did not make partial progress")
    out = execute(argv + ["--resume", str(partial), "--checkpoint", str(resumed)])
    execute(argv + ["--checkpoint", str(complete)])
    if digest(resumed) != digest(complete):
        raise ValueError("resumed and uninterrupted checkpoint bytes differ")
    changed = argv.copy()
    changed[2] = "scenes/veil.vs"
    rejection = execute(changed + ["--resume", str(partial)], 1)
    corrupt = output / "corrupt.vfilm"
    corrupt.write_bytes(partial.read_bytes()[:-1])
    corruption = execute(argv + ["--resume", str(corrupt)], 1)
    corrupt.unlink()
    report = {"interrupted": interrupted, "resumed": json.loads(out.stdout),
              "rejected_scene": rejection.stderr.strip(),
              "rejected_corruption": corruption.stderr.strip(),
              "expected_samples": total, "sha256": digest(complete)}
    if backend == "vulkan":
        alternate = argv.copy()
        alternate[alternate.index("--backend") + 1] = "cpu"
        report["rejected_backend"] = execute(alternate + ["--resume", str(partial)], 1).stderr.strip()
        changed_device(corrupt, partial.read_bytes())
        report["rejected_device"] = execute(argv + ["--resume", str(corrupt)], 1).stderr.strip()
        corrupt.unlink()
    print(json.dumps(report))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--backend", choices=("cpu", "vulkan"), default="cpu")
    options = parser.parse_args()
    recovery(options.binary.resolve(), options.output, options.backend)

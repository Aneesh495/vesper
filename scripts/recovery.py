"""Drive the CLI through interruption, resume and mismatched state rejection."""

import argparse
import hashlib
import json
import signal
import subprocess
import time
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(argv, status=0):
    done = subprocess.run(argv, capture_output=True, text=True, timeout=30)
    if done.returncode != status:
        raise RuntimeError(f"{argv}: status {done.returncode} {done.stderr}")
    return done


def recovery(binary, output):
    output.mkdir(parents=True, exist_ok=False)
    partial = output / "partial.vfilm"
    resumed = output / "resumed.vfilm"
    complete = output / "complete.vfilm"
    argv = [str(binary), "render", "scenes/afterglow.vs", "--width", "192", "--height", "128",
            "--spp", "96", "--seed", "19", "--depth", "6", "--prefix", str(output / "frame")]
    process = subprocess.Popen(argv + ["--checkpoint", str(partial)], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    try:
        time.sleep(0.28)
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=20)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    if process.returncode != 130:
        raise ValueError(f"interrupted render returned {process.returncode}: {stderr}")
    interrupted = json.loads(stdout)
    if not 0 < interrupted["samples"] < 192 * 128 * 96:
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
    print(json.dumps({"interrupted": interrupted, "resumed": json.loads(out.stdout),
                      "rejected_scene": rejection.stderr.strip(),
                      "rejected_corruption": corruption.stderr.strip(),
                      "expected_samples": 192 * 128 * 96, "sha256": digest(complete)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    options = parser.parse_args()
    recovery(options.binary.resolve(), options.output)

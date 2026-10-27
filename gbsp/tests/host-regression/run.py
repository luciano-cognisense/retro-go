#!/usr/bin/env python3
"""Compara baseline e candidata do gbsp no host, sem ROM comercial."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time


HERE = Path(__file__).resolve().parent
DEFAULT_ROOT = HERE.parents[2]
HARNESS_REL = Path("gbsp/tests/waitstate-common-base/harness")


def run(cmd, *, cwd=None, env=None, timeout=900):
    started = time.monotonic()
    p = subprocess.run(cmd, cwd=cwd, env=env, text=True, capture_output=True, timeout=timeout)
    return {
        "cmd": [str(x) for x in cmd],
        "returncode": p.returncode,
        "seconds": round(time.monotonic() - started, 3),
        "stdout": p.stdout,
        "stderr": p.stderr,
    }


def source_hash(root: Path) -> str:
    h = hashlib.sha256()
    source = root / "gbsp/components/gbsp-libretro"
    for path in sorted(source.rglob("*")):
        if path.is_file() and path.suffix in {".c", ".cpp", ".h"}:
            h.update(path.relative_to(source).as_posix().encode() + b"\0")
            h.update(path.read_bytes())
    return h.hexdigest()


def symbols(harness: Path, rom: str):
    path = harness / "rom" / rom.replace(".gba", ".sym")
    if not path.exists():
        return {}
    return {k: int(v, 16) for k, v in (line.strip().split("=") for line in path.read_text().splitlines())}


def scenarios(harness: Path, quick: bool):
    roms = [
        "waitcnt-1.gba", "waitcnt-0x2B.gba", "waitcnt-0x9D.gba",
        "alert-1.gba", "alert-0x2B.gba",
        "stress-1.gba", "stress-0x11.gba", "stress-0x5F.gba", "stress-0xE7.gba",
    ]
    frames = 80 if quick else 300
    result = []
    for rom in roms:
        sym = symbols(harness, rom)
        idle = ["--idle", hex(sym["idle_wait"])] if "idle_wait" in sym else []
        result.append((f"{rom}:normal", rom, frames, idle))
        if idle:
            result.append((f"{rom}:sem-idle", rom, frames, []))
        result.append((f"{rom}:save-load", rom, frames, idle + ["--save-load", "17"]))
        result.append((f"{rom}:reset", rom, frames, idle + ["--reset", "23"]))
    long_frames = 300 if quick else 6000
    idle = ["--idle", hex(symbols(harness, "waitcnt-1.gba")["idle_wait"])]
    result.append(("waitcnt-1.gba:longo", "waitcnt-1.gba", long_frames, idle))
    return result


def execute(binary: Path, harness: Path, scenario, env=None):
    name, rom, frames, options = scenario
    result = run([binary, harness / "rom" / rom, str(frames), *options], env=env)
    result["scenario"] = name
    return result


def compare_suite(base_bin: Path, cand_bin: Path, harness: Path, quick: bool, env=None):
    def runtime_errors(stderr: str):
        # UBSan inclui o nome src-host-base/candidate. Normalize somente esse
        # rótulo; o arquivo, a linha e a mensagem continuam sendo comparados.
        return sorted(
            re.sub(r"src-(?:host-base|host-candidate)", "src-HOST", line)
            for line in stderr.splitlines() if "runtime error:" in line
        )

    rows = []
    for scenario in scenarios(harness, quick):
        a = execute(base_bin, harness, scenario, env)
        b = execute(cand_bin, harness, scenario, env)
        equal = a["returncode"] == b["returncode"] == 0 and a["stdout"] == b["stdout"]
        rows.append({
            "scenario": scenario[0], "equal": equal,
            "returncodes": [a["returncode"], b["returncode"]],
            "seconds": [a["seconds"], b["seconds"]],
            "sanitizer_error": "AddressSanitizer" in a["stderr"] + b["stderr"],
            "runtime_errors_equal": runtime_errors(a["stderr"]) == runtime_errors(b["stderr"]),
        })
        print(f"{scenario[0]:30s} {'IGUAL' if equal else 'DIVERGE'}")
    return rows


def rewind_suite(base_bin: Path, cand_bin: Path, harness: Path, quick: bool, env=None):
    pairs = [(3, 4), (5, 9)] if quick else [(3, 4), (5, 9), (10, 11), (12, 30), (7, 23), (20, 21), (33, 34)]
    roms = ["waitcnt-1.gba", "waitcnt-0x2B.gba", "waitcnt-0x9D.gba"]
    keep = 12 if quick else 40
    rows = []
    for binary_name, binary in (("baseline", base_bin), ("candidate", cand_bin)):
        for rom in roms:
            idle = ["--idle", hex(symbols(harness, rom)["idle_wait"])]
            for n, m in pairs:
                direct = run([binary, harness / "rom" / rom, str(n + keep), *idle, "--mark", str(n)], env=env)
                rewind = run([binary, harness / "rom" / rom, str(m + keep), *idle, "--rewind", str(n), str(m)], env=env)
                a = direct["stdout"].split("# acumuladores zerados\n", 1)
                b = rewind["stdout"].split("# acumuladores zerados\n", 1)
                equal = direct["returncode"] == rewind["returncode"] == 0 and len(a) == len(b) == 2 and a[1] == b[1]
                rows.append({"binary": binary_name, "rom": rom, "N": n, "M": m, "equal": equal})
                print(f"rewind {binary_name:9s} {rom:18s} {n:2d}->{m:2d} {'IGUAL' if equal else 'DIVERGE'}")
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", type=Path, default=DEFAULT_ROOT)
    ap.add_argument("--candidate", type=Path, required=True)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--sanitizers", action="store_true")
    args = ap.parse_args()
    baseline = args.baseline.resolve()
    candidate = args.candidate.resolve()
    harness = baseline / HARNESS_REL
    output = (args.output or baseline / "outputs/host-regression").resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not harness.is_dir():
        raise SystemExit(f"harness ausente: {harness}")
    for root in (baseline, candidate):
        if not (root / "gbsp/components/gbsp-libretro/cpu.cpp").is_file():
            raise SystemExit(f"checkout gbsp inválido: {root}")

    labels = (("host-base", baseline), ("host-candidate", candidate))
    setup = []
    for label, root in labels:
        setup.append(run([harness / "setup-src.sh", label, root], cwd=harness))
        if setup[-1]["returncode"]:
            raise SystemExit(setup[-1]["stderr"] or setup[-1]["stdout"])
        setup.append(run([harness / "build.sh", label, harness / f"src-{label}"], cwd=harness))
        if setup[-1]["returncode"]:
            raise SystemExit(setup[-1]["stderr"] or setup[-1]["stdout"])

    base_bin = harness / "bin/sim-host-base"
    cand_bin = harness / "bin/sim-host-candidate"
    normal = compare_suite(base_bin, cand_bin, harness, args.quick)
    rewind = rewind_suite(base_bin, cand_bin, harness, args.quick)
    sanitizer = []
    if args.sanitizers:
        san_env = dict(os.environ, SAN="1", ASAN_OPTIONS="detect_leaks=0")
        for label, _root in labels:
            built = run([harness / "build.sh", f"san-{label}", harness / f"src-{label}"], cwd=harness, env=san_env)
            setup.append(built)
            if built["returncode"]:
                raise SystemExit(built["stderr"] or built["stdout"])
        sanitizer = compare_suite(harness / "bin/sim-san-host-base", harness / "bin/sim-san-host-candidate", harness, True, san_env)

    checks = {
        "setup_ok": all(x["returncode"] == 0 for x in setup),
        "normal_equal": all(x["equal"] for x in normal),
        "normal_no_asan": not any(x["sanitizer_error"] for x in normal),
        "runtime_error_sets_equal": all(x["runtime_errors_equal"] for x in normal),
        "rewind_ok": all(x["equal"] for x in rewind),
        "sanitizers_equal": not args.sanitizers or all(x["equal"] and not x["sanitizer_error"] and x["runtime_errors_equal"] for x in sanitizer),
    }
    result = {
        "tudo_ok": all(checks.values()), "checks": checks,
        "baseline": str(baseline), "candidate": str(candidate),
        "baseline_source_sha256": source_hash(baseline),
        "candidate_source_sha256": source_hash(candidate),
        "quick": args.quick, "sanitizers": args.sanitizers,
        "normal": normal, "rewind": rewind, "sanitizer": sanitizer,
    }
    (output / "verification.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"tudo_ok": result["tudo_ok"], "checks": checks}, indent=2))
    return 0 if result["tudo_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())

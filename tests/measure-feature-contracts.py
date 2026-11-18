"""Serial, alternating main/candidate measurements for the contract refactor."""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def run(options):
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    groups = {}
    for group in ("native", "compiler", "abi", "model"):
        baseline, candidate = getattr(options, "baseline_" + group), getattr(options, "candidate_" + group)
        if bool(baseline) != bool(candidate):
            raise ValueError(f"Both {group} executables are required")
        if baseline:
            groups[group] = {"baseline": baseline.resolve(strict=True), "candidate": candidate.resolve(strict=True)}
    identities = {}
    for revisions in groups.values():
        for executable in revisions.values():
            for path in [executable, *executable.parent.glob("*.dll")]:
                identities[str(path)] = sha(path)
    environment = dict(os.environ)
    if options.counts:
        counts = options.counts.resolve(strict=True)
        identities[str(counts)] = sha(counts)
        environment["XUI_PERF_COUNTS"] = str(counts)
    else:
        environment.pop("XUI_PERF_COUNTS", None)
    samples = []
    manifest = dict(baseline_commit=options.baseline_commit, platform=platform.platform(),
                    started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    warmup_pairs=2, recorded_pairs=7, threshold_percent=5,
                    identities=identities, counts=environment.get("XUI_PERF_COUNTS"),
                    groups={g: {r: str(p) for r, p in rev.items()} for g, rev in groups.items()})
    save(output / "manifest.json", manifest)
    for group, revisions in groups.items():
        reference = None
        for index in range(9):
            for revision in (("candidate", "baseline") if index % 2 else ("baseline", "candidate")):
                executable = revisions[revision]
                argv = [str(executable)] + ([] if group == "compiler" else ["--measure"])
                record = dict(group=group, round=index, revision=revision, warmup=index < 2, argv=argv)
                samples.append(record)
                save(output / "samples.json", samples)
                log = output / f"{group}-{index:02d}-{revision}.log"
                with log.open("wb") as stream:
                    result = subprocess.run(argv, cwd=executable.parent, env=environment, stdout=stream, stderr=subprocess.STDOUT,
                                            timeout=180, check=False)
                record["exit_code"] = result.returncode
                save(output / "samples.json", samples)
                if result.returncode:
                    raise RuntimeError(f"Failed sample retained: {log}")
                rows = [json.loads(line) for line in log.read_text(encoding="utf-8-sig").splitlines() if line.startswith("{")]
                record["rows"] = rows
                timed = [row for row in rows if "wall_ms" in row]
                if not timed or len({row["phase"] for row in timed}) != len(timed):
                    raise RuntimeError(f"Missing/duplicate timing rows: {log}")
                if any(not math.isfinite(row["wall_ms"]) or row["wall_ms"] <= 0 or row["iterations"] <= 0 for row in timed):
                    raise RuntimeError(f"Invalid timing/work count: {log}")
                signature = [{key: value for key, value in row.items() if key not in
                              ("wall_ms", "cpu_ms", "allocated_bytes", "cpp_allocations", "cpp_requested_bytes") and
                              not key.endswith("100ns")} for row in rows]
                if reference is None:
                    reference = signature
                elif signature != reference:
                    raise RuntimeError(f"Exact baseline/candidate work differs: {log}")
                save(output / "samples.json", samples)
    if any(sha(Path(path)) != digest for path, digest in identities.items()):
        raise RuntimeError("Frozen measurement binaries changed")
    summary = []
    for group in groups:
        recorded = [s for s in samples if s["group"] == group and not s["warmup"]]
        phases = [row["phase"] for row in recorded[0]["rows"] if "wall_ms" in row]
        for phase in phases:
            revisions = {}
            for revision in ("baseline", "candidate"):
                rows = [next(row for row in sample["rows"] if row.get("phase") == phase)
                        for sample in recorded if sample["revision"] == revision]
                for row in rows:
                    if "cpu_ms" not in row:
                        row["cpu_ms"] = (row["kernel_after100ns"] + row["user_after100ns"] -
                                         row["kernel_before100ns"] - row["user_before100ns"]) / 10000
                revisions[revision] = rows
            metrics = {}
            for metric in ("wall_ms", "cpu_ms", "allocated_bytes", "cpp_allocations", "cpp_requested_bytes"):
                if metric not in revisions["baseline"][0]:
                    continue
                a = [row[metric] for row in revisions["baseline"]]
                b = [row[metric] for row in revisions["candidate"]]
                base, candidate = statistics.median(a), statistics.median(b)
                delta = (candidate / base - 1) * 100 if base else (0 if candidate == 0 else None)
                metrics[metric] = dict(baseline_median=base, candidate_median=candidate, delta_percent=delta,
                                       baseline_samples=a, candidate_samples=b,
                                       regression=delta is None or delta > 5)
            summary.append(dict(group=group, phase=phase, metrics=metrics))
    save(output / "summary.json", summary)
    flags = [(row["phase"], metric) for row in summary for metric, result in row["metrics"].items() if result["regression"]]
    print(json.dumps({"workloads": len(summary), "processes": len(samples), "screening_flags": flags}))
    return 1 if flags else 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for group in ("native", "compiler", "abi", "model"):
        for revision in ("baseline", "candidate"):
            parser.add_argument(f"--{revision}-{group}", type=Path, required=group in ("native", "compiler"))
    parser.add_argument("--baseline-commit", required=True)
    parser.add_argument("--counts", type=Path, help="Frozen shared native/ABI iteration counts; hashed with the binaries.")
    parser.add_argument("--output", type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))

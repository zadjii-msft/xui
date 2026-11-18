"""Paired image reconciliation measurements; native host measurements use collection-host-evidence.ps1."""
import argparse
import hashlib
import json
import math
import statistics
import subprocess
from pathlib import Path


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def measure(options):
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    paths = {revision: {group: (getattr(options, revision) / name).resolve(strict=True)
                        for group, name in (("legacy", "file_list_image_tests.exe"), ("rows", "row_image_probe.exe"))}
             for revision in ("baseline", "candidate")}
    hashes = {str(path): digest(path) for groups in paths.values() for path in groups.values()}
    save(output / "manifest.json", dict(baseline_commit=options.baseline_commit, hashes=hashes,
                                      warmups=2, recorded_pairs=7, threshold_percent=5))
    samples = []
    for group in ("legacy", "rows"):
        expected = None
        for pair in range(9):
            for revision in (("candidate", "baseline") if pair % 2 else ("baseline", "candidate")):
                executable = paths[revision][group]
                args = [str(executable)] + (["--measure"] if group == "legacy" else [])
                record = dict(group=group, pair=pair, revision=revision, warmup=pair < 2, args=args)
                samples.append(record)
                save(output / "samples.json", samples)
                log = output / f"{group}-{pair:02}-{revision}.log"
                with log.open("wb") as stream:
                    result = subprocess.run(args, cwd=executable.parent, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
                record["exit_code"] = result.returncode
                save(output / "samples.json", samples)
                if result.returncode:
                    raise RuntimeError(f"Failed sample retained at {log}")
                rows = [json.loads(line) for line in log.read_text().splitlines() if line.startswith("{")]
                if len(rows) != (1 if group == "legacy" else 3):
                    raise RuntimeError(f"Incomplete measurement: {log}")
                if any(not math.isfinite(row["wall_ms"]) or row["wall_ms"] <= 0 for row in rows):
                    raise RuntimeError(f"Invalid timing: {log}")
                signature = [{key: value for key, value in row.items()
                              if key not in ("wall_ms", "cpu_ms", "cpp_allocations", "cpp_requested_bytes") and not key.endswith("100ns")}
                             for row in rows]
                if expected is not None and signature != expected:
                    raise RuntimeError(f"Baseline/candidate work changed: {log}")
                expected = signature
                record["rows"] = rows
                save(output / "samples.json", samples)
    if any(digest(Path(path)) != sha for path, sha in hashes.items()):
        raise RuntimeError("Measured executable changed")
    summary = []
    for group in ("legacy", "rows"):
        records = [record for record in samples if record["group"] == group and not record["warmup"]]
        for row in records[0]["rows"]:
            phase = row["phase"]
            metrics = {}
            for key in ("wall_ms", "cpu_ms", "cpp_allocations", "cpp_requested_bytes"):
                if key not in row:
                    continue
                values = {revision: [next(r[key] for r in record["rows"] if r["phase"] == phase)
                                     for record in records if record["revision"] == revision]
                          for revision in ("baseline", "candidate")}
                baseline, candidate = (statistics.median(values[r]) for r in ("baseline", "candidate"))
                delta = (candidate / baseline - 1) * 100 if baseline else (0 if candidate == 0 else None)
                metrics[key] = dict(baseline=baseline, candidate=candidate, delta_percent=delta,
                                    samples=values, flagged=delta is None or delta > 5)
            summary.append(dict(phase=phase, metrics=metrics))
    save(output / "summary.json", summary)
    flags = [(row["phase"], key) for row in summary for key, value in row["metrics"].items() if value["flagged"]]
    print(json.dumps(dict(processes=len(samples), phases=len(summary), flags=flags)))
    return bool(flags)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--baseline-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    raise SystemExit(measure(parser.parse_args()))

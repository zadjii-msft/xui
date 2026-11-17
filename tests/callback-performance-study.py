"""Audit and run fixed phase-5 callback-study groups against frozen executables."""
import argparse
import datetime
import hashlib
import json
import math
import os
import statistics
import subprocess
from pathlib import Path

EDITOR_PHASES = (
    "editor-source-sync", "native-edit-notifications", "synthetic-native-composition",
    "native-selection-caret", "retained-editor-scroll", "native-editor-paint", "real-hwnd-uia-getters",
)
EDITOR_WORK = (
    "iterations", "updates", "text_callbacks", "password_callbacks", "date_callbacks", "queries", "checksum",
    "native_text_writes", "native_font_writes", "native_selection_writes", "native_inset_writes",
    "native_notifications", "native_date_value_writes", "native_date_range_writes", "native_destructions",
    "native_placement_notifications", "native_host_updates", "native_move_size_notifications",
)
COLLECTION_PHASES = ("scroll-sync", "selection-publication", "source-publication", "paint-visible-rows")


def require(value, message):
    if not value:
        raise RuntimeError(message)


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def read_rows(path):
    return [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.startswith("{")]


def validate(group, rows, counts, reference=None):
    timed = [row for row in rows if "wall_ms" in row]
    names = [row["phase"] for row in timed]
    expected = group["phases"]
    require(names == expected and len(names) == len(set(names)), "Exact complete native phase order")
    signature = []
    for row in timed:
        phase = row["phase"]
        require(type(row["iterations"]) is int and row["iterations"] > 0, "Positive exact work")
        require(type(row["wall_ms"]) in (int, float) and math.isfinite(row["wall_ms"])
                and 0 < row["wall_ms"] < 90000, "Positive bounded wall time")
        if group["name"] in ("model", "abi", "managed"):
            require(row["iterations"] == counts[phase] and row["measured"] is True, "Frozen iteration protocol")
            fields = ("phase", "iterations", "callbacks", "checksum") if group["name"] == "model" else ("phase", "iterations", "callbacks")
        elif group["name"] == "editor":
            require(row["measured"] is True, "Actual measured native editor work")
            fields = ("phase", *EDITOR_WORK)
            for resource in ("gdi", "user"):
                require(row["resources_after"][resource] <= row["resources_before"][resource], "Editor phase native resource bound")
        else:
            fields = tuple(key for key in row if key not in (
                "wall_ms", "ui_thread_cpu_ms", "kernel_before100ns", "kernel_after100ns",
                "user_before100ns", "user_after100ns", "cpp_allocations"))
        signature.append({field: row[field] for field in fields})
        prefixes = ("owner_kernel", "owner_user", "query_kernel", "query_user", "process_kernel", "process_user") \
            if group["name"] == "editor" else ("kernel", "user")
        for prefix in prefixes:
            a, b = row[prefix + "_before100ns"], row[prefix + "_after100ns"]
            require(type(a) is int and type(b) is int and 0 <= a <= b, "Raw monotonic integer CPU counters")
    if group["name"] == "model":
        source = [r for r in rows if "source" in r]
        require(len(source) == 1, "Actual large-source read inventory")
        require(len([r for r in rows if "setup" in r]) == 32, "Complete32-family setup/storage inventory")
    elif group["name"] == "abi":
        retained = rows[-1]
        require(retained == {"retained_handles_before": 6, "retained_handles_after": 6}, "No ABI handle growth")
    elif group["name"] == "managed":
        require(rows[-1]["resources"] == "post-study" and rows[-1]["gdi"] == 0 and rows[-1]["user"] == 1,
                "Managed no-window GDI/USER resource bounds")
    elif group["name"] == "collection":
        require(len(rows) == 5, "Collection output cardinality")
        retained = rows[-1]
        for field, value in dict(items=1000000, grid_rows=100000, legacy_rows=10000, peers=4,
                                 child_hwnds=5, targets=1, selection_terms=2, editor_scroll_writes=0).items():
            require(retained[field] == value, "Actual retained collection/native bounds")
    elif group["name"] == "editor":
        require(len(rows) == 56, "Full56-record native editor inventory")
        for name in ("actual-native-embedding", "uia-native-contract", "uia-retirement", "editor-resources"):
            found = [r for r in rows if r.get("phase") == name]
            require(len(found) == 1, "Full native provider/lifetime contract")
            signature.extend(found)
        retirements = [r for r in rows if r.get("phase") == "editor-retirement"]
        require(len(retirements) == 1 and retirements[0]["native_destructions"] == 13, "Exact native retirement count")
    if reference is not None:
        require(signature == reference, "Exact baseline/candidate work/effects differ")
    return signature


def run(study, group_name):
    manifest = load(study / "manifest.json")
    group = next(group for group in manifest["groups"] if group["name"] == group_name)
    for item in manifest["identities"]:
        require(sha(Path(item["path"])) == item["sha256"], "Frozen study input changed")
    counts = dict((line.split()[0], int(line.split()[1])) for line in (study / "counts.txt").read_text().splitlines())
    output = study / (manifest.get("block_prefix", "block-") + group_name)
    output.mkdir(exist_ok=False)
    save(output / "manifest.json", manifest)
    environment = dict(os.environ, XUI_PERF_COUNTS=str((study / "counts.txt").resolve()))
    records, reference = [], None
    source_reference = {}
    start = now()
    try:
        for index in range(9):
            for revision in (("candidate", "baseline") if index % 2 else ("baseline", "candidate")):
                kind = "warmup" if index < 2 else "recorded"
                name = f"{kind}-{index:02d}-{revision}"
                record = dict(round=index, revision=revision, kind=kind, started_utc=now(), state="attempting")
                records.append(record)
                save(output / "samples.json", records)
                executable = group["executables"][revision]
                argv = ([manifest["runner"]] if group["desktop"] else []) + [executable] + group["arguments"]
                record["argv"] = argv
                try:
                    with (output / (name + ".log")).open("wb") as stream:
                        result = subprocess.run(argv, env=environment, cwd=Path(executable).parent,
                                                stdout=stream, stderr=subprocess.STDOUT, timeout=200, check=False)
                    record["exit_code"] = result.returncode
                except Exception as error:
                    record["error"] = repr(error)
                    raise
                finally:
                    record["completed_utc"] = now()
                    record["state"] = "retained"
                    save(output / "samples.json", records)
                require(record["exit_code"] == 0, "Failed attempt retained: " + name)
                rows = read_rows(output / (name + ".log"))
                record["rows"] = rows
                save(output / "samples.json", records)
                signature = validate(group, rows, counts, reference)
                if group_name == "model":
                    source_work = [{key: row[key] for key in ("phase", "source_reads", "source_keys", "source_lookups")}
                                   for row in rows if "wall_ms" in row]
                    source_work.extend(row for row in rows if "source" in row)
                    if revision not in source_reference:
                        source_reference[revision] = source_work
                    require(source_work == source_reference[revision], "Source work must be identical across every process of one revision")
                    save(output / "source-work-per-revision.json", source_reference)
                if reference is None:
                    require(revision == "baseline" and index == 0, "Only first baseline warmup establishes work reference")
                    reference = signature
                    save(output / "work-reference.json", reference)
                record["state"] = "validated"
                save(output / "samples.json", records)
                print(group_name, name, "retained", flush=True)
        for item in manifest["identities"]:
            require(sha(Path(item["path"])) == item["sha256"], "Frozen study input changed after block")
        save(output / "block-receipt.json", dict(status="completed", started_utc=start, completed_utc=now(),
                                               processes=18, warmup_pairs=2, recorded_pairs=7,
                                               timed_rows=18 * len(group["phases"]), no_exclusions=True))
    except Exception as error:
        save(output / "block-receipt.json", dict(status="stopped", started_utc=start, completed_utc=now(),
                                               processes=len(records), error=repr(error), no_exclusions=True))
        raise


def metrics(group, rows):
    output = {}
    for row in rows:
        if "wall_ms" not in row:
            continue
        values = {"wall_ms": row["wall_ms"]}
        for thread in (("owner", "query", "process") if group["name"] == "editor" else ("owner",)):
            total = 0
            for component in ("kernel", "user"):
                prefix = thread + "_" + component if group["name"] == "editor" else component
                value = (row[prefix + "_after100ns"] - row[prefix + "_before100ns"]) / 10000
                values[thread + "_" + component + "_ms"] = value
                total += value
            values[thread + "_cpu_ms"] = total
        for field in ("cpp_allocations", "cpp_requested_bytes", "managed_bytes", "caller_cpp_allocations", "caller_cpp_requested_bytes"):
            if field in row:
                values[field] = row[field]
        output[row["phase"]] = values
    output["sum-of-measured-components"] = {
        key: sum(row[key] for row in output.values()) for key in set.intersection(*(set(row) for row in output.values()))
    }
    return output


def pct(candidate, baseline):
    return (candidate / baseline - 1) * 100 if baseline else None


def distribution(values):
    return dict(samples=values, minimum=min(values), median=statistics.median(values), mean=statistics.mean(values),
                p90_nearest_rank=sorted(values)[math.ceil(len(values) * .9) - 1], maximum=max(values))


def analyze(study):
    manifest = load(study / "manifest.json")
    counts = dict((line.split()[0], int(line.split()[1])) for line in (study / "counts.txt").read_text().splitlines())
    comparisons, signals, inventories = [], [], []
    for group in manifest["groups"]:
        directory = study / (manifest.get("block_prefix", "block-") + group["name"])
        block = load(directory / "block-receipt.json")
        records = load(directory / "samples.json")
        require(block["status"] == "completed" and block["processes"] == len(records) == 18, "Complete fixed group")
        order = [(i, r, "warmup" if i < 2 else "recorded") for i in range(9)
                 for r in (("candidate", "baseline") if i % 2 else ("baseline", "candidate"))]
        require([(r["round"], r["revision"], r["kind"]) for r in records] == order, "Exact full process order")
        reference = load(directory / "work-reference.json")
        values = {}
        source_reference = {}
        previous = datetime.datetime.fromisoformat(block["started_utc"])
        for record in records:
            require(record["exit_code"] == 0 and record["state"] == "validated", "No excluded/replaced attempt")
            start = datetime.datetime.fromisoformat(record["started_utc"])
            end = datetime.datetime.fromisoformat(record["completed_utc"])
            require(previous <= start < end and (end - start).total_seconds() < 200, "Serial bounded actual process envelopes")
            previous = end
            raw = read_rows(directory / f"{record['kind']}-{record['round']:02d}-{record['revision']}.log")
            require(raw == record["rows"], "Stored records equal real process output")
            validate(group, raw, counts, reference)
            if group["name"] == "model":
                source_work = [{key: row[key] for key in ("phase", "source_reads", "source_keys", "source_lookups")}
                               for row in raw if "wall_ms" in row]
                source_work.extend(row for row in raw if "source" in row)
                revision = record["revision"]
                if revision not in source_reference:
                    source_reference[revision] = source_work
                require(source_work == source_reference[revision], "Reconstructed source work stable per revision")
            values[record["round"], record["revision"]] = metrics(group, raw)
        if source_reference:
            require(source_reference == load(directory / "source-work-per-revision.json"), "Raw/source-work artifact agrees")
        inventories.append(dict(group=group["name"], processes=18, timed_rows=block["timed_rows"]))
        for phase in values[2, "baseline"]:
            fields = set(values[2, "baseline"][phase]) & set(values[2, "candidate"][phase])
            for metric in sorted(fields):
                baseline = [values[i, "baseline"][phase][metric] for i in range(2, 9)]
                candidate = [values[i, "candidate"][phase][metric] for i in range(2, 9)]
                ratios = [pct(b, a) for a, b in zip(baseline, candidate)]
                row = dict(group=group["name"], phase=phase, metric=metric, baseline=distribution(baseline),
                           candidate=distribution(candidate), absolute_median_delta=statistics.median(candidate) - statistics.median(baseline),
                           median_change_percent=pct(statistics.median(candidate), statistics.median(baseline)),
                           sum_change_percent=pct(sum(candidate), sum(baseline)),
                           paired_change_percent=ratios,
                           paired_median_change_percent=statistics.median(ratios) if all(x is not None for x in ratios) else None,
                           candidate_greater_pairs=sum(b > a for a, b in zip(baseline, candidate)))
                comparisons.append(row)
                if metric in ("wall_ms", "owner_cpu_ms", "query_cpu_ms", "process_cpu_ms"):
                    for statistic in ("median_change_percent", "sum_change_percent", "paired_median_change_percent"):
                        if row[statistic] is not None and row[statistic] > 5:
                            signals.append(dict(group=group["name"], phase=phase, metric=metric, statistic=statistic,
                                                percent=row[statistic], absolute_median_delta=row["absolute_median_delta"]))
    result = dict(protocol="Separate2warmup+7alternatingrecordedpair fixed groups; no pooling across changed images/protocols",
                  inventories=inventories, comparisons=comparisons, over_five_percent_signals=signals,
                  performance_acceptance=not signals)
    save(study / "analysis.json", result)
    print(json.dumps(inventories), flush=True)
    print("Over5% threshold statistics:", len(signals), flush=True)
    for signal in signals:
        print(json.dumps(signal), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("study", type=Path)
    parser.add_argument("--group", choices=("model", "abi", "managed", "editor", "collection"))
    args = parser.parse_args()
    run(args.study.resolve(), args.group) if args.group else analyze(args.study.resolve())

"""Negative contract checks, run after building the native schema exporter."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--native-executable", required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
exporter = args.native_executable.resolve(strict=True)


def run(tree, script, *arguments, succeeds):
    result = subprocess.run([sys.executable, str(tree / "bindings" / script), "--check", *arguments],
                            capture_output=True, text=True)
    if (result.returncode == 0) != succeeds:
        raise AssertionError(f"Unexpected exit {result.returncode}: {script}\n{result.stdout}\n{result.stderr}")
    return result


with tempfile.TemporaryDirectory(prefix="xui-contract-gates-") as directory:
    tree = Path(directory)
    for name in ("include", "bindings"):
        shutil.copytree(root / name, tree / name, ignore=shutil.ignore_patterns(
            "bin", "obj", "target", ".git", "node_modules"))
    outputs = [
        ("generate_features.py", "dotnet/Xui/Native.Features.g.cs"),
        ("generate_features.py", "dotnet/Xui/Features.g.cs"),
        ("generate_features.py", "rust/xui-sys/src/features.rs"),
        ("generate_features.py", "rust/xui/src/features_generated.rs"),
        ("generate_control_styles.py", "dotnet/Xui/ControlStyleCatalog.g.cs"),
        ("generate_control_styles.py", "rust/xui/src/control_style_catalog.g.rs"),
    ]
    for script in ("generate_features.py", "generate_control_styles.py"):
        run(tree, script, succeeds=True)
    for script, name in outputs:
        path = tree / "bindings" / name
        original = path.read_bytes()
        drift = original + b"\n// intentional drift\n"
        path.write_bytes(drift)
        result = run(tree, script, succeeds=False)
        if "stale" not in result.stderr or path.read_bytes() != drift:
            raise AssertionError(f"Check must report drift without rewriting {name}")
        path.write_bytes(original)
    run(tree, "generate_control_styles.py", "--native-executable", str(exporter), succeeds=True)
    run(tree, "generate_control_styles.py", "--native-executable", str(tree / "missing.exe"), succeeds=False)
    catalog = tree / "bindings" / "control_style_catalog.json"
    original = catalog.read_text(encoding="utf-8")
    data = json.loads(original)
    data["schemas"].pop()
    catalog.write_text(json.dumps(data), encoding="utf-8")
    result = run(tree, "generate_control_styles.py", "--native-executable", str(exporter), succeeds=False)
    if "Native style schemas do not match" not in result.stderr:
        raise AssertionError("Native catalog mismatch must fail before generated-file checks")
print("Generated C#/Rust drift, non-writing checks, missing native exporter and native schema mismatch passed.")

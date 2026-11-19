"""Enforce XUI's evaluated target graph and source-level architecture boundaries."""
import argparse
import json
from pathlib import Path
import re
import sys


STANDARD_HEADERS = set("""
algorithm any array atomic barrier bit bitset cassert cctype cerrno cfenv cfloat charconv
chrono cinttypes climits clocale cmath codecvt compare complex concepts condition_variable
coroutine csetjmp csignal cstdarg cstddef cstdint cstdio cstdlib cstring ctime cuchar cwchar
cwctype deque exception execution expected filesystem format forward_list fstream functional
future initializer_list iomanip ios iosfwd iostream istream iterator latch limits list locale
map memory memory_resource mutex new numbers numeric optional ostream queue random ranges
ratio regex scoped_allocator semaphore set shared_mutex source_location span sstream stack
stdexcept stop_token streambuf string string_view strstream syncstream system_error thread
tuple type_traits typeindex typeinfo unordered_map unordered_set utility valarray variant
vector version assert.h ctype.h errno.h fenv.h float.h inttypes.h limits.h locale.h math.h
setjmp.h signal.h stdarg.h stdbool.h stddef.h stdint.h stdio.h stdlib.h string.h time.h
uchar.h wchar.h wctype.h
""".split())
PLATFORM_HEADERS = {"application.hpp", "accessibility.hpp", "native_edit.hpp", "swap_chain_panel.hpp"}
LEGACY_HEADERS = {"file_list.hpp", "file_list_model.hpp", "view_worker.hpp"}
PEERS = {
    "host_peer", "control_host", "basic_host", "scene_host", "navigation_host", "composite_host",
    "collection_host", "grid_host", "file_list_host", "native_field_host", "text_input_host",
    "document_host", "password_host", "date_time_host",
}
PLATFORM_HOSTS = {"window_host", "native_runtime_host", "native_swap_chain_host"}
SERVICE_HEADERS = {"host_peer.hpp", "control_host.hpp", "native_field_host.hpp"}
ABI_SERVICES = {"shell_commands_internal.hpp", "feature_contract.g.hpp", "feature_creation.g.inc"}
CONTROL_PEERS = {"basic_host", "scene_host", "navigation_host", "composite_host"}
FIELD_PEERS = {"text_input_host", "document_host", "password_host", "date_time_host"}
TOKEN = re.compile(
    r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+([^\n]+)', re.M)


class BoundaryError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise BoundaryError(message)


def source_parts(text):
    text = re.sub(r"\\\r?\n", "", text)
    blank = lambda match: re.sub(r"[^\n]", " ", match.group())
    code = TOKEN.sub(blank, text)
    comments_removed = TOKEN.sub(lambda m: blank(m) if m.group().startswith(("//", "/*")) else m.group(), text)
    includes = []
    for match in INCLUDE.finditer(comments_removed):
        if "#" not in code[match.start():match.start(1)]:
            continue
        value = match[1].strip()
        header = re.fullmatch(r'["<]([^">]+)[">]', value)
        require(header is not None, f"Non-literal include cannot be checked: {value}")
        includes.append(header[1])
    require(not re.search(r"#[ \t]*define[ \t]+(?:private|protected)\b", code),
            "Access-control macro bypass is forbidden")
    return code, includes, comments_removed


class Sources:
    def __init__(self, root):
        self.root = root.resolve()
        self.cache = {}

    def path(self, value):
        path = (self.root / value).resolve()
        require(path.is_relative_to(self.root), f"Source escapes the repository: {value}")
        require(path.is_file(), f"Missing architecture input: {value}")
        return path

    def read(self, path):
        if path not in self.cache:
            try:
                self.cache[path] = source_parts(path.read_text(encoding="utf-8-sig"))
            except BoundaryError as error:
                raise BoundaryError(f"{path.relative_to(self.root)}: {error}") from error
        return self.cache[path]

    def closure(self, roots):
        seen, external = set(), set()
        pending = [self.path(p) for p in roots]
        while pending:
            path = pending.pop()
            if path in seen:
                continue
            seen.add(path)
            for name in self.read(path)[1]:
                candidates = [path.parent / name, self.root / "include" / name, self.root / "src" / name]
                found = next((p.resolve() for p in candidates if p.is_file()), None)
                if found is not None:
                    require(found.is_relative_to(self.root), f"{path.relative_to(self.root)} includes outside repository: {name}")
                    pending.append(found)
                else:
                    external.add((str(path.relative_to(self.root)), name))
        return seen, external


def validate_targets(targets):
    require(isinstance(targets, dict) and set(targets) <= {"xui_core", "xui_file_list", "xui_windows", "xui"},
            "Unknown target classification")
    require("xui_core" in targets and "xui_file_list" in targets, "Core and legacy targets must be present")
    allowed = {"xui_core": {"Threads::Threads"}, "xui_file_list": {"xui_core", "Threads::Threads"},
               "xui": {"xui_windows"}}
    for name, row in targets.items():
        require(set(row) == {"sources", "links"}, f"Invalid target description: {name}")
        require(isinstance(row["sources"], list) and row["sources"] and
                all(isinstance(s, str) for s in row["sources"]), f"Missing target sources: {name}")
        require(isinstance(row["links"], list) and all(isinstance(s, str) for s in row["links"]),
                f"Invalid target dependencies: {name}")
        if name in allowed:
            require(set(row["links"]) <= allowed[name], f"{name} has forbidden dependencies: {set(row['links']) - allowed[name]}")
    require("xui_core" in targets["xui_file_list"]["links"], "Legacy library must depend on core")
    require(("xui" in targets) == ("xui_windows" in targets), "Windows and ABI targets must be described together")
    if "xui" in targets:
        require(targets["xui"]["links"] == ["xui_windows"], "ABI must use the Windows runtime target")
        require("xui_file_list" in targets["xui_windows"]["links"], "Windows must retain the isolated legacy dependency")
        require("xui" not in targets["xui_windows"]["links"], "Windows must not depend on its ABI adapter")


def check(root, targets):
    validate_targets(targets)
    source = Sources(root)
    core, _ = source.closure(targets["xui_core"]["sources"])
    for path in core:
        require(path.name not in LEGACY_HEADERS, f"Generic core reaches legacy file-list declaration: {path.relative_to(source.root)}")

    public = source.root / "include" / "xui"
    portable_headers = [str(p.relative_to(source.root)) for p in public.rglob("*.hpp")
                        if p.relative_to(public).as_posix() not in PLATFORM_HEADERS]
    portable, external = source.closure(
        targets["xui_core"]["sources"] + targets["xui_file_list"]["sources"] + portable_headers)
    for path in portable:
        require(path.name not in PLATFORM_HEADERS, f"Portable models reach platform header: {path.relative_to(source.root)}")
        code = source.read(path)[0]
        require(not re.search(r"\b(?:HWND|HDC|HRESULT|HANDLE|WPARAM|LPARAM|LRESULT|DWORD|IUnknown)\b", code),
                f"Portable models use native platform types: {path.relative_to(source.root)}")
        require(not re.search(r"#[ \t]*pragma[ \t]+comment\s*\(\s*lib\b|\b__declspec\s*\(\s*dllimport\b", code),
                f"Portable models bypass target dependencies with native imports: {path.relative_to(source.root)}")
    for path, header in sorted(external):
        require(header in STANDARD_HEADERS, f"{path}: nonstandard/platform include in portable models: {header}")

    families = {}
    if "xui_windows" in targets:
        compiled = {source.path(p) for p in targets["xui_windows"]["sources"]}
        for path in compiled:
            if path.stem.endswith("_host"):
                require(path.stem in PEERS | PLATFORM_HOSTS, f"Unclassified Windows host: {path.name}")
        for peer in sorted(PEERS):
            implementation = source.path(f"src/{peer}.cpp")
            require(implementation in compiled, f"Family host is no longer independently compiled: {peer}")
            closure, _ = source.closure([str(implementation)])
            permitted = {peer, "host_peer"}
            if peer in CONTROL_PEERS:
                permitted.add("control_host")
            if peer in FIELD_PEERS:
                permitted.add("native_field_host")
            for path in closure:
                text = source.read(path)[0]
                require(path.name != "application.hpp" and not path.name.startswith(("application.", "application_", "c_api")),
                        f"{peer} reaches window/ABI implementation instead of host services: {path.relative_to(source.root)}")
                require(path.suffix != ".cpp" or path == implementation,
                        f"{peer} includes another implementation unit: {path.name}")
                require(path.stem not in PEERS or path.stem in permitted,
                        f"{peer} reaches another concrete family instead of host services: {path.name}")
                require(not re.search(r"\bWindow\s*::\s*Impl\b", text),
                        f"{peer} reaches private Window::Impl through {path.relative_to(source.root)}")
                if path.name in SERVICE_HEADERS:
                    require(not re.search(r"\bvoid\s*\*|\b(?:Window|any)\b", text),
                            f"Unrestricted owner escape in host service: {path.name}")
            families[peer] = len(closure)
        abi, _ = source.closure(targets["xui"]["sources"])
        for path in abi:
            if path.is_relative_to(source.root / "src"):
                require(path.parent == source.root / "src" and
                        (path.name.startswith("c_api") or path.name in ABI_SERVICES),
                        f"ABI reaches private host implementation instead of runtime services: {path.name}")
            require(not re.search(r"\bWindow\s*::\s*Impl\b|\bimpl_\b", source.read(path)[0]) or
                    path.parent == source.root / "include" / "xui",
                    f"ABI reaches private runtime state: {path.relative_to(source.root)}")

    exports = set()
    for path in (source.root / "include" / "xui").rglob("*.h"):
        text = source.read(path)[0]
        exports.update(re.findall(r"XUI_API\s+\w+\s+XUI_CALL\s+(\w+)\s*\(", text))
    require(exports, "Public ABI declarations are missing")
    managed_count = rust_count = 0
    managed_root = source.root / "bindings" / "dotnet" / "Xui"
    for path in managed_root.rglob("*.cs"):
        if {"bin", "obj"} & set(path.relative_to(managed_root).parts):
            continue
        text = source.read(path)[2]
        attributes = re.findall(r"\[(?:global::)?(?:System\.Runtime\.InteropServices\.)?"
                                r"(?:LibraryImport|DllImport)(?:Attribute)?\s*\((.*?)\)\]", text, re.S)
        for arguments in attributes:
            library = re.match(r'\s*"([^"]+)"', arguments)
            entry = re.search(r'EntryPoint\s*=\s*"([^"]+)"', arguments)
            require(library is not None and library[1] == "xui" and entry is not None and entry[1] in exports,
                    f"{path.name}: managed import is not a declared XUI ABI service")
            managed_count += 1
    for path in (source.root / "bindings" / "rust" / "xui-sys" / "src").rglob("*.rs"):
        text = source.read(path)[2]
        entries = set(re.findall(r"pub\s+fn\s+(xui_\w+)\s*\(", text))
        for block in re.findall(r'extern\s*"(?:C|system)"\s*\{(.*?)\}', text, re.S):
            entries.update(re.findall(r"\bfn\s+(\w+)\s*\(", block))
        entries.update(re.findall(r'#\[link_name\s*=\s*"([^"]+)"\]', text))
        for library in re.findall(r'#\[link\(\s*name\s*=\s*"([^"]+)"', text):
            require(library == "xui", f"{path.name}: Rust links outside the XUI ABI")
        for entry in entries:
            require(entry in exports, f"{path.name}: Rust import is not a declared XUI ABI service: {entry}")
            rust_count += 1
    require(managed_count and rust_count, "Managed/Rust ABI declarations are missing")
    return dict(targets={name: len(row["sources"]) for name, row in targets.items()},
                portable_files=len(portable), core_files=len(core), peer_closures=families,
                abi_exports=len(exports), managed_imports=managed_count, rust_imports=rust_count,
                checked_files=len(source.cache))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--targets", type=Path, required=True, help="Evaluated CMake source/link graph")
    parser.add_argument("--report", type=Path)
    options = parser.parse_args()
    try:
        result = check(options.root, json.loads(options.targets.read_text(encoding="utf-8")))
    except (BoundaryError, OSError, json.JSONDecodeError) as error:
        print(f"Architecture boundary violation: {error}", file=sys.stderr)
        return 1
    if options.report:
        options.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Architecture boundaries passed: " + json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

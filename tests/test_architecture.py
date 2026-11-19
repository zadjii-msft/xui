import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import architecture


class ArchitectureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="xui-boundary-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.put("include/xui/core.hpp", "#pragma once\n#include <memory>\nclass Element {};\n")
        self.put("include/xui/file_list_model.hpp", "#pragma once\n#include <vector>\nclass FileSnapshot {};\n")
        self.put("include/xui/application.hpp", "class Window { struct Impl; };\n")
        self.put("include/xui/xui.h", "XUI_API int XUI_CALL xui_example(void);\n")
        self.put("src/core.cpp", '#include "xui/core.hpp"\n')
        self.put("src/file_list.cpp", '#include "xui/file_list_model.hpp"\n')
        self.put("src/c_api.cpp", '#include "xui/application.hpp"\n#include "xui/xui.h"\n')
        self.put("bindings/dotnet/Xui/Native.cs", '[LibraryImport("xui", EntryPoint = "xui_example")]\n')
        self.put("bindings/rust/xui-sys/src/lib.rs", 'unsafe extern "C" { pub fn xui_example() -> i32; }\n')
        for peer in architecture.PEERS:
            self.put(f"src/{peer}.cpp", f'#include "{peer}.hpp"\n')
            self.put(f"src/{peer}.hpp", "#pragma once\n" +
                     ('class PeerHostServices {};\n' if peer == "host_peer" else '#include "host_peer.hpp"\n'))
        self.targets = {
            "xui_core": {"sources": ["src/core.cpp"], "links": []},
            "xui_file_list": {"sources": ["src/file_list.cpp"], "links": ["xui_core", "Threads::Threads"]},
            "xui_windows": {"sources": [f"src/{p}.cpp" for p in sorted(architecture.PEERS)], "links": ["xui_file_list"]},
            "xui": {"sources": ["src/c_api.cpp"], "links": ["xui_windows"]},
        }

    def put(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def reject(self, message):
        with self.assertRaisesRegex(architecture.BoundaryError, message):
            architecture.check(self.root, self.targets)

    def test_valid_graph_and_public_abi(self):
        report = architecture.check(self.root, self.targets)
        self.assertEqual(len(report["peer_closures"]), 14)
        self.assertEqual(report["managed_imports"], 1)
        self.assertEqual(report["rust_imports"], 1)

    def test_core_windows_dependency(self):
        for dependency in ("xui_windows", "user32", "$<LINK_ONLY:xui_windows>"):
            with self.subTest(dependency=dependency):
                self.targets["xui_core"]["links"] = [dependency]
                self.reject("forbidden dependencies")

    def test_legacy_and_binding_target_dependencies(self):
        self.targets["xui_file_list"]["links"].append("user32")
        self.reject("forbidden dependencies")
        self.targets["xui_file_list"]["links"].pop()
        self.targets["xui"]["links"].append("xui_core")
        self.reject("forbidden dependencies")

    def test_direct_and_transitive_platform_include(self):
        self.put("src/core.cpp", '#include "indirect.hpp"\n')
        for header in ("windows.h", "Windows.h", "d2d1.h", "wrl/client.h"):
            with self.subTest(header=header):
                self.put("src/indirect.hpp", f"#include <{header}>\n")
                self.reject("platform include")

    def test_public_portable_header_is_checked_without_current_source_use(self):
        self.put("include/xui/models/new_model.hpp", "#include <windows.h>\n")
        self.reject("platform include")

    def test_native_link_pragma_and_import_declaration(self):
        for source in ('#pragma comment(lib, "user32.lib")', '__declspec(dllimport) unsigned NativeCall();'):
            with self.subTest(source=source):
                self.put("src/core.cpp", source)
                self.reject("bypass target dependencies")

    def test_portable_platform_type_and_unknown_include(self):
        self.put("src/core.cpp", "HWND owner;\n")
        self.reject("native platform types")
        self.put("src/core.cpp", "#include <unreviewed_backend.h>\n")
        self.reject("platform include")

    def test_legacy_declarations_cannot_return_to_core(self):
        self.put("src/core.cpp", '#include "xui/file_list_model.hpp"\n')
        self.reject("Generic core reaches legacy")

    def test_platform_header_reexport_is_rejected(self):
        self.put("include/xui/core.hpp", '#include "application.hpp"\n')
        self.reject("platform header")

    def test_macros_and_continued_includes(self):
        self.put("src/core.cpp", "#include BACKEND_HEADER\n")
        self.reject("Non-literal include")
        self.put("src/core.cpp", '#\\\ninclude <windows.h>\n')
        self.reject("platform include")

    def test_comments_and_literals_do_not_create_dependencies(self):
        self.put("src/core.cpp", """
// #include <windows.h>
/* HWND fake; #include "application.cpp" */
const char* text = "http://example.test/Window::Impl";
const char* raw = R"sample(
#include <windows.h>
HWND fake;
)sample";
#include /* explanation */ <string>
""")
        architecture.check(self.root, self.targets)

    def test_peer_coordinator_dependency_and_private_type(self):
        self.put("src/basic_host.hpp", '#include "indirect.hpp"\n')
        self.put("src/indirect.hpp", '#include "xui/application.hpp"\n')
        self.reject("instead of host services")
        self.put("src/indirect.hpp", "using Owner = Window::Impl;\n")
        self.reject("private Window::Impl")

    def test_peer_cpp_inclusion_and_access_bypass(self):
        self.put("src/application.cpp", "struct Coordinator {};\n")
        self.put("src/basic_host.cpp", '#include "application.cpp"\n')
        self.reject("instead of host services")
        self.put("src/basic_host.cpp", "#define private public\n")
        self.reject("Access-control macro")

    def test_concrete_families_do_not_reach_each_others_state(self):
        self.put("src/collection_host.cpp", '#include "grid_host.hpp"\n')
        self.reject("another concrete family")

    def test_service_owner_escape(self):
        for escape in ("virtual void* owner() = 0;", "virtual Window& owner() = 0;", "std::any state;"):
            with self.subTest(escape=escape):
                self.put("src/control_host.hpp", "class ControlHostServices { " + escape + " };")
                self.reject("Unrestricted owner escape")

    def test_families_require_independent_compilation_and_classification(self):
        self.targets["xui_windows"]["sources"].remove("src/basic_host.cpp")
        self.reject("no longer independently compiled")
        self.targets["xui_windows"]["sources"].append("src/basic_host.cpp")
        self.put("src/extra_host.cpp", "")
        self.targets["xui_windows"]["sources"].append("src/extra_host.cpp")
        self.reject("Unclassified Windows host")

    def test_binding_private_host_dependency(self):
        self.put("src/c_api.cpp", '#include "host_peer.hpp"\n')
        self.reject("instead of runtime services")
        self.put("src/c_api.cpp", "auto state = window.impl_;\n")
        self.reject("private runtime state")

    def test_explicit_binding_runtime_service(self):
        self.put("src/shell_commands_internal.hpp", "class AsyncShellMenu {};\n")
        self.put("src/c_api.cpp", '#include "shell_commands_internal.hpp"\n')
        architecture.check(self.root, self.targets)

    def test_binding_private_subdirectory_is_not_an_escape(self):
        self.put("src/nested/renderer.hpp", "class PrivateRenderer {};")
        self.put("src/c_api.cpp", '#include "nested/renderer.hpp"\n')
        self.reject("instead of runtime services")

    def test_managed_imports_require_declared_runtime_services(self):
        for attribute in (
            '[LibraryImport("user32", EntryPoint = "xui_example")]',
            '[LibraryImport("xui", EntryPoint = "xui_private")]',
            '[System.Runtime.InteropServices.LibraryImport("user32", EntryPoint = "xui_example")]',
            '[global::System.Runtime.InteropServices.DllImportAttribute("xui", EntryPoint = "private")]',
            '[DllImport("xui")]',
        ):
            with self.subTest(attribute=attribute):
                self.put("bindings/dotnet/Xui/Native.cs", attribute)
                self.reject("managed import")

    def test_rust_imports_and_link_aliases_require_public_exports(self):
        for declaration in (
            'pub fn xui_private();',
            '#[link_name = "private_entry"] pub fn xui_example();',
            'unsafe extern "system" { fn private_entry(); }',
        ):
            with self.subTest(declaration=declaration):
                self.put("bindings/rust/xui-sys/src/lib.rs", declaration)
                self.reject("Rust import")

    def test_rust_library_boundary(self):
        self.put("bindings/rust/xui-sys/src/lib.rs", '#[link(name = "user32")] unsafe extern "C" { pub fn xui_example(); }')
        self.reject("Rust links outside")

    def test_nested_binding_modules_are_checked(self):
        managed = self.put("bindings/dotnet/Xui/Interop/Calls.cs", '[LibraryImport("user32", EntryPoint = "private")]')
        self.reject("managed import")
        managed.unlink()
        self.put("bindings/rust/xui-sys/src/nested/calls.rs", 'unsafe extern "C" { fn private_entry(); }')
        self.reject("Rust import")

    def test_managed_build_intermediates_are_not_source_inputs(self):
        self.put("bindings/dotnet/Xui/obj/Generated.cs", '[LibraryImport("user32", EntryPoint = "private")]')
        self.put("bindings/dotnet/Xui/bin/Fixture.cs", '[LibraryImport("user32", EntryPoint = "private")]')
        architecture.check(self.root, self.targets)

    def test_missing_and_external_sources_fail_closed(self):
        self.targets["xui_core"]["sources"] = ["src/missing.cpp"]
        self.reject("Missing architecture input")
        self.targets["xui_core"]["sources"] = ["../outside.cpp"]
        self.reject("escapes the repository")

    def test_cli_fails_without_rewriting_source(self):
        self.put("src/core.cpp", "#include <windows.h>\n")
        manifest = self.put("targets.json", json.dumps(self.targets))
        before = (self.root / "src/core.cpp").read_bytes()
        result = subprocess.run([sys.executable, str(ROOT / "tools/architecture.py"), "--root", str(self.root),
                                 "--targets", str(manifest)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Architecture boundary violation", result.stderr)
        self.assertEqual(before, (self.root / "src/core.cpp").read_bytes())


if __name__ == "__main__":
    unittest.main()

"""Prove CMake configure and incremental builds reject deliberate boundary violations."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from test_architecture import ArchitectureTests, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True, type=Path)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--platform", default="")
    parser.add_argument("--report", required=True, type=Path)
    options = parser.parse_args()
    fixture = ArchitectureTests()
    fixture.setUp()
    records = []
    try:
        fixture.put("tools/architecture.py", (ROOT / "tools/architecture.py").read_text())
        fixture.put("cmake/Architecture.cmake", (ROOT / "cmake/Architecture.cmake").read_text())
        project = [
            "cmake_minimum_required(VERSION 3.24)", "project(BoundaryFixture LANGUAGES CXX)",
            "enable_testing()", "option(BUILD_TESTING \"Boundary fixtures\" ON)",
        ]
        for name, row in fixture.targets.items():
            project.append(f"add_library({name} STATIC {' '.join(row['sources'])})")
            project.append(f'target_include_directories({name} PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}/include")')
            if row["links"]:
                # The synthetic model has no thread use; omit that imported target.
                links = [link for link in row["links"] if link != "Threads::Threads"]
                project.append(f"target_link_libraries({name} PUBLIC {' '.join(links)})")
        project_text = "\n".join(project) + "\n"
        fixture.put("CMakeLists.txt", project_text + "include(cmake/Architecture.cmake)\n")
        build = fixture.root / "build"
        configure = [str(options.cmake), "-S", str(fixture.root), "-B", str(build), "-G", options.generator,
                     f"-DPython3_EXECUTABLE={sys.executable}"]
        if options.platform:
            configure += ["-A", options.platform]
        def build_target(target):
            return [str(options.cmake), "--build", str(build), "--target", target, "--config", "Release"]
        incremental = build_target("xui_core")

        def run(name, command, expected, message=None):
            process = subprocess.run(command, capture_output=True, text=True, timeout=120)
            text = process.stdout + process.stderr
            records.append(dict(name=name, command=command, exit_code=process.returncode, output=text))
            options.report.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
            if (process.returncode == 0) != expected or (message is not None and message not in text):
                raise RuntimeError(f"Unexpected CMake outcome for {name}:\n{text}")

        run("valid-configure", configure, True, "Architecture boundaries passed")
        run("valid-incremental-build", incremental, True, "Architecture boundaries passed")
        fixture.put("include/xui/core.hpp", "#include <windows.h>\n")
        run("header-edit-without-reconfigure", incremental, False, "nonstandard/platform include")
        for target in ("xui_file_list", "xui_windows", "xui"):
            run(f"header-edit-blocks-{target}", build_target(target), False, "nonstandard/platform include")
        run("invalid-include-during-configure", configure, False, "architecture boundary check failed")
        fixture.put("include/xui/core.hpp", "#pragma once\n#include <memory>\nclass Element {};\n")
        run("valid-configure-after-repair", configure, True)
        fixture.put("src/basic_host.cpp", '#include "xui/application.hpp"\n')
        run("peer-edit-without-reconfigure", build_target("xui_windows"), False, "instead of host services")
        fixture.put("src/basic_host.cpp", '#include "basic_host.hpp"\n')
        fixture.put("CMakeLists.txt", project_text +
                    "target_link_libraries(xui_core PRIVATE user32)\ninclude(cmake/Architecture.cmake)\n")
        run("invalid-link-with-testing-disabled", configure + ["-DBUILD_TESTING=OFF"], False,
            "xui_core cannot link user32")
        fixture.put("CMakeLists.txt", project_text +
                    "target_link_libraries(xui_core INTERFACE user32)\ninclude(cmake/Architecture.cmake)\n")
        run("invalid-interface-link-with-testing-disabled", configure + ["-DBUILD_TESTING=OFF"], False,
            "xui_core cannot link user32")
        fixture.put("CMakeLists.txt", project_text +
                    "include(cmake/Architecture.cmake)\ntarget_link_libraries(xui_core INTERFACE user32)\n")
        run("invalid-link-after-guard-include", configure + ["-DBUILD_TESTING=OFF"], False,
            "xui_core cannot link user32")
        fixture.put("CMakeLists.txt", project_text + "include(cmake/Architecture.cmake)\n")
        run("missing-required-python", configure + ["-DBUILD_TESTING=ON", "-DPython3_EXECUTABLE=" +
            str(fixture.root / "missing-python.exe")], False, "Python3")
        run("final-repaired-configure", configure + ["-DBUILD_TESTING=ON"], True)
        run("final-repaired-incremental-build", incremental, True)
        print(f"Architecture CMake integration: {len(records)} verified configure/build outcomes.")
    finally:
        fixture.doCleanups()


if __name__ == "__main__":
    main()

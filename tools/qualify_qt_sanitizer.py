#!/usr/bin/env python3
"""Strict standalone Qt Quick qualification; no Mantis build or runtime imports."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


PROBE_SHA256 = "2cb0ef9093ebfb0631e1623f577c6856dfaada0b7569cee49d3f92894cc5e3ab"
SOURCE = Path(__file__).resolve().parents[1] / "tests/qualification/qt-sanitizer"
SANITIZER_ERROR = re.compile(
    r"ERROR: (?:AddressSanitizer|LeakSanitizer)|SUMMARY: (?:AddressSanitizer|UndefinedBehaviorSanitizer)"
    r"|AddressSanitizer:DEADLYSIGNAL|runtime error:"
)


class InvalidEvidence(Exception):
    pass


class Blocked(Exception):
    pass


def require(condition, reason):
    if not condition:
        raise InvalidEvidence(reason)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inside(path, prefix):
    return Path(path).resolve().is_relative_to(prefix.resolve())


def environment(output):
    # An allowlist prevents inherited sanitizer suppressions, loader overrides,
    # QML/Qt overrides, compiler flags and host settings from weakening a run.
    runtime = output / "runtime"
    runtime.mkdir(mode=0o700)
    return {
        "PATH": "/usr/bin:/bin",
        "HOME": os.environ["HOME"],
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "XDG_RUNTIME_DIR": str(runtime),
        "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
        "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
        "QT_QPA_PLATFORM": "offscreen",
        "QT_QUICK_BACKEND": "software",
        "QT_DEBUG_PLUGINS": "1",
        "QML_IMPORT_TRACE": "1",
        "QSG_INFO": "1",
        "QT_LOGGING_RULES": "qt.scenegraph.*=true;qt.quick.*=true",
        "LD_DEBUG": "libs",
    }


def check_version(actual, expected):
    if actual != expected:
        raise Blocked(f"Qt version {actual!r}; required {expected}")


def qtpaths(prefix):
    for relative in ("bin/qtpaths", "bin/qtpaths6", "lib/qt6/bin/qtpaths6"):
        candidate = prefix / relative
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise Blocked(f"No executable qtpaths in explicit prefix {prefix}")


def check_linkage(text, prefix, version):
    require("not found" not in text, "Unresolved dynamic dependency")
    require("libasan.so" in text and "libubsan.so" in text, "Missing ASan/UBSan runtime linkage")
    paths = re.findall(r"libQt6\S+\s+=>\s+(\S+)", text)
    require(paths, "No dynamic Qt dependency evidence")
    for path in paths:
        require(inside(path, prefix), f"Qt library outside explicit prefix: {path}")
        require(Path(path).resolve().name.endswith(f".so.{version}"), f"Qt library version mismatch: {path}")
    return paths


def check_runtime(text, prefix, version):
    paths = re.findall(r"calling init: (\S+)", text)
    libraries = [p for p in paths if Path(p).name.startswith("libQt6")]
    for module in ("Core", "Gui", "Qml", "Quick"):
        require(any(Path(p).name.startswith(f"libQt6{module}.so") for p in libraries),
                f"Missing actual runtime Qt{module} initialization")
    for path in libraries:
        require(inside(path, prefix), f"Runtime Qt library outside prefix: {path}")
        require(Path(path).resolve().name.endswith(f".so.{version}"), f"Runtime Qt version mismatch: {path}")
    plugins = re.findall(r'"([^"\n]+)" loaded library', text)
    require(plugins, "Missing actual plugin load evidence")
    require(any(Path(p).name == "libqoffscreen.so" for p in plugins), "Offscreen QPA not loaded")
    require(any(Path(p).name == "libquickwindowplugin.so" for p in plugins), "QML Window plugin not loaded")
    metadata = {}
    for path, data in re.findall(r"Found metadata in lib ([^\n]+), metadata=\n(\{.*?\n\})", text, re.S):
        try:
            metadata[path] = json.loads(data)["version"]
        except (ValueError, KeyError) as error:
            raise InvalidEvidence(f"Malformed plugin metadata: {path}") from error
    major, minor, _ = map(int, version.split("."))
    for path in plugins:
        require(inside(path, prefix), f"Loaded plugin outside prefix: {path}")
        require(path in metadata, f"Missing loaded plugin version: {path}")
        # Qt plugin metadata encodes major/minor, with patch intentionally zero.
        require(metadata[path] >> 8 == (major << 8 | minor), f"Plugin version mismatch: {path}")
    imports = re.findall(r'addImportPath: "([^"\n]+)"', text)
    require(any(inside(p, prefix) for p in imports if not p.startswith("qrc:")),
            "Missing isolated QML import path")
    require("Loading backend software" in text, "Software scenegraph backend not initialized")
    require('QImageReader size QSize(32, 32)' in text, "Original image decode not observed")
    require("softwarecontext.renderer: render QRegion(0,0 320x240)" in text,
            "Original 320x240 scenegraph workload not rendered")
    require("Frame rendered with 'software' renderloop" in text, "No completed software frame")
    require(not re.search(r"Cannot load|module .+ is not installed|Error decoding|Cannot open:", text),
            "Plugin/QML/image loading failure")
    return {"runtime_qt_libraries": sorted(set(libraries)), "loaded_plugins": plugins,
            "qml_import_paths": imports, "image_size": [32, 32], "render_size": [320, 240]}


def check_positive(returncode, text, prefix, version):
    require(returncode == 0, f"Real probe exited {returncode}")
    require(not SANITIZER_ERROR.search(text), "Unexpected sanitizer diagnostic in real probe")
    return check_runtime(text, prefix, version)


def check_negative(returncode, text, version):
    require(returncode > 0, "Intentional leak control did not exit nonzero")
    require(f"runtime Qt {version}" in text, "Wrong Qt runtime in leak control")
    require("ERROR: LeakSanitizer: detected memory leaks" in text, "LSan did not detect intentional leak")
    require("leakForDetectorControl" in text, "Leak stack does not identify intentional allocation")
    require("123 byte(s) leaked in 1 allocation(s)" in text, "Unexpected leak-control allocation report")
    require(not re.search(r"ERROR: AddressSanitizer|runtime error:|AddressSanitizer:DEADLYSIGNAL", text),
            "Unexpected non-leak sanitizer failure in negative control")


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def run(command, name, output, env, summary):
    log = output / f"{name}.log"
    with log.open("wb") as stream:
        result = subprocess.run([str(x) for x in command], cwd=output, env=env,
                                stdout=stream, stderr=subprocess.STDOUT)
    record = {"name": name, "argv": [str(x) for x in command], "returncode": result.returncode,
              "log": log.name}
    summary["commands"].append(record)
    write_json(output / f"{name}.exit.json", record)
    return result.returncode, log.read_text(errors="replace")


def checked(command, name, output, env, summary):
    code, text = run(command, name, output, env, summary)
    if code != 0:
        raise Blocked(f"Prerequisite command {name} exited {code}; see {name}.log")
    return text


def verify_manifest(output):
    entries = (output / "SHA256SUMS").read_text().splitlines()
    require(entries, "Empty evidence manifest")
    for entry in entries:
        expected, relative = entry.split("  ", 1)
        path = output / relative
        require(inside(path, output) and path.is_file(), f"Missing evidence: {relative}")
        require(digest(path) == expected, f"Evidence checksum mismatch: {relative}")


def verify_complete(output):
    """Recheck raw required evidence, rather than trusting a PASS summary."""
    verify_manifest(output)
    summary = json.loads((output / "summary.json").read_text())
    require(summary["status"] == "PASS", "Qualification did not pass")
    prefix = Path(summary["qt_prefix"])
    version = summary["qt_version"]
    require(version in ("6.4.2", "6.9.2"), "Unsupported evidence version")
    info = json.loads((output / "qualification.json").read_text())
    require(info == {"qt_version": version, "compiler_id": "GNU", "compiler_version": "13.3.0"},
            "Missing/invalid CMake Qt/compiler metadata")
    require(digest(output / "inputs/qt-leak-probe.cpp") == PROBE_SHA256, "Probe input checksum mismatch")
    env = json.loads((output / "environment.json").read_text())
    require(env["ASAN_OPTIONS"] == "detect_leaks=1:halt_on_error=1", "Leak/ASan policy mismatch")
    require(env["UBSAN_OPTIONS"] == "halt_on_error=1:print_stacktrace=1", "UBSan policy mismatch")
    require(env["QT_QPA_PLATFORM"] == "offscreen" and env["QT_QUICK_BACKEND"] == "software",
            "Render configuration mismatch")
    require(not any(k in env for k in ("LSAN_OPTIONS", "LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH",
                                       "QML_IMPORT_PATH", "QML2_IMPORT_PATH")), "Unexpected environment override")
    names = [record["name"] for record in summary["commands"]]
    required = {"qtpaths", "compiler", "cmake-version", "ninja-version", "configure", "build",
                "probe-1", "probe-2", "probe-3", "intentional-leak"}
    for target in ("qt-leak-probe", "leak-detector-selftest"):
        required.update(f"{target}-{kind}" for kind in ("ldd", "readelf", "symbols", "sections"))
        text = (output / f"{target}-ldd.log").read_text()
        check_linkage(text, prefix, version)
        symbols = (output / f"{target}-symbols.log").read_text()
        require("__asan_init" in symbols and "__ubsan_handle_" in symbols, "Missing instrumentation")
        require(".debug_info" in (output / f"{target}-sections.log").read_text(), "Missing debug info")
    require(set(names) == required and len(names) == len(required), "Missing/duplicate command evidence")
    for record in summary["commands"]:
        name = record["name"]
        require(record["log"] == f"{name}.log", "Unexpected evidence log path")
        require((output / record["log"]).is_file(), f"Missing raw log: {name}")
        require(json.loads((output / f"{name}.exit.json").read_text()) == record, "Exit record mismatch")
        if name.startswith("probe-"):
            check_positive(record["returncode"], (output / record["log"]).read_text(), prefix, version)
        elif name == "intentional-leak":
            check_negative(record["returncode"], (output / record["log"]).read_text(), version)
        else:
            require(record["returncode"] == 0, f"Failed required command: {name}")
    require(len(summary["probe_runs"]) == 3 and all(r["returncode"] == 0 for r in summary["probe_runs"]),
            "Three consecutive clean runs not recorded")
    commands = json.loads((output / "compile_commands.json").read_text())
    require(len(commands) == 2 and {Path(c["file"]).name for c in commands} ==
            {"qt-leak-probe.cpp", "leak-detector-selftest.cpp"}, "Missing/invalid compilation records")
    for command in commands:
        text = command["command"]
        require(all(flag in text for flag in ("-O0", "-g", "-fsanitize=address,undefined",
                                               "-fno-omit-frame-pointer")), "Required compile flag missing")
        require("-fno-sanitize" not in text, "Instrumentation disabled")


def finalize(output, summary):
    write_json(output / "summary.json", summary)
    files = sorted(p for p in output.rglob("*") if p.is_file()
                   and p.name != "SHA256SUMS" and not inside(p, output / "build"))
    (output / "SHA256SUMS").write_text("".join(
        f"{digest(p)}  {p.relative_to(output)}\n" for p in files))
    verify_manifest(output)


def qualify(prefix, version, output, summary):
    if not prefix.is_dir():
        raise Blocked(f"Qt prefix unavailable: {prefix}")
    for tool in ("cmake", "ninja", "g++", "ldd", "readelf", "nm"):
        if not Path("/usr/bin", tool).is_file():
            raise Blocked(f"Missing prerequisite /usr/bin/{tool}")
    require(digest(SOURCE / "qt-leak-probe.cpp") == PROBE_SHA256, "Original probe fixture hash mismatch")
    inputs = output / "inputs"
    inputs.mkdir()
    for source in SOURCE.iterdir():
        if source.suffix in (".cpp", ".txt"):
            shutil.copy2(source, inputs / source.name)
    shutil.copy2(Path(__file__), inputs / Path(__file__).name)
    env = environment(output)
    write_json(output / "environment.json", env)
    # Loader tracing is only needed for probe execution, not tool diagnostics.
    tool_env = {k: v for k, v in env.items() if k != "LD_DEBUG"}
    paths_text = checked([qtpaths(prefix), "--query"], "qtpaths", output, tool_env, summary)
    paths = dict(line.split(":", 1) for line in paths_text.splitlines() if ":" in line)
    check_version(paths.get("QT_VERSION"), version)
    require(Path(paths["QT_INSTALL_PREFIX"]).resolve() == prefix, "qtpaths prefix mismatch")
    compiler = checked(["/usr/bin/g++", "-dumpfullversion"], "compiler", output, tool_env, summary).strip()
    if compiler != "13.3.0":
        raise Blocked(f"Required GCC13.3.0, found {compiler}; no compiler fallback")
    checked(["/usr/bin/cmake", "--version"], "cmake-version", output, tool_env, summary)
    checked(["/usr/bin/ninja", "--version"], "ninja-version", output, tool_env, summary)
    shutil.copy2("/etc/os-release", output / "os-release")
    build = output / "build"
    qt_cmake = Path(paths["QT_INSTALL_LIBS"]) / "cmake/Qt6"
    checked(["/usr/bin/cmake", "-S", inputs, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
             "-DCMAKE_CXX_COMPILER=/usr/bin/g++", f"-DCMAKE_PREFIX_PATH={prefix}",
             f"-DQt6_DIR={qt_cmake}", f"-DQUALIFICATION_QT_VERSION={version}"],
            "configure", output, tool_env, summary)
    checked(["/usr/bin/cmake", "--build", build, "--parallel", "4"], "build", output, tool_env, summary)
    shutil.copy2(build / "compile_commands.json", output / "compile_commands.json")
    shutil.copy2(build / "qualification.json", output / "qualification.json")
    info = json.loads((build / "qualification.json").read_text())
    require(info == {"qt_version": version, "compiler_id": "GNU", "compiler_version": "13.3.0"},
            "CMake Qt/compiler metadata mismatch")
    for target in ("qt-leak-probe", "leak-detector-selftest"):
        binary = build / target
        text = checked(["/usr/bin/ldd", binary], f"{target}-ldd", output, tool_env, summary)
        check_linkage(text, prefix, version)
        checked(["/usr/bin/readelf", "-d", binary], f"{target}-readelf", output, tool_env, summary)
        symbols = checked(["/usr/bin/nm", "-D", binary], f"{target}-symbols", output, tool_env, summary)
        require("__asan_init" in symbols and "__ubsan_handle_" in symbols,
                f"Missing ASan/UBSan instrumentation: {target}")
        sections = checked(["/usr/bin/readelf", "--sections", binary], f"{target}-sections",
                           output, tool_env, summary)
        require(".debug_info" in sections, f"Missing debug symbols: {target}")
    for attempt in range(1, 4):
        code, text = run([build / "qt-leak-probe"], f"probe-{attempt}", output, env, summary)
        summary["probe_runs"].append({"attempt": attempt, "returncode": code})
        # Stop at the first genuine candidate failure; do not run more probes.
        evidence = check_positive(code, text, prefix, version)
        summary["probe_runs"][-1]["evidence"] = evidence
    code, text = run([build / "leak-detector-selftest"], "intentional-leak", output, env, summary)
    check_negative(code, text, version)
    summary["intentional_leak"] = {"returncode": code, "expected_leak_detected": True}
    summary["status"] = "PASS"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-prefix", type=Path)
    parser.add_argument("--expected-version", choices=("6.9.2", "6.4.2"), default="6.9.2",
                        help="6.4.2 is for comparison only; real probe leaks still fail")
    parser.add_argument("--output-dir", type=Path, help="New, empty evidence/build directory")
    parser.add_argument("--verify-evidence", type=Path, help="Recheck manifest and all required raw evidence")
    args = parser.parse_args(argv)
    if args.verify_evidence:
        if args.qt_prefix or args.output_dir:
            parser.error("--verify-evidence cannot be combined with a new run")
        try:
            verify_complete(args.verify_evidence.resolve())
        except (InvalidEvidence, OSError, ValueError, KeyError) as error:
            print(f"FAIL: {error}")
            return 1
        print("PASS: manifest and required raw evidence verified")
        return 0
    if not args.qt_prefix or not args.output_dir:
        parser.error("--qt-prefix and --output-dir are required for a new run")
    output = args.output_dir.resolve()
    protected = Path("/home/martin/.ao/data/worktrees/mantis-studio/mantis-studio-3")
    baseline = Path("/home/martin/src/mantis-studio/build/baseline-studio-sanitizers")
    if inside(output, protected) or inside(output, baseline):
        parser.error("Output must not touch protected or original baseline artifacts")
    if output.exists() and any(output.iterdir()):
        parser.error("Output directory must be empty; existing evidence is never overwritten")
    output.mkdir(parents=True, exist_ok=True)
    summary = {"status": "BLOCKED", "qt_prefix": str(args.qt_prefix.resolve()),
               "qt_version": args.expected_version, "probe_sha256": PROBE_SHA256,
               "commands": [], "probe_runs": []}
    try:
        qualify(args.qt_prefix.resolve(), args.expected_version, output, summary)
    except Blocked as error:
        summary.update(status="BLOCKED", reason=str(error))
    except (InvalidEvidence, OSError, ValueError, KeyError) as error:
        summary.update(status="FAIL", reason=str(error))
    finalize(output, summary)
    if summary["status"] == "PASS":
        try:
            verify_complete(output)
        except (InvalidEvidence, OSError, ValueError, KeyError) as error:
            summary.update(status="FAIL", reason=f"Final evidence verification: {error}")
            finalize(output, summary)
    print(json.dumps(summary, indent=2))
    return 0 if summary["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())

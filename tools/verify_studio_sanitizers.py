#!/usr/bin/env python3
"""Read-only, fail-closed verifier for private current-source Studio evidence.

Reuses PR #4/#5's receipt, instrumentation and CTest cross-checking approach.
Trust anchors are supplied by the reviewer, never a historical baseline constant.
Receipts are local evidence, not independently authenticated build attestations.
"""

import argparse
import hashlib
import json
from pathlib import Path
import posixpath
import re
import shlex
import sys
import xml.etree.ElementTree as ET

STRICT = {
    "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
    "QT_QPA_PLATFORM": "offscreen",
    "QT_QUICK_BACKEND": "software",
}
FORBIDDEN = {"LSAN_OPTIONS", "LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH",
             "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QSG_RENDER_LOOP", "CTEST_PARALLEL_LEVEL"}
FINDING = re.compile(
    r"(?:ERROR|SUMMARY): (?:AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer)"
    r"|AddressSanitizer:DEADLYSIGNAL|runtime error:|LeakSanitizer has encountered a fatal error"
)
FOCUSED = {
    "studio-calibration-controller", "studio-calibration-daemon", "studio-calibration-qml",
    "studio-scan-qml", "studio-scan-stress", "studio-scan-surfaces",
    "studio-devices-qml", "studio-devices-stress", "studio-projects-qml",
    "studio-projects-scroll", "studio-home-qml", "studio-ui-m0-qml",
    "studio-bridge-errors",
}
REPEATED = {
    "studio-home-qml", "studio-projects-qml", "studio-projects-resize", "studio-projects-scroll",
    "studio-devices-qml", "studio-devices-stress", "studio-scan-qml", "studio-scan-stress",
    "studio-scan-surfaces", "studio-ui-m0-qml",
}
GUI_BINARIES = {
    "mantis-studio", "mantis-studio-scan-tests", "mantis-studio-devices-tests",
    "mantis-studio-projects-tests", "mantis-studio-home-tests", "mantis-studio-ui-m0-tests",
    "mantis-studio-calibration-tests", "mantis-studio-calibration-qml",
    "mantis-studio-calibration-integration", "mantis-studio-bridge-tests", "mantis-studio-core-tests",
}


class InvalidEvidence(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise InvalidEvidence(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()


def check_environment(env):
    require(all(env.get(k) == v for k, v in STRICT.items()), "Strict runtime environment mismatch")
    require(not any(env.get(k) for k in FORBIDDEN), "Forbidden runtime override")


def check_application_fault(text):
    require("ERROR: AddressSanitizer: heap-use-after-free" in text and
            "MeasurementView::~MeasurementView" in text and "apps/studio/preview.hpp" in text and
            "tests/unit/studio_scan.cpp" in text, "Missing actual application-owned primary ASan control")


def selection_pattern(names):
    # CTest uses cmsys's POSIX-style regex, not Python's non-capturing groups.
    return "^(" + "|".join(re.escape(n) for n in sorted(names)) + ")$"


class Evidence:
    def __init__(self, root, anchor):
        self.root = Path(root).resolve()
        require(digest(self.root / "manifest.json") == anchor, "Evidence trust anchor mismatch")
        self.files = json.loads((self.root / "manifest.json").read_text())["files"]
        require(isinstance(self.files, dict) and bool(self.files), "Empty evidence manifest")
        for name, expected in self.files.items():
            path = self.path(name)
            require(path.is_file() and not path.is_symlink(), "Missing/unsafe evidence: " + name)
            require(digest(path) == expected, "Evidence checksum mismatch: " + name)

    def path(self, name):
        require(isinstance(name, str) and bool(name), "Invalid evidence name")
        p = Path(name)
        require(not p.is_absolute() and ".." not in p.parts, "Unsafe evidence path")
        path = self.root / p
        require(path.resolve().is_relative_to(self.root), "Evidence escapes root")
        return path

    def read(self, name):
        require(name in self.files, "Unmanifested evidence: " + name)
        return self.path(name).read_text()

    def json(self, name):
        return json.loads(self.read(name))


def receipt(evidence, name, success=True):
    r = evidence.json(name)
    require(type(r["exit"]) is int and (r["exit"] == 0 if success else r["exit"] != 0),
            "Unexpected command exit: " + name)
    require(isinstance(r["command"], list) and bool(r["command"]), "Missing command")
    require(r["elapsed_seconds"] > 0 and r["max_rss_kib"] > 0, "Missing runtime/memory evidence")
    check_environment(r["environment"])
    log = evidence.read(r["log"])
    require(bool(log.strip()), "Missing command diagnostics")
    if success:
        require(not FINDING.search(log), "Sanitizer finding in " + r["log"])
    return r, log


def inventory(tests, source, build):
    result = {}
    for test in tests:
        name = test["name"]
        require(name not in result and bool(test.get("command")), "Duplicate/incomplete CTest registration")
        for prop in test.get("properties", []):
            key, value = prop["name"], prop["value"]
            require(key not in {"DISABLED", "WILL_FAIL", "SKIP_RETURN_CODE", "SKIP_REGULAR_EXPRESSION"},
                    "CTest result policy override")
            if key in {"ENVIRONMENT", "ENVIRONMENT_MODIFICATION"}:
                require(isinstance(value, list), "Malformed CTest environment")
                for entry in value:
                    k, sep, v = entry.partition("=")
                    require(bool(sep), "Malformed CTest environment entry")
                    require(k not in FORBIDDEN, "Forbidden CTest environment override")
                    if k in STRICT:
                        require(key == "ENVIRONMENT" and v == STRICT[k], "CTest weakens strict policy")
        # Normalize only the two explicit roots, preserving argv, scales and all properties.
        value = json.dumps({k: v for k, v in test.items() if k != "backtrace"}, sort_keys=True)
        result[name] = value.replace(str(build), "<BUILD>").replace(str(source), "<SOURCE>")
    require(bool(result), "Empty CTest inventory")
    return result


def check_ctest(evidence, gate, expected, full=False):
    r, log = receipt(evidence, gate["receipt"])
    command = r["command"]
    require(Path(command[0]).name == "ctest", "Not an actual CTest command")
    require("--parallel" in command and command[command.index("--parallel") + 1] == "1",
            "CTest must be sequential")
    require("--no-tests=error" in command, "Empty CTest selections must fail")
    require("--repeat" not in command, "Repeated results need separate complete receipts")
    selectors = {"-R", "-E", "-I", "-L", "-LE", "--tests-regex", "--exclude-regex",
                 "--tests-information", "--label-regex", "--label-exclude", "--tests-from-file",
                 "--exclude-from-file", "--rerun-failed", "--exclude-fixture", "--stop-on-failure"}
    require(not any(arg.split("=", 1)[0] in selectors - ({"-R"} if not full else set()) for arg in command),
            "Filtered/incomplete CTest command")
    if not full:
        require("-R" in command, "Missing exact selected-test command")
        pattern = command[command.index("-R") + 1]
        require(pattern == selection_pattern(expected),
                "CTest selection differs from risk manifest")
    junit = ET.fromstring(evidence.read(gate["junit"]))
    cases = list(junit.iter("testcase"))
    observed = [case.attrib["name"] for case in cases]
    require(len(observed) == len(set(observed)) and set(observed) == set(expected),
            "Incomplete/unexpected CTest results")
    for case in cases:
        require(case.attrib.get("status") == "run", "CTest did not run")
        require(not any(case.find(tag) is not None for tag in ("failure", "error", "skipped")),
                "Failed/skipped test: " + case.attrib["name"])
        require(not FINDING.search("".join(case.itertext())), "Sanitizer finding in test output")
    logged = re.findall(r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(.+?)\s+\.{2,}\s+Passed", log, re.M)
    require(len(logged) == len(expected) and set(logged) == set(expected), "Incomplete passing CTest log")
    require(f"100% tests passed, 0 tests failed out of {len(expected)}" in log, "Missing CTest summary")
    return set(observed)


def check_qt_paths(log, prefix, version, files, loader=False):
    if not loader:
        # QML import tracing also reports unsuccessful qmldir searches for
        # C++ registered modules. Only ldd's unresolved DSO result belongs here.
        require(not re.search(r"=>\s+not found\b", log), "Unresolved runtime dependency")
    paths = (re.findall(r"calling init:\s+(\S*libQt6\S+)", log) if loader else
             re.findall(r"libQt6\S+\s+=>\s+(\S+)", log))
    require(bool(paths), "Missing actual Qt loader/linkage evidence")
    for path in paths:
        require(path in files, "Unidentified loaded Qt library")
        canonical = Path(files[path]["resolved"])
        spelling = Path(posixpath.normpath(path))
        require(spelling.is_absolute() and spelling.is_relative_to(Path(prefix) / "lib") and
                canonical.is_relative_to(Path(prefix) / "lib") and ".." not in canonical.parts and
                canonical.name.endswith(".so." + version), "Qt runtime version/prefix mismatch")
        require(re.fullmatch(r"[0-9a-f]{64}", files[path]["sha256"]), "Missing Qt binary identity")
    if loader:
        for module in ("Core", "Gui", "Quick", "Qml"):
            require(any(Path(path).name.startswith("libQt6" + module + ".so") for path in paths),
                    "Missing actual runtime Qt module: " + module)
        plugins = re.findall(r'"([^"\n]+)" loaded library', log)
        require(any(Path(p).name == "libqoffscreen.so" for p in plugins), "Offscreen QPA not loaded")
        metadata = {}
        for path, data in re.findall(r"Found metadata in lib ([^\n]+), metadata=\n(\{.*?\n\})", log, re.S):
            metadata[path] = json.loads(data)["version"]
        major, minor, _ = map(int, version.split("."))
        for path in plugins:
            spelling = Path(posixpath.normpath(path))
            require(path in files and spelling.is_absolute() and spelling.is_relative_to(prefix) and
                    Path(files[path]["resolved"]).is_relative_to(prefix) and
                    ".." not in Path(files[path]["resolved"]).parts,
                    "Loaded Qt plugin outside prefix")
            require(re.fullmatch(r"[0-9a-f]{64}", files[path]["sha256"]), "Missing loaded plugin identity")
            require(metadata.get(path, 0) >> 8 == (major << 8 | minor), "Loaded Qt plugin version mismatch")


def check_instrumentation(evidence, config, source, qt):
    cache = evidence.read(config["cache"])
    for key, value in {"CMAKE_BUILD_TYPE": "Debug", "MANTIS_SANITIZE": "ON",
                       "MANTIS_BUILD_STUDIO": "ON" if config["studio"] else "OFF",
                       "Python3_EXECUTABLE": "/usr/bin/python3"}.items():
        require(re.search(rf"^{key}:[^=]+={re.escape(value)}$", cache, re.M), "Wrong CMake configuration: " + key)
    commands = evidence.json(config["compile_commands"])
    authored = []
    for entry in commands:
        path = Path(entry["file"])
        if path.is_relative_to(source):
            relative = str(path.relative_to(source))
            if relative.startswith(("apps/", "src/", "plugins/", "tests/unit/studio_", "tests/integration/studio_")):
                flags = shlex.split(entry["command"])
                require("-fsanitize=address,undefined" in flags and "-g" in flags and
                        "-fno-omit-frame-pointer" in flags, "Uninstrumented authored source: " + relative)
                require(not any(f.startswith(("-fno-sanitize", "-fsanitize-ignorelist", "-fsanitize-blacklist"))
                                or f == "-g0" for f in flags), "Disabled instrumentation: " + relative)
                authored.append(relative)
    require(bool(authored), "Missing authored compile evidence")
    if config["studio"]:
        required = {"apps/studio/" + name + ".cpp" for name in (
            "bridge", "calibration_controller", "live_views", "scan_model", "devices_model", "projects_model", "home_model")}
        required |= {"src/render/render.cpp", "tests/unit/studio_calibration.cpp", "tests/unit/studio_scan.cpp"}
        require(required <= set(authored), "Missing required Studio/render/test compilation")
        for module in ("Core", "Gui", "Quick", "Qml", "QuickControls2", "Concurrent", "Test"):
            require(f"Qt6{module}_DIR:PATH={qt['prefix']}/lib/cmake/Qt6{module}" in cache,
                    "Qt configure module outside prefix")
    links = evidence.read(config["link_commands"])
    names = {b["name"] for b in config["binaries"]}
    require((GUI_BINARIES if config["studio"] else {"mantisd", "mantis-cli"}) <= names,
            "Missing authored executable instrumentation evidence")
    require(bool(config["libraries"]), "Missing library instrumentation evidence")
    manifests = evidence.json(config["plugin_manifests"])
    require(bool(manifests), "Missing configured plugin manifests")
    for entry in manifests.values():
        require(entry["content"]["library"] in names and re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]),
                "Configured plugin binary missing from instrumentation evidence")
    for binary in config["binaries"] + config["libraries"]:
        require("__asan_init" in evidence.read(binary["symbols"]) and
                "__ubsan_handle" in evidence.read(binary["symbols"]), "Missing ASan/UBSan symbols")
        require(".debug_info" in evidence.read(binary["sections"]), "Missing debug information")
        require(re.fullmatch(r"[0-9a-f]{64}", binary["sha256"]), "Missing binary identity")
        if "linkage" not in binary:  # Static libraries have no runtime loader or sanitizer link step.
            continue
        lines = [line for line in links.splitlines() if "-o " + binary["output"] + " " in line]
        require(len(lines) == 1 and "-fsanitize=address,undefined" in shlex.split(lines[0]),
                "Missing actual sanitizer link flags: " + binary["name"])
        linkage = evidence.read(binary["linkage"])
        require("libasan.so" in linkage and "libubsan.so" in linkage and "not found" not in linkage,
                "Missing sanitizer runtime linkage")
        if "libQt6" in linkage:
            require(config["studio"], "Qt leaked into headless build")
            check_qt_paths(linkage, qt["prefix"], qt["version"], config["runtime_files"])


def verify(root, anchor, expected_source_sha, expected_source_manifest):
    e = Evidence(root, anchor)
    q = e.json("qualification.json")
    require(q["schema"] == 1 and q["source_sha"] == expected_source_sha, "Source SHA/schema mismatch")
    require(digest(e.path("sources.json")) == expected_source_manifest, "Source content identity mismatch")
    require(e.read("sources.json") == e.read("restored-sources.json"), "Source changed during qualification")
    source, qt = Path(q["source_root"]), q["qt"]
    require(qt["version"] == e.read(qt["version_receipt"]).strip(), "Qt version query mismatch")
    require(q["compiler"] and q["linker"] and q["opencv"], "Missing toolchain provenance")
    sources = e.json("sources.json")
    for name, expected in q["tools"].items():
        require(sources[name]["sha256"] == expected, "Qualification tool identity mismatch")
    require({"tools/verify_studio_sanitizers.py", "tools/qualify_studio_sanitizers.py"} <= set(q["tools"]),
            "Missing qualification tool identity")
    require(len(q["qt_controls"]) == 3 and len(set(q["qt_controls"])) == 3, "Missing standalone Qt controls")
    for control in q["qt_controls"]:
        _, log = receipt(e, control)
        require("QT_CONTROL_FRAME runtime Qt " + qt["version"] in log, "Missing standalone frame/version")
        check_qt_paths(log, qt["prefix"], qt["version"], q["qt_control_files"], loader=True)
    required_modes = {"debug": (True, False), "headless": (False, False),
                      "sanitizers": (False, True), "studio-sanitizers": (True, True)}
    require(set(q["configurations"]) == set(required_modes), "Missing required configurations")
    inventories = {}
    for name, config in q["configurations"].items():
        studio, sanitized = required_modes[name]
        require(config["studio"] is studio and config["sanitized"] is sanitized,
                "Required configuration mode mismatch: " + name)
        cache = e.read(config["cache"])
        for key, value in {"CMAKE_HOME_DIRECTORY": str(source), "CMAKE_BUILD_TYPE": "Debug",
                           "MANTIS_BUILD_STUDIO": "ON" if config["studio"] else "OFF",
                           "MANTIS_SANITIZE": "ON" if config["sanitized"] else "OFF",
                           "Python3_EXECUTABLE": "/usr/bin/python3"}.items():
            require(re.search(rf"^{key}:[^=]+={re.escape(value)}$", cache, re.M),
                    "Wrong configured source/mode: " + key)
        inventories[name] = inventory(e.json(config["inventory"])["tests"], source, config["build"])
        require(inventories[name] == inventory(e.json(config["initial_inventory"])["tests"], source, config["build"]),
                "CTest registration/timeout/property change")
        if config["sanitized"]:
            check_instrumentation(e, config, source, qt)
        receipt(e, config["configure"])
        receipt(e, config["build_receipt"])
    require(set(inventories) == {"debug", "headless", "sanitizers", "studio-sanitizers"}, "Missing build gate")
    require(inventories["headless"] == inventories["sanitizers"], "Headless sanitizer registration mismatch")
    require(inventories["debug"] == inventories["studio-sanitizers"], "Studio sanitizer registration mismatch")
    studio = set(inventories["debug"]) - set(inventories["headless"])
    require(studio == {n for n in inventories["debug"] if n.startswith("studio-")}, "Unexpected Studio inventory delta")
    require(FOCUSED <= studio, "Missing focused risk coverage")
    gates = q["gates"]
    require(set(gates) == {"focused", "broader", "normal-studio", "normal-headless", "headless-sanitizers", "ui-repeat"},
            "Incomplete qualification gates")
    check_ctest(e, gates["focused"], FOCUSED)
    check_ctest(e, gates["broader"], studio - FOCUSED)
    for gate, config in (("normal-studio", "debug"), ("normal-headless", "headless"), ("headless-sanitizers", "sanitizers")):
        check_ctest(e, gates[gate], inventories[config], full=True)
    require(len(gates["ui-repeat"]) == 3, "Missing UI10x3 repeats")
    require(len({r["receipt"] for r in gates["ui-repeat"]}) == 3, "Reused repeat receipt")
    for gate in gates["ui-repeat"]:
        check_ctest(e, gate, REPEATED)
    workload, log = receipt(e, q["workload"])
    require(Path(workload["command"][0]).name == "mantis-studio", "Loader proof is not Studio")
    studio_binary = next(b for b in q["configurations"]["studio-sanitizers"]["binaries"] if b["name"] == "mantis-studio")
    require(workload["executable_sha256"] == studio_binary["sha256"], "Rendered Studio binary identity mismatch")
    check_qt_paths(log, qt["prefix"], qt["version"], q["workload_files"], loader=True)
    require("Loading backend software" in log and "Frame rendered with 'software' renderloop" in log,
            "Missing real Studio software frame")
    require(qt["prefix"] + "/plugins/platforms/libqoffscreen.so" in log and qt["prefix"] + "/qml/" in log,
            "Missing real Studio plugin/QML loader proof")
    require(q["screenshot"] in e.files, "Unmanifested Studio screenshot")
    require(e.path(q["screenshot"]).read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), "Missing rendered Studio PNG")
    controls = q["negative_controls"]
    require(set(controls) == {"NC1", "NC2"}, "Missing executed detector controls")
    _, leak = receipt(e, controls["NC1"], success=False)
    require("ERROR: LeakSanitizer: detected memory leaks" in leak and "leakForDetectorControl" in leak,
            "Missing primary intentional leak diagnostic")
    nc2, invalid = receipt(e, controls["NC2"], success=False)
    require(Path(nc2["command"][0]).name == "mantis-studio-scan-tests", "Wrong NC2 executable")
    check_application_fault(invalid)
    require(q["fault_restoration"]["before"] == q["fault_restoration"]["after"] ==
            sources["tests/unit/studio_scan.cpp"]["sha256"], "Fault not restored")
    fault = q["fault_restoration"]
    require(re.fullmatch(r"[0-9a-f]{64}", fault["mutant_source_sha256"]) and
            fault["mutant_source_sha256"] != fault["before"], "Missing mutated control source identity")
    require(nc2["executable_sha256"] == fault["mutant_binary_sha256"], "Wrong NC2 binary identity")
    restored_binary = next(b for b in q["configurations"]["studio-sanitizers"]["binaries"]
                           if b["name"] == "mantis-studio-scan-tests")
    require(restored_binary["sha256"] != fault["mutant_binary_sha256"], "Faulted binary was not replaced")
    receipt(e, q["fault_restoration"]["build"])
    receipt(e, q["regressions"])
    _, failure = receipt(e, q["test_failure_control"], success=False)
    require("Invalid --ui-mode" in failure and "0% tests passed, 1 tests failed" in failure,
            "Missing primary failing CTest diagnostic")
    require(set(q.get("evidence_controls", {})) == {"NC3", "NC4", "NC5", "NC6"},
            "Missing completed evidence controls")
    for name, primary in {"NC3": "Source SHA", "NC4": "Qt runtime version/prefix",
                          "NC5": "Uninstrumented authored source", "NC6": "Incomplete/unexpected CTest results"}.items():
        _, text = receipt(e, q["evidence_controls"][name], success=False)
        require("Invalid qualification evidence: " + primary in text, "Wrong evidence-control diagnostic: " + name)
    return {"result": "QUALIFIED", "source_sha": expected_source_sha,
            "source_manifest_sha256": expected_source_manifest, "studio_tests": len(studio),
            "limits": "Linux offscreen/software; prebuilt Qt/dependencies not instrumented; independent review required"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("evidence", type=Path)
    p.add_argument("--expected-manifest-sha256", required=True)
    p.add_argument("--expected-source-sha", required=True)
    p.add_argument("--expected-source-manifest-sha256", required=True)
    args = p.parse_args()
    try:
        result = verify(args.evidence, args.expected_manifest_sha256, args.expected_source_sha,
                        args.expected_source_manifest_sha256)
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError, ET.ParseError) as error:
        print("Invalid qualification evidence: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Verify complete Studio qualification receipts without trusting PASS labels.

The caller supplies independently retained hashes for the sealed candidate source
manifest and the evidence manifest. Source snapshots and build trees stay private.
This verifier executes no build, test or archived code.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

from qualify_qt_sanitizer import InvalidEvidence, SANITIZER_ERROR, check_linkage, require

BASELINE = "d2614b96304ff5fd45db8f09076da058afb9230d"
BASELINE_SOURCE_MANIFEST = "1c9bf658d61f0301d921f7769f7074a73fcd4d51e427194e2d798af75df9c353"
ENVIRONMENT = {
    "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
    "QT_QPA_PLATFORM": "offscreen",
    "QT_QUICK_BACKEND": "software",
}
FORBIDDEN = ("LSAN_OPTIONS", "LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH",
             "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QSG_RENDER_LOOP")
FOCUSED = {"studio-scan-qml", "studio-scan-capture-wire", "studio-ui-m0-qml",
           "studio-scan-surfaces", "studio-bridge-errors"}
ROUTES = {"studio-home-qml", "studio-projects-qml", "studio-projects-resize",
          "studio-projects-scroll", "studio-devices-qml", "studio-devices-stress",
          "studio-scan-qml", "studio-scan-stress", "studio-scan-surfaces", "studio-ui-m0-qml"}
GATES = {"baseline-focused", "baseline-full", "candidate-focused", "candidate-full",
         "baseline-repeat", "candidate-repeat", "normal-studio", "normal-headless",
         "headless-sanitizers", "tool-regressions", "architecture-contracts"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Evidence:
    def __init__(self, root, expected_manifest):
        self.root = root.resolve()
        manifest = self.root / "manifest.json"
        require(digest(manifest) == expected_manifest, "Evidence manifest trust anchor mismatch")
        self.files = json.loads(manifest.read_text())["files"]
        require(bool(self.files), "Empty evidence manifest")
        for name, expected in self.files.items():
            path = self.path(name)
            require(path.is_file() and not path.is_symlink(), f"Missing/unsafe evidence: {name}")
            require(digest(path) == expected, f"Evidence checksum mismatch: {name}")

    def path(self, name):
        require(isinstance(name, str) and bool(name), "Invalid evidence path")
        relative = Path(name)
        require(not relative.is_absolute() and ".." not in relative.parts, "Unsafe evidence path")
        path = self.root / relative
        require(path.resolve().is_relative_to(self.root), "Evidence escapes archive")
        return path

    def read(self, name):
        require(name in self.files, f"Unmanifested evidence: {name}")
        return self.path(name).read_text()

    def json(self, name):
        return json.loads(self.read(name))


def check_environment(env):
    for key, value in ENVIRONMENT.items():
        require(env.get(key) == value, f"Invalid strict environment: {key}")
    for key in FORBIDDEN:
        require(not env.get(key), f"Forbidden runtime override: {key}")


def check_configuration(evidence, config, sanitized, studio):
    cache = evidence.read(config["cache"])
    required = {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_COMPILER": "/usr/bin/g++",
        "MANTIS_BUILD_STUDIO": "ON" if studio else "OFF",
        "MANTIS_SANITIZE": "ON" if sanitized else "OFF",
        "Python3_EXECUTABLE": "/usr/bin/python3",
    }
    for key, value in required.items():
        require(re.search(rf"^{key}:[^=]+={re.escape(value)}$", cache, re.M), f"Wrong configuration: {key}")
    require(evidence.read(config["compiler_version"]).strip() == "13.3.0", "Wrong actual compiler")
    if not sanitized:
        return
    commands = evidence.json(config["compile_commands"])
    require(bool(commands), "Missing compilation evidence")
    instrumented = [entry for entry in commands if "-fsanitize=address,undefined" in entry.get("command", "")]
    require(bool(instrumented), "Missing actual compiler instrumentation")
    for executable in config["executables"]:
        symbols = evidence.read(executable["symbols"])
        require("__asan_init" in symbols and "__ubsan_handle" in symbols, "Missing instrumentation symbols")
        require(".debug_info" in evidence.read(executable["sections"]), "Missing executable debug information")
        link = evidence.read(executable["link_command"])
        require("-fsanitize=address,undefined" in link, "Missing actual linker instrumentation")
        dependencies = evidence.read(executable["linkage"])
        if studio:
            check_linkage(dependencies, Path(config["qt_prefix"]), "6.9.2")
        else:
            require("libasan.so" in dependencies and "libubsan.so" in dependencies,
                    "Missing sanitizer runtime linkage")
            require("libQt6" not in dependencies, "Headless executable links Qt")
    require(bool(config["executables"]), "Missing instrumented executable evidence")
    if studio:
        require(config["qt_version"] == "6.9.2", "Wrong Qt version")
        prefix = config["qt_prefix"]
        for module in ("Core", "Gui", "Qml", "Quick", "QuickControls2", "Concurrent", "Test"):
            require(re.search(rf"^Qt6{module}_DIR:PATH={re.escape(prefix)}/lib/cmake/Qt6{module}$", cache, re.M),
                    f"Qt module outside prefix: {module}")
        compiled = "\n".join(entry.get("command", "") for entry in instrumented)
        require("apps/studio/" in compiled and "tests/unit/studio_scan.cpp" in compiled,
                "Missing actual Studio/test instrumentation")


def check_workload(evidence, workload, prefix):
    receipt = evidence.json(workload["receipt"])
    require(receipt["exit"] == 0, "Rendered workload failed")
    check_environment(receipt["environment"])
    command = receipt["command"]
    require(bool(command), "Missing rendered workload command")
    executable = 0
    if Path(command[0]).name == "env":
        executable = 1
        while executable < len(command) and "=" in command[executable]:
            key, _ = command[executable].split("=", 1)
            require(key in ("LD_DEBUG", "QT_MESSAGE_PATTERN", "QT_LOGGING_RULES"),
                    "Workload wrapper overrides execution policy")
            executable += 1
    require(executable < len(command) and Path(command[executable]).name == "mantis-studio",
            "Rendered workload is not the Studio executable")
    text = evidence.read(workload["log"])
    require(not SANITIZER_ERROR.search(text), "Sanitizer finding in rendered workload")
    require("Loading backend software" in text and "Frame rendered with 'software' renderloop" in text,
            "Missing rendered software Qt Quick workload")
    libraries = re.findall(r"calling init:\s+(\S*libQt6\S+)", text)
    require(bool(libraries), "Missing actual Qt loader evidence")
    for path in libraries:
        require(Path(path).resolve().is_relative_to(Path(prefix) / "lib") and Path(path).resolve().name.endswith(".so.6.9.2"),
                "Runtime Qt library outside qualified version/prefix")
    require(prefix + "/plugins/platforms/libqoffscreen.so" in text, "Missing offscreen plugin evidence")
    require(prefix + "/qml/" in text, "Missing actual QML import evidence")
    image = evidence.path(workload["screenshot"])
    require(workload["screenshot"] in evidence.files, "Unmanifested rendered image")
    require(image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), "Missing rendered PNG")


def check_ctest(evidence, gate, required):
    receipt = evidence.json(gate["receipt"])
    require(receipt["exit"] == 0, "Nonzero test exit")
    command = receipt["command"]
    require(command and Path(command[0]).name == "ctest", "Missing actual CTest command")
    require("--parallel" in command and command[command.index("--parallel") + 1] == "1",
            "CTest must execute sequentially")
    if gate.get("full"):
        forbidden = ("-R", "-E", "-I", "-L", "-LE", "--tests-regex", "--exclude-regex",
                     "--tests-information", "--label-regex", "--label-exclude", "--tests-from-file",
                     "--exclude-from-file", "--rerun-failed", "--exclude-fixture", "--stop-on-failure")
        require(not any(part == flag or part.startswith(flag + "=")
                        for part in command for flag in forbidden), "Filtered mandatory full suite")
    check_environment(receipt["environment"])
    log = evidence.read(gate["log"])
    require(bool(log.strip()), "Missing test output")
    if gate.get("full"):
        require("100% tests passed, 0 tests failed" in log, "Missing successful full CTest summary")
    require(not SANITIZER_ERROR.search(log), "Sanitizer finding in required test output")
    inventory = evidence.json(gate["inventory"])["tests"]
    names = [test["name"] for test in inventory]
    require(len(names) == len(set(names)) and bool(names), "Invalid CTest inventory")
    for test in inventory:
        for prop in test.get("properties", []):
            require(not (prop["name"] == "DISABLED" and prop["value"]), "Disabled mandatory test")
            if prop["name"] in ("ENVIRONMENT", "ENVIRONMENT_MODIFICATION"):
                entries = prop["value"] if isinstance(prop["value"], list) else prop["value"].split(";")
                for entry in entries:
                    key, separator, value = entry.partition("=")
                    require(bool(separator), "Malformed CTest environment property")
                    if key in ENVIRONMENT:
                        require(prop["name"] == "ENVIRONMENT" and value == ENVIRONMENT[key],
                                "CTest property overrides strict runtime policy")
                    if key in FORBIDDEN:
                        require(not value, "CTest property supplies forbidden runtime override")
    require(required <= set(names), "Mandatory tests missing from inventory")
    results = ET.fromstring(evidence.read(gate["junit"]))
    cases = list(results.iter("testcase"))
    observed = [test.attrib["name"] for test in cases]
    require(len(observed) == len(set(observed)), "Duplicate test results")
    require(required <= set(observed), "Missing mandatory test results")
    if gate.get("full"):
        require(set(observed) == set(names), "Full CTest results differ from registered inventory")
        summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", log)
        require(summary and int(summary[1]) == len(names), "Full CTest summary count differs from inventory")
        logged = re.findall(r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(.+?)\s+\.{2,}\s+Passed", log, re.M)
        require(len(logged) == len(names) and set(logged) == set(names),
                "Full CTest log does not contain every passing test")
    for test in cases:
        require(not any(test.find(tag) is not None for tag in ("failure", "error", "skipped")),
                f"Failed/skipped test: {test.attrib['name']}")
        require(test.attrib.get("status", "run") == "run", "Test did not run")
        require(not SANITIZER_ERROR.search("".join(test.itertext())), "Sanitizer finding in test result")
    return set(names)


def verify(root, expected_manifest, expected_candidate):
    evidence = Evidence(root, expected_manifest)
    qualification = evidence.json("qualification.json")
    require(qualification["baseline_revision"] == BASELINE, "Wrong unchanged baseline revision")
    require(digest(evidence.path(qualification["candidate_source_manifest"])) == expected_candidate,
            "Candidate snapshot trust anchor mismatch")
    before = evidence.json(qualification["protected_before"])
    require(before == evidence.json(qualification["protected_after"]), "Protected source or index changed")
    require(before["head"] == BASELINE and before["branch"] == "ao/ui-m3d1-recovery/root",
            "Wrong protected candidate identity")
    candidate_files = evidence.json(qualification["candidate_source_manifest"])
    require(candidate_files == {name: entry["sha256"] for name, entry in before["files"].items()},
            "Candidate snapshot differs from protected source hashes")
    require(digest(evidence.path(qualification["baseline_source_manifest"])) ==
            BASELINE_SOURCE_MANIFEST,
            "Baseline source manifest differs from unchanged CUT-0C")
    require(set(qualification["gates"]) == GATES, "Incomplete mandatory qualification gates")
    configs = qualification["configurations"]
    for name, sanitized, studio in (("baseline", True, True), ("candidate", True, True),
                                   ("normal-studio", False, True), ("normal-headless", False, False),
                                   ("headless-sanitizers", True, False)):
        check_configuration(evidence, configs[name], sanitized, studio)
    require(configs["baseline"]["qt_prefix"] == configs["candidate"]["qt_prefix"], "Mismatched Qt environments")
    gates = qualification["gates"]
    for phase in ("focused", "full"):
        baseline_env = dict(evidence.json(gates["baseline-" + phase]["receipt"])["environment"])
        candidate_env = dict(evidence.json(gates["candidate-" + phase]["receipt"])["environment"])
        for key in ("XDG_RUNTIME_DIR", "GIT_DIR", "GIT_WORK_TREE", "GIT_OPTIONAL_LOCKS"):
            baseline_env.pop(key, None)
            candidate_env.pop(key, None)
        require(baseline_env == candidate_env, "Mismatched baseline/candidate runtime environments")
    for name in ("baseline", "candidate"):
        git = evidence.json(qualification["git_fixtures"][name])
        require(git["head"] == BASELINE, "Qualification Git metadata has wrong source HEAD")
        cache = evidence.read(configs[name]["cache"])
        require(re.search(rf"^CMAKE_HOME_DIRECTORY:INTERNAL={re.escape(git['source'])}$", cache, re.M),
                "Git working tree differs from actual CMake source")
        full_env = evidence.json(gates[name + "-full"]["receipt"])["environment"]
        require(full_env.get("GIT_DIR") == git["git_dir"] and
                full_env.get("GIT_WORK_TREE") == git["source"] and full_env.get("GIT_OPTIONAL_LOCKS") == "0",
                "Full suite lacks truthful isolated Git provenance fixture")
        focused = FOCUSED | ({"studio-bridge-replay"} if name == "candidate" else set())
        check_workload(evidence, qualification["workloads"][name], configs[name]["qt_prefix"])
        check_ctest(evidence, gates[name + "-focused"], focused)
        check_ctest(evidence, gates[name + "-full"], focused)
        require(gates[name + "-full"].get("full") is True, "Mandatory full gate not declared full")
        repeats = gates[name + "-repeat"]
        require(len(repeats) == 3, "Expected exactly three initial lifetime repetitions")
        require(len({repeat["receipt"] for repeat in repeats}) == 3, "Repeated receipt reused as separate execution")
        for repeat in repeats:
            check_ctest(evidence, repeat, ROUTES | focused)
    for name in ("normal-studio", "normal-headless", "headless-sanitizers"):
        require(gates[name].get("full") is True, "Missing mandatory full regression gate")
        check_ctest(evidence, gates[name], FOCUSED if name == "normal-studio" else {"boundaries"})
    for name in ("tool-regressions", "architecture-contracts"):
        receipt = evidence.json(gates[name]["receipt"])
        require(receipt["exit"] == 0, f"Failed {name}")
        require(bool(evidence.read(gates[name]["log"]).strip()), f"Missing {name} output")
    return {"result": "QUALIFIED", "baseline": BASELINE, "candidate_manifest_sha256": expected_candidate}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--expected-manifest-sha256", required=True)
    parser.add_argument("--expected-candidate-sha256", required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.evidence, args.expected_manifest_sha256, args.expected_candidate_sha256), indent=2))
    except (InvalidEvidence, OSError, ValueError, KeyError, TypeError, ET.ParseError) as error:
        print(f"Invalid qualification evidence: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

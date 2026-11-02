#!/usr/bin/env python3
"""Opt-in local Studio qualification. Raw evidence is private; CI stays unchanged.

Run with /usr/bin/python3 -B. No package installation, suppression, CTest parallelism,
timeout extension, inherited loader override or production mutation is performed.
NC2 temporarily faults one existing test, restores its bytes in finally, rebuilds,
then runs every positive gate on the restored sources. Run only in a clean assigned
worktree, with no other build/test process using these caches.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

import verify_studio_sanitizers as policy

SOURCE = Path(__file__).resolve().parents[1]


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(policy.encoded(data))


def source_manifest():
    names = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                                    cwd=SOURCE).decode().split("\0")
    result = {}
    for name in sorted(set(names) - {""}):
        path = SOURCE / name
        policy.require(path.is_file() and not path.is_symlink(), "Unsupported source entry: " + name)
        result[name] = {"sha256": policy.digest(path), "executable": bool(path.stat().st_mode & 0o111)}
    return result


class Run:
    def __init__(self, output, resume=False):
        self.output = output
        self.environment = dict(policy.STRICT, PATH="/usr/bin:/bin", HOME=os.environ["HOME"],
                                LANG="C.UTF-8", LC_ALL="C.UTF-8", PYTHONDONTWRITEBYTECODE="1",
                                XDG_RUNTIME_DIR=str(output / "runtime"))
        (output / "runtime").mkdir(mode=0o700, exist_ok=resume)

    def command(self, name, argv, *, env=None, success=True, cwd=SOURCE):
        folder = self.output / "commands" / name
        folder.mkdir(parents=True)
        actual_env = dict(self.environment, **(env or {}))
        executable = Path(str(argv[0]))
        executable_hash = policy.digest(executable) if executable.is_file() else None
        log, metrics = folder / "output.log", folder / "time.txt"
        start = time.monotonic()
        with log.open("wb") as stream:
            result = subprocess.run(["/usr/bin/time", "-v", "-o", str(metrics)] + list(map(str, argv)),
                                    cwd=cwd, env=actual_env, stdout=stream, stderr=subprocess.STDOUT)
        timing = metrics.read_text()
        match = re.search(r"Maximum resident set size \(kbytes\): (\d+)", timing)
        record = {"command": list(map(str, argv)), "exit": result.returncode,
                  "environment": actual_env, "elapsed_seconds": time.monotonic() - start,
                  "max_rss_kib": int(match[1]) if match else 0,
                  "executable_sha256": executable_hash,
                  "log": str(log.relative_to(self.output))}
        receipt = folder / "receipt.json"
        write(receipt, record)
        print(f"{name}: exit={result.returncode} seconds={record['elapsed_seconds']:.2f} "
              f"max_rss_kib={record['max_rss_kib']}", flush=True)
        if success:
            policy.require(result.returncode == 0, f"{name} failed; primary diagnostics: {record['log']}")
            policy.require(not policy.FINDING.search(log.read_text(errors="replace")),
                           f"{name}: sanitizer diagnostic in {record['log']}")
        else:
            policy.require(result.returncode != 0, name + " negative control did not fail")
        return str(receipt.relative_to(self.output)), log.read_text(errors="replace")

    def save(self, name, data):
        write(self.output / name, data)
        return name

    def text(self, name, value):
        path = self.output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)
        return name

    def capture(self, name, command):
        receipt, text = self.command(name, command)
        return receipt, text

    def ctest(self, name, build, names=None):
        junit = self.output / "results" / (name + ".xml")
        junit.parent.mkdir(exist_ok=True)
        argv = ["ctest", "--test-dir", build, "--parallel", "1", "--no-tests=error", "--output-on-failure", "--output-junit", junit]
        if names is not None:
            policy.require(bool(names), "Empty selection")
            argv += ["-R", policy.selection_pattern(names)]
        receipt, _ = self.command(name, argv)
        gate = {"receipt": receipt, "junit": str(junit.relative_to(self.output))}
        expected = names if names is not None else {
            t["name"] for t in json.loads((self.output / Path(build).name / "initial-inventory.json").read_text())["tests"]}
        policy.check_ctest(policy.Evidence(self.output, seal(self.output)), gate, expected, full=names is None)
        return gate


def identities(paths):
    result = {}
    for name in sorted(set(map(str, paths))):
        path = Path(name)
        policy.require(path.is_file(), "Missing loaded runtime file")
        result[name] = {"resolved": str(path.resolve()), "sha256": policy.digest(path)}
    return result


def instrumentation(run, name, build, studio):
    result = {"compile_commands": run.text(name + "/compile_commands.json", (build / "compile_commands.json").read_text())}
    _, commands = run.command(name + "-link-commands", ["ninja", "-C", build, "-t", "commands"])
    result["link_commands"] = run.text(name + "/link_commands.txt", commands)
    binaries = [p for p in (build / "bin").iterdir()
                if p.name.startswith("mantis-studio") or p.name in {"mantisd", "mantis-cli", "mantis-plugin-host", "mantis-worker"}]
    binaries += sorted((build / "plugins").glob("*.so"))
    libraries = ([build / ("lib" + name + ".a") for name in (
        "mantis-render", "mantis-studio-calibration", "mantis-studio-home", "mantis-studio-projects",
        "mantis-studio-devices", "mantis-studio-scan")] if studio else [build / "libmantis-client.a"])
    result.update(binaries=[], libraries=[])
    runtime_paths = set()
    for binary in sorted(binaries + libraries):
        tag = name + "-" + binary.name
        record = {"name": binary.name, "sha256": policy.digest(binary), "output": str(binary.relative_to(build))}
        for key, command in (("symbols", ["nm", "-u", binary]), ("sections", ["readelf", "--sections", binary])):
            _, text = run.command(tag + "-" + key, command)
            record[key] = run.text(name + "/" + binary.name + "-" + key + ".txt", text)
        if binary in binaries:
            _, text = run.command(tag + "-linkage", ["ldd", binary])
            record["linkage"] = run.text(name + "/" + binary.name + "-linkage.txt", text)
            runtime_paths.update(re.findall(r"=>\s+(/\S+)", text))
            result["binaries"].append(record)
        else:
            result["libraries"].append(record)
    result["runtime_files"] = identities(runtime_paths)
    result["plugin_manifests"] = run.save(name + "/plugin-manifests.json", {
        p.name: {"sha256": policy.digest(p), "content": json.loads(p.read_text())}
        for p in sorted((build / "plugins").glob("*.json"))})
    compiler = re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.+)$", (build / "CMakeCache.txt").read_text(), re.M)
    policy.require(compiler, "Missing actual C++ compiler")
    result["compiler_file"] = identities([compiler[1]])
    cmake_compilers = list((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    policy.require(len(cmake_compilers) == 1, "Ambiguous configured compiler metadata")
    result["compiler_metadata"] = run.text(name + "/compiler.cmake", cmake_compilers[0].read_text())
    return result


def seal(output):
    files = {str(p.relative_to(output)): policy.digest(p) for p in sorted(output.rglob("*"))
             if p.is_file() and p.name != "manifest.json" and "runtime" not in p.relative_to(output).parts}
    write(output / "manifest.json", {"files": files})
    return policy.digest(output / "manifest.json")


def evidence_controls(run, q):
    """Mutate copies of real evidence, recomputing hashes to exercise semantics."""
    result = {}
    with tempfile.TemporaryDirectory(prefix="mantis-evidence-controls-") as temporary:
        for name in ("NC3", "NC4", "NC5", "NC6"):
            target = Path(temporary) / name
            shutil.copytree(run.output, target, ignore=shutil.ignore_patterns("runtime"))
            qualification = json.loads((target / "qualification.json").read_text())
            expected_sha = q["source_sha"]
            if name == "NC3":
                expected_sha = "f" * 40
            elif name == "NC4":
                paths = qualification["workload_files"]
                core = next(p for p in paths if Path(p).name.startswith("libQt6Core.so"))
                paths[core]["resolved"] = "/incompatible/libQt6Core.so.6.4.2"
                write(target / "qualification.json", qualification)
            elif name == "NC5":
                path = target / qualification["configurations"]["studio-sanitizers"]["compile_commands"]
                commands = json.loads(path.read_text())
                unit = next(c for c in commands if c["file"].endswith("/apps/studio/bridge.cpp"))
                unit["command"] = unit["command"].replace("-fsanitize=address,undefined", "")
                write(path, commands)
            else:
                path = target / qualification["gates"]["focused"]["junit"]
                tree = policy.ET.parse(path)
                parent = next(p for p in tree.iter() if p.find("testcase") is not None)
                parent.remove(parent.find("testcase"))
                tree.write(path)
            anchor = seal(target)
            record, _ = run.command(name + "-evidence-rejection", ["/usr/bin/python3", "-B",
                SOURCE / "tools/verify_studio_sanitizers.py", target, "--expected-manifest-sha256", anchor,
                "--expected-source-sha", expected_sha, "--expected-source-manifest-sha256",
                policy.digest(target / "sources.json")], success=False)
            result[name] = record
    return result


def prepare(args, run):
    actual_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip()
    policy.require(actual_sha == args.source_sha, "Source HEAD differs from explicit expected SHA")
    sources = source_manifest()
    run.save("sources.json", sources)
    q = {"schema": 1, "source_sha": actual_sha, "source_root": str(SOURCE),
         "head_tree": subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=SOURCE, text=True).strip(),
         "tools": {n: v["sha256"] for n, v in sources.items() if n.startswith(("tests/qualification/", "tools/qualify_studio_", "tools/verify_studio_"))},
         "configurations": {}, "gates": {}, "negative_controls": {}}
    _, q["compiler"] = run.command("compiler", ["g++", "--version"])
    _, q["linker"] = run.command("linker", ["ld", "--version"])
    q["toolchain_files"] = identities([shutil.which(name, path=run.environment["PATH"])
                                      for name in ("g++", "gcc", "ld", "cmake", "ninja", "ctest", "python3")])
    _, q["opencv"] = run.command("opencv", ["pkg-config", "--modversion", "opencv4"])
    _, version = run.command("qt-version", [args.qt_prefix / "bin/qtpaths", "--qt-version"])
    policy.require(version.strip() == args.qt_version, "Explicit Qt version mismatch")
    q["qt"] = {"prefix": str(args.qt_prefix), "version": args.qt_version,
               "version_receipt": run.text("qt-version.txt", version)}
    _, paths = run.command("qt-paths", [args.qt_prefix / "bin/qtpaths", "--query"])
    policy.require("QT_INSTALL_PREFIX:" + str(args.qt_prefix) in paths, "Qt installation origin mismatch")
    for name in ("config_qtbase.opt", "config_qtbase.summary", "config_qtdeclarative.opt", "config_qtdeclarative.summary"):
        run.text("qt-origin/" + name, (args.qt_prefix / name).read_text())
    control = SOURCE / "build/qt-sanitizer-controls"
    run.command("qt-controls-configure", ["cmake", "-S", SOURCE / "tests/qualification/qt-sanitizer", "-B", control,
                "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", "-DQUALIFICATION_QT_VERSION=" + args.qt_version,
                "-DCMAKE_PREFIX_PATH=" + str(args.qt_prefix), "-DQt6_DIR=" + str(args.qt_prefix / "lib/cmake/Qt6")])
    run.command("qt-controls-build", ["cmake", "--build", control, "--parallel", str(args.jobs)])
    for name in ("studio-sanitizers", "debug", "headless", "sanitizers"):
        build = SOURCE / "build" / name
        preset = {"debug": "linux-debug", "headless": "headless", "sanitizers": "sanitizers"}.get(name)
        argv = (["cmake", "--preset", preset] if preset else
                ["cmake", "-S", SOURCE, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
                 "-DMANTIS_BUILD_STUDIO=ON", "-DMANTIS_SANITIZE=ON", "-DCMAKE_PREFIX_PATH=" + str(args.qt_prefix),
                 "-DQt6_DIR=" + str(args.qt_prefix / "lib/cmake/Qt6")])
        conf, _ = run.command(name + "-configure", argv + ["-DPython3_EXECUTABLE=/usr/bin/python3", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"])
        _, registered = run.command(name + "-inventory-before", ["ctest", "--test-dir", build, "--show-only=json-v1"])
        run.text(name + "/prebuild-inventory.json", registered)
        q["configurations"][name] = {"build": str(build), "studio": name in {"debug", "studio-sanitizers"},
            "sanitized": name in {"sanitizers", "studio-sanitizers"}, "configure": conf,
            "initial_inventory": name + "/initial-inventory.json"}
        built, _ = run.command(name + "-build", ["cmake", "--build", build, "--parallel", str(args.jobs)])
        q["configurations"][name]["build_receipt"] = built
    build = SOURCE / "build/studio-sanitizers"
    # Mutate only this worktree's existing Studio test, never product sources or another worktree.
    target = SOURCE / "tests/unit/studio_scan.cpp"
    before = target.read_bytes()
    marker = b'        check(QDir().mkpath(out), "Evidence directory unavailable");'
    policy.require(before.count(marker) == 1, "NC2 insertion anchor is ambiguous")
    fault = (b'        auto *fault = new MeasurementView;\n        delete fault;\n'
             b'        fault->setImage(QImage(2, 2, QImage::Format_Grayscale8));\n')
    mutant_source = mutant_binary = None
    try:
        target.write_bytes(before.replace(marker, marker + b"\n" + fault))
        mutant_source = policy.digest(target)
        run.command("NC2-build", ["cmake", "--build", build, "--parallel", str(args.jobs), "--target", "mantis-studio-scan-tests"])
        mutant_binary = policy.digest(build / "bin/mantis-studio-scan-tests")
        q["negative_controls"]["NC2"], log = run.command("NC2-use-after-free", [build / "bin/mantis-studio-scan-tests",
                                                         run.output / "nc2-images", "surfaces"], success=False)
        policy.check_application_fault(log)
    finally:
        target.write_bytes(before)
        rebuilt, _ = run.command("NC2-restored-build", ["cmake", "--build", build, "--parallel", str(args.jobs),
                                                       "--target", "mantis-studio-scan-tests"])
        q["fault_restoration"] = {"before": hashlib.sha256(before).hexdigest(), "after": policy.digest(target),
                                  "mutant_source_sha256": mutant_source, "mutant_binary_sha256": mutant_binary,
                                  "build": rebuilt}
    for name, config in q["configurations"].items():
        cache = Path(config["build"])
        _, registered = run.command(name + "-inventory-built", ["ctest", "--test-dir", cache, "--show-only=json-v1"])
        run.text(config["initial_inventory"], registered)
        config["cache"] = run.text(name + "/cache.txt", (cache / "CMakeCache.txt").read_text())
        if config["sanitized"]:
            config.update(instrumentation(run, name, cache, config["studio"]))
    q["regressions"], _ = run.command("verifier-regressions", ["/usr/bin/python3", "-B", SOURCE / "tests/qualification/studio-sanitizers/test_verifier.py"])
    run.save("restored-sources.json", source_manifest())
    policy.require(sources == source_manifest(), "Preparation changed source inputs")
    evidence = policy.Evidence(run.output, seal(run.output))
    for config in q["configurations"].values():
        if config["sanitized"]:
            policy.check_instrumentation(evidence, config, SOURCE, q["qt"])
    run.save("prepared.json", q)
    print("PREPARED: builds and NC2 complete; strict runtime gates remain unexecuted", flush=True)


def execute(args, run):
    q = json.loads((run.output / "prepared.json").read_text())
    policy.require(q["source_sha"] == args.source_sha and q["qt"] == {
        "prefix": str(args.qt_prefix), "version": args.qt_version, "version_receipt": "qt-version.txt"},
        "Resume identity mismatch")
    policy.require(json.loads((run.output / "sources.json").read_text()) == source_manifest(),
                   "Source changed after preparation")
    actual_sha = args.source_sha
    trace_env = {"LD_DEBUG": "libs", "QT_DEBUG_PLUGINS": "1", "QML_IMPORT_TRACE": "1",
                 "QSG_INFO": "1", "QT_LOGGING_RULES": "qt.scenegraph.*=true;qt.quick.*=true"}
    control = SOURCE / "build/qt-sanitizer-controls"
    q["qt_controls"] = []
    q["qt_control_files"] = {}
    for i in range(3):
        r, text = run.command("qt-control-" + str(i + 1), [control / "qt-leak-probe"], env=trace_env)
        policy.require("QT_CONTROL_FRAME runtime Qt " + args.qt_version in text, "Standalone frame/version absent")
        q["qt_controls"].append(r)
        q["qt_control_files"].update(identities(re.findall(r"calling init:\s+(/\S+)", text)))
    r, log = run.command("NC1-leak", [control / "leak-detector-selftest"], success=False)
    policy.require("ERROR: LeakSanitizer: detected memory leaks" in log and "leakForDetectorControl" in log,
                   "NC1 missing primary LeakSanitizer diagnostic")
    q["negative_controls"]["NC1"] = r
    build = SOURCE / "build/studio-sanitizers"
    q["gates"]["focused"] = run.ctest("focused", build, policy.FOCUSED)
    on = json.loads((run.output / "debug/initial-inventory.json").read_text())["tests"]
    broader = {t["name"] for t in on if t["name"].startswith("studio-")} - policy.FOCUSED
    q["gates"]["broader"] = run.ctest("broader", build, broader)
    for gate, config in (("normal-studio", "debug"), ("normal-headless", "headless"), ("headless-sanitizers", "sanitizers")):
        q["gates"][gate] = run.ctest(gate, SOURCE / "build" / config)
    q["gates"]["ui-repeat"] = [run.ctest("ui-repeat-" + str(i + 1), SOURCE / "build/debug", policy.REPEATED) for i in range(3)]
    screenshot = run.output / "studio-software.png"
    q["workload"], log = run.command("studio-loader", [build / "bin/mantis-studio", "--ui-mode", "mock",
                                      "--workspace", "scan", "--screenshot", screenshot, "--quit-after", "1000"], env=trace_env)
    loaded = re.findall(r"calling init:\s+(/\S+)", log)
    q["workload_files"] = identities(loaded)
    q["screenshot"] = str(screenshot.relative_to(run.output))
    failure = run.output / "failing-ctest"
    failure.mkdir()
    (failure / "CTestTestfile.cmake").write_text(
        f'add_test(intentional-invalid-mode "{build}/bin/mantis-studio" "--ui-mode" "invalid-control")\n')
    q["test_failure_control"], log = run.command("NC6-real-ctest-failure",
        ["ctest", "--test-dir", failure, "--parallel", "1", "--output-on-failure"], success=False)
    policy.require("Invalid --ui-mode" in log, "Failing-test primary diagnostic absent")
    for name, config in q["configurations"].items():
        _, registered = run.command(name + "-inventory-final", ["ctest", "--test-dir", config["build"], "--show-only=json-v1"])
        config["inventory"] = run.text(name + "/inventory.json", registered)
    run.save("restored-sources.json", source_manifest())
    run.save("qualification.json", q)
    anchor = seal(run.output)
    content = policy.digest(run.output / "sources.json")
    try:
        policy.verify(run.output, anchor, actual_sha, content)
    except policy.InvalidEvidence as error:
        policy.require(str(error) == "Missing completed evidence controls", "Pre-control evidence invalid: " + str(error))
    else:
        raise policy.InvalidEvidence("Incomplete control evidence unexpectedly qualified")
    q["evidence_controls"] = evidence_controls(run, q)
    run.save("qualification.json", q)
    anchor = seal(run.output)
    result = policy.verify(run.output, anchor, actual_sha, content)
    print(json.dumps(dict(result, manifest_sha256=anchor), indent=2), flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--phase", choices=("prepare", "execute"), required=True)
    p.add_argument("--source-sha", required=True)
    p.add_argument("--qt-prefix", type=Path, required=True)
    p.add_argument("--qt-version", required=True)
    p.add_argument("--output", type=Path, required=True, help="New private evidence directory, outside the source tree")
    p.add_argument("--jobs", type=int, default=4, choices=range(1, 9))
    args = p.parse_args()
    args.output, args.qt_prefix = args.output.resolve(), args.qt_prefix.resolve()
    policy.require(not args.output.is_relative_to(SOURCE), "Evidence must be outside source")
    if args.phase == "prepare":
        policy.require(not args.output.exists(), "Preparation requires a new evidence directory")
        args.output.mkdir(parents=True, mode=0o700)
    else:
        policy.require((args.output / "prepared.json").is_file(), "Missing completed preparation")
    try:
        run = Run(args.output, resume=args.phase == "execute")
        (prepare if args.phase == "prepare" else execute)(args, run)
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("BLOCKED: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

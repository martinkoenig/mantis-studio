#!/usr/bin/env python3
"""Real standalone SDK install/relocation/consumer qualification (stdlib only).

Never deletes an output root or treats a timeout/arbitrary error as a negative PASS.
All subprocesses are serial, bounded and owned process groups; receipts retain argv,
logs, exits, identities, inventory hashes and cleanup. Run only with native resource
custody. This is not a production host, shipping-layout or full-product qualifier.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
HEADERS = {
    "c/include/mantis/plugin.h": "c/mantis/plugin.h",
    "c/include/mantis/projected_light.h": "c/mantis/projected_light.h",
    "c/include/mantis/semantic_views.h": "c/mantis/semantic_views.h",
    "cpp/include/mantis/sdk.hpp": "cpp/mantis/sdk.hpp",
}
VERSION = "0.2.0"
NATIVE = {"cmake", "ctest", "ninja", "make", "cc", "gcc", "g++", "c++", "cc1", "cc1plus", "clang", "clang++"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def processes():
    result = []
    if sys.platform == "linux":
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                text = (entry / "stat").read_text()
                comm, rest = text.split("(", 1)[1].rsplit(")", 1)
                fields = rest.split()
                result.append(dict(pid=int(entry.name), comm=comm, state=fields[0],
                                   ppid=int(fields[1]), group=int(fields[2]), birth=fields[19]))
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                continue
    return result


class Campaign:
    def __init__(self, output):
        self.output = output.resolve()
        if self.output.is_relative_to(REPO):
            raise RuntimeError('External installed-consumer evidence must be outside the repository')
        self.output.mkdir(parents=True, exist_ok=False)
        self.records = []
        self.env = os.environ.copy()
        for key in list(self.env):
            if key.startswith("CMAKE_") or key in {"CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH", "GCC_EXEC_PREFIX", "COMPILER_PATH", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "CC", "CXX", "MantisPluginSDK_DIR", "MantisPluginSDK_ROOT", "CTEST_PARALLEL_LEVEL"}:
                self.env.pop(key)
        self.env.update(LC_ALL="C", CMAKE_BUILD_PARALLEL_LEVEL="1", CTEST_PARALLEL_LEVEL="1")
        self.tools = {}
        for key, value in {"cmake": "cmake", "ctest": "ctest", "cc": "cc", "cxx": "c++", "ninja": "ninja"}.items():
            executable = shutil.which(value)
            if executable is None:
                raise RuntimeError(f"Required system tool missing: {value}")
            self.tools[key] = str(Path(executable).resolve())
        self.identity = dict(machine=platform.machine(), system=platform.system(),
                             tools={k: dict(path=v, sha256=digest(Path(v))) for k, v in self.tools.items()},
                             source_headers={k: digest(REPO / "sdk" / k) for k in HEADERS})
        inputs = [REPO / 'CMakeLists.txt', REPO / 'CMakePresets.json', REPO / 'LICENSE',
                  REPO / 'sdk/CMakeLists.txt', REPO / 'sdk/cmake/MantisPluginSDKConfig.cmake.in',
                  REPO / 'tests/contract/c_abi.c', REPO / 'tests/contract/boundaries.py']
        inputs += [p for root in ('tests/sdk', 'tests/contract/l2') for p in (REPO / root).rglob('*')
                   if p.is_file() and '__pycache__' not in p.parts]
        self.identity['source_inputs'] = {str(p.relative_to(REPO)): digest(p) for p in sorted(inputs) if p.is_file()}
        self.save()
        if sys.platform != "linux":
            raise RuntimeError("The qualification process custodian currently requires Linux")
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.previous_subreaper = ctypes.c_int()
        if self.libc.prctl(37, ctypes.byref(self.previous_subreaper), 0, 0, 0) != 0:
            raise OSError(ctypes.get_errno(), "GET_CHILD_SUBREAPER")
        if self.libc.prctl(36, 1, 0, 0, 0) != 0:
            raise OSError(ctypes.get_errno(), "SET_CHILD_SUBREAPER")

    def close(self):
        if self.libc.prctl(36, self.previous_subreaper.value, 0, 0, 0) != 0:
            raise OSError(ctypes.get_errno(), "restore CHILD_SUBREAPER")

    @staticmethod
    def reap_group(group):
        deadline = time.monotonic() + 2
        while True:
            try:
                while os.waitpid(-group, os.WNOHANG)[0] > 0:
                    pass
            except ChildProcessError:
                pass
            remaining = [p for p in processes() if p["group"] == group]
            if not remaining or time.monotonic() >= deadline:
                return remaining
            time.sleep(0.01)  # Only bounded owned-child cancellation/reaping, not success polling.


    def save(self):
        (self.output / "receipts.json").write_text(json.dumps(dict(identity=self.identity, commands=self.records), indent=2) + "\n")

    def run(self, label, args, *, expected=None, env=None, timeout=120):
        actors = processes()
        competitors = [p for p in actors if p["comm"] in NATIVE]
        free = shutil.disk_usage(self.output).free
        if competitors:
            raise RuntimeError(f"Native resource conflict before {label}: {competitors}")
        available = int(next(line.split()[1] for line in Path('/proc/meminfo').read_text().splitlines()
                             if line.startswith('MemAvailable:'))) * 1024
        if free < 512 * 1024 * 1024 or available < 512 * 1024 * 1024:
            raise RuntimeError(f"Insufficient native disk/memory reserve before {label}")
        record = dict(label=label, argv=[str(a) for a in args], cwd=str(self.output),
                      disk_free=free, memory_available=available, timeout=timeout,
                      environment_overrides={key: (env or self.env).get(key) for key in
                                             ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'DESTDIR', 'CMAKE_BUILD_PARALLEL_LEVEL', 'CTEST_PARALLEL_LEVEL')}, controller=[p for p in actors if p["pid"] == os.getpid()],
                      native_competitors=competitors, expected_diagnosis=expected)
        self.records.append(record)
        log = self.output / f"{len(self.records):03d}-{label}.log"
        record["log"] = log.name
        self.save()
        started = time.monotonic()
        child = None
        cause = None
        try:
            with log.open("wb") as stream:
                child = subprocess.Popen(record["argv"], cwd=self.output,
                                         env=env or self.env, stdout=stream, stderr=subprocess.STDOUT,
                                         start_new_session=True)
                record["pid"] = child.pid
                record["child_identity"] = [p for p in processes() if p["pid"] == child.pid]
                try:
                    record["exit"] = child.wait(timeout=timeout)
                except BaseException as exc:
                    cause = exc
                    record["first_cause"] = type(exc).__name__
        finally:
            if child is not None:
                remaining = [p for p in processes() if p["group"] == child.pid]
                record["remaining_before_cleanup"] = remaining
                if child.poll() is None or remaining:
                    try:
                        os.killpg(child.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        child.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        try:
                            child.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            record['cleanup_error'] = 'Owned direct child did not retire after SIGKILL'
                    remaining = [p for p in processes() if p["group"] == child.pid]
                    if remaining:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    record["cleanup_remaining"] = self.reap_group(child.pid)
                    if cause is None:
                        cause = RuntimeError("Unexpected surviving owned subprocess group")
                record["remaining_after_cleanup"] = [p for p in processes() if p["group"] == child.pid]
                record['exit'] = child.returncode
            record["seconds"] = time.monotonic() - started
            self.save()
        if cause:
            raise RuntimeError(f"{label}: {record.get('first_cause', str(cause))}; see {log}") from cause
        text = log.read_text(errors="replace")
        if expected is None:
            if record["exit"] != 0:
                raise RuntimeError(f"{label}: unexpected exit {record['exit']}; see {log}")
        elif record["exit"] <= 0 or not re.search(expected, re.sub(r'\s+', ' ', text)):
            # CMake line-wraps diagnostics according to label/path length. Only
            # normalize whitespace; the actual exit and named cause still must match.
            raise RuntimeError(f"{label}: intended failure not observed; see {log}")
        print(f"{label}: exit={record['exit']}" + (" intended negative" if expected else " PASS"), flush=True)
        return text

    def configure(self, label, source, build, prefix, *extra, expected=None):
        return self.run(label, [self.tools["cmake"], "-S", source, "-B", build, "-G", "Ninja",
                               f"-DCMAKE_MAKE_PROGRAM={self.tools['ninja']}",
                               f"-DCMAKE_C_COMPILER={self.tools['cc']}", f"-DCMAKE_CXX_COMPILER={self.tools['cxx']}",
                               f"-DCMAKE_PREFIX_PATH={prefix}", f"-DSDK_FORBIDDEN_ROOT={REPO / 'sdk'}",
                               "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF", "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
                               "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON", "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=ON",
                               "-DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF", "-DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF",
                               "-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF", *extra], expected=expected)

    def consume(self, label, fixture, prefix, *, c_only=False, sanitize=False):
        build = self.output / (label + " build")
        flags = ["-DC_ONLY=ON"] if c_only else []
        if sanitize:
            # Non-PIE isolates the tiny Linux/GCC sanitizer fixture from ASan shadow/ASLR collisions.
            flags += ["-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie",
                      "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie",
                      "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie"]
        self.configure(label + "-configure", fixture, build, prefix, *flags)
        self.run(label + "-build", [self.tools["cmake"], "--build", build, "--parallel", "1", "--verbose"])
        self.run(label + "-inventory", [self.tools["ctest"], "--test-dir", build, "--show-only=json-v1"])
        environment = self.env.copy()
        if sanitize:
            environment.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        self.run(label + "-test", [self.tools["ctest"], "--test-dir", build, "--parallel", "1", "--output-on-failure", "--verbose"], env=environment)
        origins = (build / "origins.txt").read_text()
        if str(prefix) not in origins or "INTERFACE_LIBRARY" not in origins:
            raise RuntimeError("Installed target origin evidence missing")
        commands = json.loads((build / "compile_commands.json").read_text())
        c_command = next(entry['command'] for entry in commands if entry['file'].endswith('/c-consumer.c'))
        if '-std=c11' not in c_command:
            raise RuntimeError('Imported C language requirement did not raise the C90 client default')
        if not c_only:
            cpp_command = next(entry['command'] for entry in commands if entry['file'].endswith('/cpp-consumer.cpp'))
            if not re.search(r'-std=c\+\+(?:23|2b)', cpp_command):
                raise RuntimeError('Imported C++ requirement did not raise the C++98 client default')
        dependencies = self.run(label + '-dependency-origins', [self.tools['ninja'], '-C', build, '-t', 'deps'])
        for header in HEADERS.values():
            if c_only and header.startswith('cpp/'):
                continue
            if not any(line.strip().endswith(header) for line in dependencies.splitlines()):
                raise RuntimeError(f'Installed compiler dependency missing: {header}')
        for line in dependencies.splitlines():
            if line.strip().endswith(tuple(HEADERS.values())) and str(self.output / 'relocated SDK') not in line and str(self.output / 'wrong-header') not in line:
                raise RuntimeError(f'Compiler dependency did not originate in installed artifact: {line.strip()}')
        for entry in commands:
            # Only the relocated SDK and explicit frozen oracle are candidate include origins.
            for forbidden in (str(REPO / "sdk"), str(self.output / "source-copy"), str(self.output / "producer")):
                if forbidden in entry["command"]:
                    raise RuntimeError("Consumer compiled against source/build SDK path")
        if c_only and list((build / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake')):
            raise RuntimeError("C-only consumer unexpectedly enabled C++")
        return build


def copy_fixture(output):
    fixture = output / "external consumer"
    shutil.copytree(REPO / "tests/sdk/consumer", fixture)
    shutil.copy2(REPO / "tests/contract/c_abi.c", fixture / "existing-c-abi.c")
    shutil.copytree(REPO / "tests/contract/l2", fixture / "l2")
    return fixture


def check_install(prefix, source, libdir, includedir, datadir, cmakedir, forbidden):
    base = f"{includedir}/mantis-plugin-sdk-{VERSION}"
    expected = {f"{base}/{dest}": source / key for key, dest in HEADERS.items()}
    expected[f"{datadir}/licenses/MantisPluginSDK-{VERSION}/LICENSE"] = source.parent / "LICENSE"
    metadata = {f"{cmakedir}/{name}" for name in ("MantisPluginSDKConfig.cmake", "MantisPluginSDKConfigVersion.cmake", "MantisPluginSDKTargets.cmake")}
    files = {str(p.relative_to(prefix)): p for p in prefix.rglob("*") if p.is_file()}
    if set(files) != set(expected) | metadata:
        raise RuntimeError(f"Unexpected installed inventory: {sorted(set(files) ^ (set(expected) | metadata))}")
    for name, origin in expected.items():
        if files[name].read_bytes() != origin.read_bytes():
            raise RuntimeError(f"Installed provenance mismatch: {name}")
    for name in metadata:
        text = files[name].read_text()
        if any(str(path) in text for path in forbidden):
            raise RuntimeError(f"Contaminated source/build path in export: {name}")
        if re.search(r"Qt|protobuf|mantis-(?:runtime|options|platform)|IMPORTED_LOCATION", text):
            raise RuntimeError(f"Private/runtime dependency in export: {name}")
    return {name: digest(path) for name, path in sorted(files.items())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="fresh retained evidence root (never reused/deleted)")
    parser.add_argument("--baseline", action="store_true", help="prove actual missing installed package before implementation")
    args = parser.parse_args()
    campaign = Campaign(args.output)
    try:
        fixture = copy_fixture(campaign.output)
        for key in ("cmake", "cc", "cxx"):
            campaign.run(key + "-identity", [campaign.tools[key], "--version"])
        if args.baseline:
            campaign.configure("baseline-find-package", fixture, campaign.output / "baseline build",
                               campaign.output / "empty prefix", expected=r"Could not find a package configuration file provided by.*MantisPluginSDK")
            return
        source = campaign.output / "source-copy/sdk"
        source.mkdir(parents=True)
        for directory in ("c", "cpp", "cmake"):
            shutil.copytree(REPO / "sdk" / directory, source / directory)
        shutil.copy2(REPO / "sdk/CMakeLists.txt", source / "CMakeLists.txt")
        shutil.copy2(REPO / "LICENSE", source.parent / "LICENSE")
        for label, option in [('absolute', '-DCMAKE_INSTALL_INCLUDEDIR=/outside-prefix'),
                              ('traversal', '-DMantisPluginSDK_INSTALL_CMAKEDIR=../outside-prefix')]:
            campaign.configure('reject-' + label, source, campaign.output / ('invalid-' + label),
                               campaign.output / 'empty', option,
                               expected=r'must be a nonempty prefix-relative path without\s+parent\s+traversal')
        inventories = {}
        # All paths relative to a private prefix: this does not approve a shared shipping layout.
        layouts = [("lib", "include", "share", None, False),
                   ("lib64", "include", "share", None, False),
                   ("native libs", "public includes", "notice data", "metadata/custom SDK", True)]
        for number, (libdir, includedir, datadir, custom, destdir) in enumerate(layouts):
            label = f"layout{number}"
            cmakedir = custom or f"{libdir}/cmake/MantisPluginSDK-{VERSION}"
            producer = campaign.output / f"producer{number}"
            original = campaign.output / f"original prefix {number}"
            campaign.configure(label + "-sdk-configure", source, producer, campaign.output / "empty",
                               f"-DCMAKE_INSTALL_PREFIX={original}", f"-DCMAKE_INSTALL_LIBDIR={libdir}",
                               f"-DCMAKE_INSTALL_INCLUDEDIR={includedir}", f"-DCMAKE_INSTALL_DATADIR={datadir}",
                               f"-DMantisPluginSDK_INSTALL_CMAKEDIR={cmakedir}")
            environment = campaign.env.copy()
            if destdir:
                stage = campaign.output / "DESTDIR staging"
                environment["DESTDIR"] = str(stage)
                installed = stage / original.relative_to(original.anchor)
            else:
                installed = original
            campaign.run(label + "-install", [campaign.tools["cmake"], "--install", producer], env=environment)
            inventories[label] = check_install(installed, source, libdir, includedir, datadir, cmakedir,
                                                [REPO, campaign.output, source, producer, original])
            (campaign.output / "installed-inventories.json").write_text(json.dumps(inventories, indent=2) + "\n")
            relocated = campaign.output / f"relocated SDK {number}"
            installed.rename(relocated)
            producer.rename(campaign.output / f"offline producer {number}")
            offline = source.parent.with_name("offline-source-copy")
            source.parent.rename(offline)
            try:
                # Debian CMake does not automatically search lib64 prefixes. An
                # explicit metadata-directory hint is also the portable custom-dir route.
                prefix = relocated / cmakedir if number != 0 else relocated
                campaign.consume(label, fixture, prefix, c_only=(number == 1))
                if number == 0:
                    campaign.consume("sanitizers", fixture, prefix, sanitize=True)
                    negative_controls(campaign, fixture, relocated, cmakedir)
            finally:
                offline.rename(source.parent)
        (campaign.output / "installed-inventories.json").write_text(json.dumps(inventories, indent=2) + "\n")
        campaign.run("boundaries", [sys.executable, "-B", REPO / "tests/contract/boundaries.py", REPO])
        campaign.run("frozen-catalog", [sys.executable, "-B", REPO / "tests/contract/l2/generate_catalog.py", "--check"])
        (campaign.output / "result.json").write_text(json.dumps(dict(result="PASS", scope="standalone installed SDK only", commands=len(campaign.records)), indent=2) + "\n")
        print("Standalone SDK qualification PASS (not independent ACCEPT)")
    except BaseException as exc:
        (campaign.output / "first-failure.json").write_text(json.dumps(dict(type=type(exc).__name__, diagnosis=str(exc)), indent=2) + "\n")
        raise
    finally:
        campaign.save()
        campaign.close()


def negative_controls(campaign, fixture, installed, cmakedir):
    campaign.configure("unsupported-version", fixture, campaign.output / "unsupported version build", installed,
                       "-DSDK_VERSION=0.2.1", expected=r"Could not find a configuration file.*MantisPluginSDK.*exactly matches requested version.*0.2.1")
    cases = [
        ("missing-header", f"include/mantis-plugin-sdk-{VERSION}/c/mantis/plugin.h", None,
         r"MantisPluginSDK missing public header:.*plugin.h"),
        ("missing-export", f"{cmakedir}/MantisPluginSDKTargets.cmake", None,
         r"include could not find requested file:.*MantisPluginSDKTargets.cmake"),
        ("missing-dependency", f"{cmakedir}/MantisPluginSDKTargets.cmake",
         ('INTERFACE_LINK_LIBRARIES "MantisPluginSDK::mantis-sdk-c"', 'INTERFACE_LINK_LIBRARIES "MantisPluginSDK::missing-c"'),
         r"SDK C\+\+ dependency mismatch"),
        ("contaminated-export", f"{cmakedir}/MantisPluginSDKTargets.cmake",
         ('${_IMPORT_PREFIX}/include', str(REPO / 'sdk/c/include')),
         r"Contaminated source/build path in export"),
    ]
    for name, relative, mutation, expected in cases:
        candidate = campaign.output / name
        shutil.copytree(installed, candidate)
        target = candidate / relative
        if mutation:
            text = target.read_text()
            if mutation[0] not in text:
                raise RuntimeError(f"Mutation not applied: {name}")
            target.write_text(text.replace(*mutation))
        else:
            target.rename(target.with_name(target.name + ".unavailable"))
        if name == "contaminated-export":
            try:
                check_install(candidate, REPO / "sdk", "lib", "include", "share", cmakedir, [REPO])
            except RuntimeError as exc:
                if not re.search(expected, str(exc)):
                    raise
                (campaign.output / "contaminated-export-diagnosis.txt").write_text(str(exc) + "\n")
                campaign.configure(name, fixture, campaign.output / (name + " build"), candidate,
                                   expected=r"SDK source-path export contamination")
            else:
                raise RuntimeError("Contaminated export accepted")
        else:
            campaign.configure(name, fixture, campaign.output / (name + " build"), candidate, expected=expected)
    # Real installed-header mutation: compile/link succeeds but actual SDK ABI contract must go RED.
    bad = campaign.output / "wrong-header"
    shutil.copytree(installed, bad)
    target = bad / f"include/mantis-plugin-sdk-{VERSION}/cpp/mantis/sdk.hpp"
    original = target.read_bytes()
    before = hashlib.sha256(original).hexdigest()
    old = b'p->abi_version == MANTIS_ABI_V1'
    if original.count(old) != 1:
        raise RuntimeError("Wrong-header mutation anchor changed")
    target.write_bytes(original.replace(old, b'true /* disposable ABI-check bypass */'))
    build = campaign.output / "wrong-header build"
    campaign.configure("wrong-header-configure", fixture, build, bad)
    campaign.run("wrong-header-build", [campaign.tools["cmake"], "--build", build, "--parallel", "1"])
    campaign.run("wrong-header-red", [build / "cpp-consumer"], expected=r"SDK contract failed: unsupported queried ABI accepted")
    target.write_bytes(original)
    if digest(target) != before:
        raise RuntimeError("Header restoration identity mismatch")
    campaign.consume("restored-header", fixture, bad)
    (campaign.output / "mutation-restoration.json").write_text(json.dumps(dict(header_before=before, header_restored=digest(target), negative="unsupported queried ABI accepted"), indent=2) + "\n")


if __name__ == "__main__":
    main()

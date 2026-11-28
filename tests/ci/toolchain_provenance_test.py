"""Real standalone C/C++ configurations; no product configure or cache execution."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("MANTIS_PROVENANCE_CLI", ROOT / "tools/ci_toolchain_provenance.py"))
FIXTURE = ROOT / "tests/ci/toolchain_fixture"
INPUTS = ("CMakeLists.txt", "main.cpp", "value.c")
REVISION = os.environ.get("MANTIS_PROVENANCE_REVISION", "1234567890abcdef1234567890abcdef12345678")
CMAKE = os.environ.get("MANTIS_PROVENANCE_CMAKE", "/usr/bin/cmake")
CTEST = os.environ.get("MANTIS_PROVENANCE_CTEST", "/usr/bin/ctest")
NINJA = os.environ.get("MANTIS_PROVENANCE_NINJA", "/usr/bin/ninja")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, timeout: float = 60, env=None):
    child = subprocess.Popen([str(x) for x in command], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, start_new_session=True, env=env)
    birth = Path(f"/proc/{child.pid}/stat").read_text().rsplit(")", 1)[1].split()[19]
    error = None
    try:
        out, err = child.communicate(timeout=timeout)
    except BaseException as initiating:
        error = initiating
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        out, err = child.communicate()
    try:
        os.killpg(child.pid, 0)
        group_retired = False
    except ProcessLookupError:
        group_retired = True
    receipt = {"command": [str(x) for x in command], "pid": child.pid,
               "birth_ticks": birth, "pgid": child.pid, "exit": child.returncode,
               "exception": type(error).__name__ if error else None,
               "group_retired": group_retired, "stdout": out, "stderr": err}
    with (EVIDENCE / "commands.jsonl").open("a") as stream:
        stream.write(json.dumps(receipt) + "\n")
    if error is not None:
        raise error
    if not group_retired:
        raise AssertionError("Owned process group did not retire")
    return subprocess.CompletedProcess(command, child.returncode, out, err)


# Explicit retained evidence root: never silently reuse a configured fixture.
EVIDENCE = Path(os.environ["MANTIS_PROVENANCE_TEST_ROOT"]).resolve()


class RealConfiguration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for executable in (CMAKE, CTEST, NINJA):
            if not Path(executable).is_absolute() or not os.access(executable, os.X_OK):
                raise AssertionError("Explicit absolute available fixture tool required")
        EVIDENCE.mkdir(parents=True, exist_ok=False)
        cls.source = EVIDENCE / "source with spaces"
        cls.build = EVIDENCE / "build with spaces"
        shutil.copytree(FIXTURE, cls.source)
        query = cls.build / ".cmake/api/v1/query"
        query.mkdir(parents=True)
        (query / "toolchains-v1").touch()
        for command in ((CMAKE, "-S", cls.source, "-B", cls.build,
                         "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
                         "-DCMAKE_C_COMPILER=/usr/bin/cc", "-DCMAKE_CXX_COMPILER=/usr/bin/c++",
                         "-DCMAKE_MAKE_PROGRAM=" + NINJA),
                        (CMAKE, "--build", cls.build, "--parallel", "1"),
                        (CTEST, "--test-dir", cls.build, "--output-on-failure", "-j", "1")):
            result = run(command)
            if result.returncode != 0:
                raise AssertionError(f"Fixture prerequisite failed: {result.stderr}")

    def command(self, mode, report=None, source=None, build=None, revision=REVISION,
                dirty="clean", seal=None):
        command = [sys.executable, "-B", CLI, mode, "--source", source or self.source,
                   "--build", build or self.build, "--revision", revision,
                   "--dirty", dirty, "--report", report or self.report]
        for name in INPUTS:
            command += ["--input", name]
        if mode == "verify":
            command += ["--report-sha256", seal or self.seal]
        return command

    def setUp(self):
        self.report = EVIDENCE / (self.id().split(".")[-1] + ".json")

    def capture(self):
        result = run(self.command("capture"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.seal = digest(self.report)
        self.assertEqual(result.stdout.strip(), self.seal)
        return json.loads(self.report.read_text())

    def verify(self):
        result = run(self.command("verify"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "VERIFIED configured-toolchain-v1")

    def test_explicit_fixture_tool_selection(self):
        receipts = [json.loads(line) for line in (EVIDENCE / "commands.jsonl").read_text().splitlines()]
        self.assertEqual(receipts[0]["command"][0],
                         os.environ.get("MANTIS_PROVENANCE_CMAKE", "/usr/bin/cmake"))
        self.assertEqual(receipts[2]["command"][0],
                         os.environ.get("MANTIS_PROVENANCE_CTEST", "/usr/bin/ctest"))
        expected_ninja = os.environ.get("MANTIS_PROVENANCE_NINJA", "/usr/bin/ninja")
        self.assertIn("-DCMAKE_MAKE_PROGRAM=" + expected_ninja, receipts[0]["command"])

    def test_real_capture_verify(self):
        report = self.capture()
        self.assertEqual(report["schema"], "configured-toolchain-v1")
        self.assertEqual(report["configured"]["generator"], "Ninja")
        self.assertEqual(report["configured"]["build_type"], "Debug")
        self.assertEqual(report["binding"], {
            "source_root_sha256": hashlib.sha256(os.fsencode(self.source)).hexdigest(),
            "build_root_sha256": hashlib.sha256(os.fsencode(self.build)).hexdigest()})
        self.assertEqual(report["configured"]["cmake"]["file"]["sha256"], digest(Path(CMAKE)))
        self.assertEqual(report["configured"]["build_tool"]["file"]["sha256"], digest(Path(NINJA)))
        self.assertEqual(report["configured"]["build_tool"]["version"], "UNVERIFIED")
        for lang, executable in (("C", "/usr/bin/cc"), ("CXX", "/usr/bin/c++")):
            tool = report["configured"]["compilers"][lang]
            self.assertEqual(tool["id"], "GNU")
            self.assertEqual(tool["file"]["sha256"], digest(Path(executable).resolve()))
            expected = run([executable, "-dumpfullversion"])
            self.assertEqual(expected.returncode, 0, expected.stderr)
            self.assertEqual(tool["version"], expected.stdout.strip())
            self.assertEqual(tool["origin"], "cmake-file-api:toolchains-v1")
        self.assertEqual(report["inputs"]["main.cpp"], digest(self.source / "main.cpp"))
        self.assertEqual(report["context"], {"revision": REVISION, "dirty": "clean",
                                            "origin": "caller-declared"})
        before = self.report.read_bytes()
        self.verify()
        self.assertEqual(self.report.read_bytes(), before)

    def reject(self, command, diagnosis):
        result = run(command)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stderr.strip(), diagnosis)
        self.assertEqual(result.stdout, "")

    def mutate_file(self, path, replacement, diagnosis):
        original = path.read_bytes()
        frozen = self.report.read_bytes()
        try:
            path.write_bytes(replacement)
            self.reject(self.command("verify"), diagnosis)
            self.assertEqual(self.report.read_bytes(), frozen)
        finally:
            path.write_bytes(original)
        self.verify()

    def test_changed_source_frozen_report(self):
        self.capture()
        self.mutate_file(self.source / "main.cpp", b"// changed input\n",
                         "source-input-mismatch")

    def test_changed_cache_frozen_report(self):
        self.capture()
        path = self.build / "CMakeCache.txt"
        self.mutate_file(path, path.read_bytes() + b"\nUNRELATED:STRING=changed\n",
                         "configuration-fingerprint-mismatch")

    def test_stale_caller_context(self):
        self.capture()
        for options in ({"revision": "f" * 40}, {"dirty": "dirty"}):
            self.reject(self.command("verify", **options), "stale-revision-or-dirty-context")
        self.verify()

    def test_wrong_roots(self):
        self.capture()
        other = EVIDENCE / "other source"
        shutil.copytree(self.source, other)
        self.reject(self.command("verify", source=other), "configured-root-mismatch")
        other_build = EVIDENCE / "copied build"
        shutil.copytree(self.build, other_build)
        self.reject(self.command("verify", build=other_build), "configured-root-mismatch")
        self.verify()

    def test_report_mutations(self):
        self.capture()
        original = self.report.read_bytes()
        for data, diagnosis in ((b'{"schema":', "malformed-report"),
                                (b'{"schema":"future"}', "unsupported-report-schema"),
                                (original + b" ", "report-digest-mismatch")):
            try:
                self.report.write_bytes(data)
                self.reject(self.command("verify"), diagnosis)
            finally:
                self.report.write_bytes(original)
        for change, diagnosis in ((lambda r: r.update(required_future=True), "report-shape-mismatch"),
                                  (lambda r: r["configured"]["cmake"].update(required_future=True),
                                   "configured-tool-or-setting-mismatch")):
            candidate = json.loads(original)
            change(candidate)
            try:
                self.report.write_text(json.dumps(candidate))
                self.reject(self.command("verify", seal=digest(self.report)), diagnosis)
            finally:
                self.report.write_bytes(original)
        self.verify()

    def test_missing_inputs_and_metadata(self):
        self.capture()
        for path, diagnosis in ((self.source / "value.c", "source-input-unavailable"),
                                (self.build / "CMakeCache.txt", "cache-unavailable")):
            hidden = path.with_suffix(path.suffix + ".held")
            path.rename(hidden)
            try:
                self.reject(self.command("verify"), diagnosis)
            finally:
                hidden.rename(path)
        self.verify()

    def cyclic_mutation(self, mutate, restore, diagnosis):
        self.capture()
        frozen = self.report.read_bytes()
        partial = self.report.with_suffix(".partial.json")
        temporaries = set(EVIDENCE.glob(".provenance-*"))
        try:
            mutate()
            # Exercise both entry points even when one has a broken error boundary.
            results = [run(self.command("capture", report=partial)),
                       run(self.command("verify"))]
        finally:
            restore()
            self.assertEqual(self.report.read_bytes(), frozen)
            self.assertFalse(partial.exists())
            self.assertEqual(set(EVIDENCE.glob(".provenance-*")), temporaries)
            self.verify()
        for mode, result in zip(("capture", "verify"), results):
            with self.subTest(mode=mode):
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(result.stderr, diagnosis + "\n")
                self.assertEqual(result.stdout, "")
                self.assertNotIn("Traceback", result.stderr)
                self.assertNotIn(str(EVIDENCE), result.stderr)

    def test_cyclic_configured_launcher(self):
        path = self.build / "CMakeCache.txt"
        original = path.read_bytes()
        launcher = EVIDENCE / "private cyclic launcher"
        launcher.symlink_to("private cyclic launcher")
        try:
            self.cyclic_mutation(
                lambda: path.write_bytes(original + (
                    "\nCMAKE_C_COMPILER_LAUNCHER:STRING=" + str(launcher) + "\n").encode()),
                lambda: path.write_bytes(original), "configured-tool-unavailable")
        finally:
            launcher.unlink()

    def test_cyclic_selected_source_input(self):
        path = self.source / "value.c"
        original = path.read_bytes()
        def mutate():
            path.unlink()
            path.symlink_to("value.c")
        def restore():
            path.unlink()
            path.write_bytes(original)
        self.cyclic_mutation(mutate, restore, "source-input-unavailable")

    def cyclic_cache_root(self, key, current):
        path = self.build / "CMakeCache.txt"
        original = path.read_bytes()
        cyclic = EVIDENCE / ("private cyclic " + key)
        cyclic.symlink_to(cyclic.name)
        before = (key + ":INTERNAL=" + str(current) + "\n").encode()
        after = (key + ":INTERNAL=" + str(cyclic) + "\n").encode()
        self.assertEqual(original.count(before), 1)
        try:
            self.cyclic_mutation(lambda: path.write_bytes(original.replace(before, after)),
                                 lambda: path.write_bytes(original), "configured-root-mismatch")
        finally:
            cyclic.unlink()

    def test_cyclic_cache_source_root(self):
        self.cyclic_cache_root("CMAKE_HOME_DIRECTORY", self.source)

    def test_cyclic_cache_build_root(self):
        self.cyclic_cache_root("CMAKE_CACHEFILE_DIR", self.build)

    def test_relative_cache_root_is_not_resolved_from_cwd(self):
        path = self.build / "CMakeCache.txt"
        original = path.read_bytes()
        relative = os.path.relpath(self.source, Path.cwd())
        replacement = original.replace(str(self.source).encode(), relative.encode())
        try:
            path.write_bytes(replacement)
            self.reject(self.command("capture"), "configured-root-mismatch")
            self.assertFalse(self.report.exists())
        finally:
            path.write_bytes(original)

    def test_normalized_flags_and_toolchain_file(self):
        path = self.build / "CMakeCache.txt"
        original = path.read_bytes()
        toolchain = self.source / "explicit-toolchain.cmake"
        toolchain.write_text("# external configuration input\n")
        try:
            path.write_bytes(original.replace(b"CMAKE_CXX_FLAGS:STRING=", b"CMAKE_CXX_FLAGS:STRING=-DSECRET_SENTINEL")
                             + ("\nCMAKE_TOOLCHAIN_FILE:FILEPATH=" + str(toolchain) + "\n").encode())
            report = self.capture()
            self.assertEqual(report["configured"]["settings_sha256"]["CMAKE_CXX_FLAGS"],
                             hashlib.sha256(b"-DSECRET_SENTINEL").hexdigest())
            self.assertEqual(report["configured"]["toolchain_file"]["sha256"], digest(toolchain))
            self.assertNotIn("SECRET_SENTINEL", self.report.read_text())
            self.assertNotIn(str(EVIDENCE), self.report.read_text())
            self.mutate_file(toolchain, b"# altered\n", "configured-tool-or-setting-mismatch")
        finally:
            path.write_bytes(original)

    def test_overwrite_refused(self):
        self.capture()
        frozen = self.report.read_bytes()
        self.reject(self.command("capture"), "report-exists")
        self.assertEqual(self.report.read_bytes(), frozen)

    def test_launcher_is_data_and_tool_mutation_is_detected(self):
        launcher = EVIDENCE / "launcher not executed"
        marker = EVIDENCE / "EXECUTED"
        launcher.write_text('#!/bin/sh\ntouch "' + str(marker) + '"\nexit 99\n')
        launcher.chmod(0o700)
        path = self.build / "CMakeCache.txt"
        original = path.read_bytes()
        try:
            path.write_bytes(original + ("\nCMAKE_C_COMPILER_LAUNCHER:STRING=" +
                                        str(launcher) + "\n").encode())
            report = self.capture()
            self.assertEqual(report["configured"]["launchers"]["C"]["sha256"], digest(launcher))
            self.mutate_file(launcher, b"#!/bin/sh\nexit 88\n",
                             "configured-tool-or-setting-mismatch")
            launcher.unlink()
            self.reject(self.command("verify"), "configured-tool-unavailable")
            self.assertFalse(marker.exists())
        finally:
            path.write_bytes(original)

    def test_unsupported_bare_launcher_no_path_fallback(self):
        path = self.build / "CMakeCache.txt"
        original = path.read_bytes()
        try:
            path.write_bytes(original + b"\nCMAKE_CXX_COMPILER_LAUNCHER:STRING=ccache\n")
            self.reject(self.command("capture"), "unsupported-tool-path")
            self.assertFalse(self.report.exists())
        finally:
            path.write_bytes(original)

    @classmethod
    def named_fixture(cls):
        if hasattr(cls, "named_build"):
            return
        cls.named_build = EVIDENCE / "named build"
        cls.launcher = EVIDENCE / "owned bin" / "fixture-launcher"
        cls.launcher.parent.mkdir()
        cls.invocations = EVIDENCE / "fixture invocations"
        cls.launcher.write_text('#!/bin/sh\nprintf "fixture-only\\n" >> "' +
                                str(cls.invocations) + '"\nexec "$@"\n')
        cls.launcher.chmod(0o700)
        query = cls.named_build / ".cmake/api/v1/query"
        query.mkdir(parents=True)
        (query / "toolchains-v1").touch()
        # Only trusted fixture setup resolves/executes the owned name. The
        # collector is never given this PATH and cannot claim it selected it.
        env = {**os.environ, "PATH": str(cls.launcher.parent) + ":/usr/bin:/bin"}
        for command in ((CMAKE, "-S", cls.source, "-B", cls.named_build, "-G", "Ninja",
                         "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_C_COMPILER=/usr/bin/cc",
                         "-DCMAKE_CXX_COMPILER=/usr/bin/c++", "-DCMAKE_MAKE_PROGRAM=" + NINJA,
                         "-DCMAKE_C_COMPILER_LAUNCHER=fixture-launcher",
                         "-DCMAKE_CXX_COMPILER_LAUNCHER=fixture-launcher"),
                        (CMAKE, "--build", cls.named_build, "--parallel", "1"),
                        (CTEST, "--test-dir", cls.named_build, "--output-on-failure", "-j", "1")):
            result = run(command, env=env)
            if result.returncode != 0:
                raise AssertionError(result.stderr)
        if len(cls.invocations.read_text().splitlines()) != 2:
            raise AssertionError("Both real fixture languages must use the owned launcher")

    def named_command(self, mode, bindings=None, schema: str | None = "configured-toolchain-v2", **kwargs):
        command = self.command(mode, build=self.named_build, **kwargs)
        if schema is not None:
            command += ["--schema", schema]
        if bindings is None:
            bindings = [(lang, "fixture-launcher", str(self.launcher)) for lang in ("C", "CXX")]
        for binding in bindings:
            command += ["--launcher-binding", *binding]
        return command

    def test_named_launcher_real_v1_boundary_and_v2_capture(self):
        self.named_fixture()
        # This exercises real configured metadata, not unknown v2 arguments.
        self.reject(self.command("capture", build=self.named_build), "unsupported-tool-path")
        self.assertFalse(self.report.exists())
        invoked = self.invocations.read_bytes()
        result = run(self.named_command("capture"), env={"PATH": ""})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.seal = digest(self.report)
        self.assertEqual(result.stdout.strip(), self.seal)
        report = json.loads(self.report.read_text())
        self.assertEqual(report["schema"], "configured-toolchain-v2")
        for lang in ("C", "CXX"):
            self.assertEqual(report["configured"]["launchers"][lang], {
                "configured_name": "fixture-launcher", "origin": "caller-declared",
                "file": {"configured_path_sha256": hashlib.sha256(os.fsencode(self.launcher)).hexdigest(),
                         "resolved_path_sha256": hashlib.sha256(os.fsencode(self.launcher.resolve())).hexdigest(),
                         "sha256": digest(self.launcher)}})
        frozen = self.report.read_bytes()
        result = run(self.named_command("verify"), env={"PATH": "/nonexistent-poison"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "VERIFIED configured-toolchain-v2")
        self.assertEqual(self.report.read_bytes(), frozen)
        self.assertEqual(self.invocations.read_bytes(), invoked)

    def named_capture(self):
        self.named_fixture()
        result = run(self.named_command("capture"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.seal = digest(self.report)
        return self.report.read_bytes()

    def named_reject_both(self, bindings, diagnosis, schema: str | None = "configured-toolchain-v2"):
        frozen = self.report.read_bytes()
        partial = self.report.with_suffix(".partial.json")
        for mode in ("capture", "verify"):
            with self.subTest(mode=mode, diagnosis=diagnosis):
                self.reject(self.named_command(mode, bindings=bindings, schema=schema,
                                              report=partial if mode == "capture" else self.report), diagnosis)
                self.assertFalse(partial.exists())
                self.assertEqual(self.report.read_bytes(), frozen)

    def test_named_binding_validation(self):
        self.named_capture()
        c = ("C", "fixture-launcher", str(self.launcher))
        cxx = ("CXX", "fixture-launcher", str(self.launcher))
        cases = [([], "missing-launcher-binding"),
                 ([c], "missing-launcher-binding"),
                 ([("Fortran", *c[1:])], "unsupported-launcher-binding-language"),
                 ([c, c], "duplicate-launcher-binding"),
                 ([c, ("C", "other-name", "/usr/bin/env")], "duplicate-launcher-binding"),
                 ([c, cxx, c], "launcher-binding-count-limit"),
                 ([("C", "stale-name", c[2]), cxx], "launcher-binding-name-mismatch")]
        for name in ("../fixture-launcher", "./fixture-launcher", "a;b", "a b", ".", "..", "a" * 81, "secret\nname"):
            cases.append(([("C", name, c[2]), cxx], "invalid-launcher-binding-name"))
        for path in ("relative", "./fixture-launcher", "/tmp/../tool", "/tmp//tool", "/tmp/./tool", "/tmp/tool/", "//tmp/tool", "/tmp/a;b", "/tmp/a\\b", "/tmp/a\nsecret", "/" + "x" * 4096):
            cases.append(([("C", c[1], path), cxx], "invalid-launcher-binding-path"))
        for bindings, diagnosis in cases:
            self.named_reject_both(bindings, diagnosis)
        for schema in (None, "configured-toolchain-v1"):
            self.named_reject_both([c, cxx], "launcher-bindings-require-v2", schema)
        self.named_reject_both([c, cxx], "unsupported-selected-schema", "future-secret")
        # Bindings for absent or absolute launchers must not be silently ignored.
        for mode in ("capture", "verify"):
            self.reject(self.command(mode, report=self.report if mode == "verify" else self.report.with_suffix(".partial")) +
                        ["--schema", "configured-toolchain-v2", "--launcher-binding", *c],
                        "unused-launcher-binding")
        self.assertEqual(run(self.named_command("verify")).returncode, 0)

    def test_named_cli_errors_are_bounded(self):
        self.named_fixture()
        base = self.named_command("capture", bindings=[])
        # Python 3.14 argparse consumes unknown option-looking tokens within
        # a fixed nargs group; older versions reject them before our Action.
        # Both layers must reject safely, with the independently expected code.
        option_like = "invalid-launcher-binding-name" if sys.version_info >= (3, 14) else "invalid-command-line"
        for extra, diagnosis in ((["--launcher-binding", "C", "secret-name"], "invalid-command-line"),
                                 (["--launcher-binding", "C", "-private-name", str(self.launcher)], option_like),
                                 (["--schema", "configured-toolchain-v1"], "duplicate-schema-selection"),
                                 (["--schema=future-private"], "duplicate-schema-selection")):
            self.reject(base + extra, diagnosis)
            self.assertFalse(self.report.exists())

    def test_named_metadata_and_scope_rejections(self):
        frozen = self.named_capture()
        path = self.named_build / "CMakeCache.txt"
        original = path.read_bytes()
        before = b"CMAKE_C_COMPILER_LAUNCHER:UNINITIALIZED=fixture-launcher"
        self.assertEqual(original.count(before), 1)
        try:
            for value, diagnosis in (("stale-name", "launcher-binding-name-mismatch"),
                                     ("fixture-launcher;arg", "unsupported-launcher-name"),
                                     ("fixture-launcher --arg", "unsupported-launcher-name"),
                                     ("./fixture-launcher", "unsupported-launcher-name"),
                                     ("../fixture-launcher", "unsupported-launcher-name")):
                path.write_bytes(original.replace(before, before.split(b"=", 1)[0] + b"=" + value.encode()))
                self.named_reject_both(None, diagnosis)
            for value in ("", str(self.launcher)):
                path.write_bytes(original.replace(before, before.split(b"=", 1)[0] + b"=" + value.encode()))
                self.named_reject_both(None, "unused-launcher-binding")
            # Compiler paths are never covered by launcher bindings.
            compiler = b"CMAKE_C_COMPILER:STRING=/usr/bin/cc"
            self.assertEqual(original.count(compiler), 1)
            path.write_bytes(original.replace(compiler, b"CMAKE_C_COMPILER:STRING=fixture-launcher"))
            self.named_reject_both(None, "compiler-metadata-mismatch")
        finally:
            path.write_bytes(original)
        self.assertEqual(self.report.read_bytes(), frozen)
        result = run(self.named_command("verify"))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_named_executable_failures_and_restoration(self):
        frozen = self.named_capture()
        original = self.launcher.read_bytes()
        invoked = self.invocations.read_bytes()
        other = EVIDENCE / "same bytes different target"
        other.write_bytes(original)
        other.chmod(0o700)
        def rejected(diagnosis):
            self.named_reject_both(None, diagnosis)
        try:
            alternate = [(lang, "fixture-launcher", str(other if lang == "C" else self.launcher))
                         for lang in ("C", "CXX")]
            self.reject(self.named_command("verify", bindings=alternate), "configured-tool-or-setting-mismatch")
            self.launcher.write_bytes(b"#!/bin/sh\nexit 99\n")
            # A new explicit capture is permitted; a frozen report is not.
            self.reject(self.named_command("verify"), "configured-tool-or-setting-mismatch")
            changed = self.report.with_suffix(".changed.json")
            result = run(self.named_command("capture", report=changed))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.launcher.write_bytes(original)
            self.launcher.chmod(0o600)
            rejected("configured-tool-unavailable")
            self.launcher.unlink()
            rejected("configured-tool-unavailable")
            self.launcher.mkdir(mode=0o700)
            rejected("configured-tool-unavailable")
            self.launcher.rmdir()
            os.mkfifo(self.launcher, 0o700)
            rejected("configured-tool-size-or-type")
            self.launcher.unlink()
            self.launcher.symlink_to(self.launcher.name)
            rejected("configured-tool-unavailable")
            self.launcher.unlink()
            self.launcher.symlink_to(other)
            self.reject(self.named_command("verify"), "configured-tool-or-setting-mismatch")
            linked = self.report.with_suffix(".linked.json")
            result = run(self.named_command("capture", report=linked))
            self.assertEqual(result.returncode, 0, result.stderr)
            entry = json.loads(linked.read_text())["configured"]["launchers"]["C"]["file"]
            self.assertEqual(entry["sha256"], hashlib.sha256(original).hexdigest())
            self.assertEqual(entry["resolved_path_sha256"], hashlib.sha256(os.fsencode(other)).hexdigest())
            self.launcher.unlink()
            # Real sparse file: zero payload blocks; unchanged 256MiB tool limit.
            with self.launcher.open("wb") as stream:
                stream.truncate(256 * 1024 * 1024 + 1)
            self.launcher.chmod(0o700)
            rejected("configured-tool-size-or-type")
        finally:
            self.launcher.unlink(missing_ok=True)
            self.launcher.write_bytes(original)
            self.launcher.chmod(0o700)
            other.unlink()
        self.assertEqual(self.report.read_bytes(), frozen)
        self.assertEqual(self.invocations.read_bytes(), invoked)
        result = run(self.named_command("verify"))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_named_frozen_report_mutations(self):
        original = self.named_capture()
        changes = [(lambda r: r.update(extra=True), "report-shape-mismatch"),
                   (lambda r: r["configured"]["launchers"]["C"].update(extra=True), "configured-tool-or-setting-mismatch"),
                   (lambda r: r["configured"]["launchers"]["C"].update(configured_name="forged"), "configured-tool-or-setting-mismatch"),
                   (lambda r: r["configured"]["launchers"]["C"].update(origin="PATH-selected"), "configured-tool-or-setting-mismatch"),
                   (lambda r: r["configured"]["launchers"]["C"]["file"].update(sha256="0" * 64), "configured-tool-or-setting-mismatch"),
                   (lambda r: r.update(schema="configured-toolchain-v1"), "unsupported-report-schema"),
                   (lambda r: r["unverified"].remove("launcher-selection"), "report-claims-mismatch")]
        try:
            self.report.write_bytes(original + b" ")
            self.reject(self.named_command("verify"), "report-digest-mismatch")
            for mutate, diagnosis in changes:
                candidate = json.loads(original)
                mutate(candidate)
                self.report.write_text(json.dumps(candidate))
                # Adversarial re-seal passes the integrity layer, not semantics.
                self.reject(self.named_command("verify", seal=digest(self.report)), diagnosis)
        finally:
            self.report.write_bytes(original)
        self.reject(self.named_command("verify", schema=None, bindings=[]), "unsupported-report-schema")
        self.reject(self.named_command("capture"), "report-exists")
        self.assertEqual(self.report.read_bytes(), original)
        result = run(self.named_command("verify"))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_named_acquisition_denial_and_readonly_verification(self):
        frozen = self.named_capture()
        invoked = self.invocations.read_bytes()
        script = EVIDENCE / "named_guard.py"
        script.write_text('''import importlib.util, os, sys
from unittest import mock
spec = importlib.util.spec_from_file_location("guarded_provenance", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class NoEnvironment(dict):
    def get(self, *args):
        raise RuntimeError("environment inspection")
    def __getitem__(self, key):
        raise RuntimeError("environment inspection")
    def __iter__(self):
        raise RuntimeError("environment inspection")
    def __contains__(self, key):
        raise RuntimeError("environment inspection")
original = module.snapshot
def guarded_snapshot(args):
    # Standard-library argument help/localization runs outside data acquisition.
    with mock.patch.object(os, "environ", NoEnvironment()):
        with mock.patch.object(os, "get_exec_path", side_effect=RuntimeError("PATH lookup")):
            return original(args)
module.snapshot = guarded_snapshot
verify = sys.argv[2] == "verify"
def audit(event, args):
    if event.startswith(("subprocess.", "socket.")) or event in ("os.system", "os.exec", "os.posix_spawn"):
        raise RuntimeError("acquisition side effect")
    if verify and ((event == "open" and args[2] & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND)) or event in (
            "os.remove", "os.rename", "os.link", "os.mkdir", "os.chmod", "os.symlink")):
        raise RuntimeError("verification write")
sys.addaudithook(audit)
raise SystemExit(module.main(sys.argv[2:]))
''')
        before = {str(p): digest(p) for p in [*self.source.rglob("*"), *self.named_build.rglob("*"), self.report, self.launcher]
                  if p.is_file()}
        for mode in ("capture", "verify"):
            report = self.report.with_suffix(".guarded.json") if mode == "capture" else self.report
            result = run([sys.executable, "-B", script, CLI] + self.named_command(mode, report=report)[3:],
                         env={"PATH": ""})
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual({p: digest(Path(p)) for p in before}, before)
        self.assertEqual(self.report.read_bytes(), frozen)
        self.assertEqual(self.invocations.read_bytes(), invoked)
        self.assertEqual(self.report.with_suffix(".guarded.json").read_bytes(), frozen)

    def test_v2_absolute_no_map_and_v1_frozen_compatibility(self):
        # CI checks both version contracts without acquiring Git/network. Local
        # qualification additionally verifies reports frozen by the parent CLI.
        original = self.capture()
        frozen = self.report.read_bytes()
        expected = self.seal
        explicit = run(self.command("verify") + ["--schema", "configured-toolchain-v1"])
        self.assertEqual(explicit.returncode, 0, explicit.stderr)
        self.assertEqual(self.seal, expected)
        self.assertEqual(self.report.read_bytes(), frozen)
        other = self.report.with_suffix(".v2.json")
        result = run(self.command("capture", report=other) + ["--schema", "configured-toolchain-v2"])
        self.assertEqual(result.returncode, 0, result.stderr)
        v2 = json.loads(other.read_text())
        self.assertEqual(v2["configured"], original["configured"])
        self.assertEqual(v2["observed"], original["observed"])
        self.assertEqual(v2["unverified"], original["unverified"] + ["launcher-selection"])
        result = run(self.command("verify", report=other, seal=digest(other)) + ["--schema", "configured-toolchain-v2"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.reject(self.command("verify", report=other, seal=digest(other)), "unsupported-report-schema")
        self.verify()

    def test_second_real_configuration(self):
        source = EVIDENCE / "alternate" / "src"
        build = EVIDENCE / "alternate" / "out"
        shutil.copytree(FIXTURE, source)
        query = build / ".cmake/api/v1/query"
        query.mkdir(parents=True)
        (query / "toolchains-v1").touch()
        configured = run([CMAKE, "-S", source, "-B", build, "-G", "Ninja",
                          "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=/usr/bin/cc",
                          "-DCMAKE_CXX_COMPILER=/usr/bin/c++", "-DCMAKE_MAKE_PROGRAM=" + NINJA,
                          "-DCMAKE_C_COMPILER_LAUNCHER=/usr/bin/env",
                          "-DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/env"])
        self.assertEqual(configured.returncode, 0, configured.stderr)
        built = run([CMAKE, "--build", build, "--parallel", "1"])
        self.assertEqual(built.returncode, 0, built.stderr)
        tested = run([CTEST, "--test-dir", build, "--output-on-failure", "-j", "1"])
        self.assertEqual(tested.returncode, 0, tested.stderr)
        captured = run(self.command("capture", source=source, build=build))
        self.assertEqual(captured.returncode, 0, captured.stderr)
        self.seal = digest(self.report)
        verified = run(self.command("verify", source=source, build=build))
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertEqual(json.loads(self.report.read_text())["configured"]["build_type"], "Release")
        v2_report = self.report.with_suffix(".absolute-v2.json")
        captured = run(self.command("capture", report=v2_report, source=source, build=build) +
                       ["--schema", "configured-toolchain-v2"])
        self.assertEqual(captured.returncode, 0, captured.stderr)
        self.assertEqual(json.loads(v2_report.read_text())["configured"],
                         json.loads(self.report.read_text())["configured"])
        verified = run(self.command("verify", report=v2_report, seal=digest(v2_report), source=source, build=build) +
                       ["--schema", "configured-toolchain-v2"])
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.reject(self.command("verify"), "root-binding-mismatch")

    def test_malformed_and_unsupported_metadata(self):
        self.capture()
        reply = self.build / ".cmake/api/v1/reply"
        index_path = next(reply.glob("index-*.json"))
        index = json.loads(index_path.read_text())
        chains = reply / index["reply"]["toolchains-v1"]["jsonFile"]
        original = chains.read_bytes()
        try:
            for data, diagnosis in ((b"{", "malformed-toolchains"),
                                    (b'{"kind":"toolchains","version":{"major":2,"minor":0}}',
                                     "unsupported-toolchains-version"),
                                    (b'{}', "missing-or-malformed-configured-metadata")):
                chains.write_bytes(data)
                self.reject(self.command("verify"), diagnosis)
        finally:
            chains.write_bytes(original)
        held = index_path.with_suffix(".held")
        index_path.rename(held)
        try:
            self.reject(self.command("verify"), "missing-or-ambiguous-file-api-index")
        finally:
            held.rename(index_path)
        self.verify()

    def test_malformed_cache_and_size_limit(self):
        path = self.build / "CMakeCache.txt"
        original = path.read_bytes()
        try:
            for data, diagnosis in ((b"truncated cache", "malformed-cache"),
                                    (original + b"\nCMAKE_BUILD_TYPE:STRING=Release\n", "malformed-cache"),
                                    (b"x" * (8 * 1024 * 1024 + 1), "cache-size-or-type")):
                path.write_bytes(data)
                self.reject(self.command("capture"), diagnosis)
                self.assertFalse(self.report.exists())
        finally:
            path.write_bytes(original)
        self.capture()
        self.verify()

    def test_input_symlink_cannot_escape_root(self):
        path = self.source / "value.c"
        original = path.read_bytes()
        outside = EVIDENCE / "outside.c"
        outside.write_bytes(original)
        try:
            path.unlink()
            path.symlink_to(outside)
            self.reject(self.command("capture"), "input-root-escape")
            self.assertFalse(self.report.exists())
        finally:
            path.unlink()
            path.write_bytes(original)
        self.capture()
        self.verify()

    def test_missing_report_does_not_generate(self):
        self.seal = "0" * 64
        self.reject(self.command("verify"), "report-unavailable")
        self.assertFalse(self.report.exists())

    def test_input_escape_duplicate_and_fifo(self):
        self.capture()
        for name in ("../outside", "CMakeLists.txt"):
            self.reject(self.command("verify") + ["--input", name], "invalid-input-list")
        path = self.source / "value.c"
        original = path.read_bytes()
        try:
            path.unlink()
            os.mkfifo(path)
            self.reject(self.command("verify"), "source-input-size-or-type")
        finally:
            path.unlink()
            path.write_bytes(original)
        self.verify()

    def test_import_is_inert(self):
        script = EVIDENCE / "import_guard.py"
        script.write_text('''import argparse, dataclasses, hashlib, inspect, itertools, json
import os, pathlib, re, stat, sys, tempfile, typing, importlib.util
path = sys.argv[1]
spec = importlib.util.spec_from_file_location("inert_provenance", path)
module = importlib.util.module_from_spec(spec)
code = spec.loader.get_code("inert_provenance")
def audit(event, args):
    if event == "open":
        raise RuntimeError("unexpected import filesystem access")
    if event.startswith(("subprocess.", "socket.", "os.")):
        raise RuntimeError("unexpected import acquisition")
sys.addaudithook(audit)
exec(code, module.__dict__)
assert inspect.isfunction(module.verify)
@dataclasses.dataclass
class Healthy:
    value: int
assert Healthy(42).value == 42
print("INERT")
''')
        result = run([sys.executable, "-B", script, CLI])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "INERT")

    def test_unrelated_failure_and_timeout_are_not_negatives(self):
        script = EVIDENCE / "unrelated.py"
        script.write_text("raise SystemExit(99)\n")
        with self.assertRaises(AssertionError):
            self.reject([sys.executable, "-B", script], "source-input-mismatch")
        script.write_text("import sys\nprint('unrelated', file=sys.stderr)\nraise SystemExit(2)\n")
        with self.assertRaises(AssertionError):
            self.reject([sys.executable, "-B", script], "source-input-mismatch")
        script.write_text("import signal\nsignal.pause()\n")
        with self.assertRaises(subprocess.TimeoutExpired):
            run([sys.executable, "-B", script], timeout=0.2)

    def test_verify_is_read_only(self):
        self.capture()
        script = EVIDENCE / "readonly_guard.py"
        script.write_text('''import importlib.util, os, sys
spec = importlib.util.spec_from_file_location("readonly_provenance", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
def audit(event, args):
    if event == "open" and args[2] & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND):
        raise RuntimeError("verification write")
    if event.startswith(("subprocess.", "socket.")) or event in (
            "os.remove", "os.rename", "os.link", "os.mkdir", "os.chmod", "os.symlink"):
        raise RuntimeError("verification side effect")
sys.addaudithook(audit)
raise SystemExit(module.main(sys.argv[2:]))
''')
        def inventory():
            paths = list(self.source.rglob("*")) + list(self.build.rglob("*")) + [self.report]
            return {str(p): digest(p) for p in paths if p.is_file()}
        before = inventory()
        result = run([sys.executable, "-B", script, CLI] + self.command("verify")[3:])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "VERIFIED configured-toolchain-v1")
        self.assertEqual(inventory(), before)

    def test_partial_publication_and_primary_error(self):
        spec = importlib.util.spec_from_file_location("publication_test", CLI)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        directory = EVIDENCE / "publication"
        directory.mkdir()
        target = directory / "report.json"
        sentinel = directory / "unrelated"
        sentinel.write_text("preserve")
        with mock.patch.object(module.os, "fsync", side_effect=OSError("injected write failure")):
            with self.assertRaisesRegex(module.Invalid, "^report-publication-failed$"):
                module.publish(target, b"partial bytes")
        self.assertFalse(target.exists())
        self.assertEqual(sorted(p.name for p in directory.iterdir()), ["unrelated"])
        with mock.patch.object(module.os, "link", side_effect=OSError("initiating link error")):
            with mock.patch.object(module.Path, "unlink", side_effect=OSError("secondary cleanup error")):
                with self.assertRaisesRegex(module.Invalid, "^report-publication-failed$"):
                    module.publish(target, b"complete bytes")
        self.assertFalse(target.exists())
        self.assertEqual(sentinel.read_text(), "preserve")
        # This injected cleanup failure deliberately retains a private temporary.
        leftovers = list(directory.glob(".provenance-*"))
        self.assertEqual(len(leftovers), 1)
        leftovers[0].unlink()
        target.write_bytes(b"existing frozen bytes")
        with self.assertRaisesRegex(module.Invalid, "^report-exists$"):
            module.publish(target, b"replacement")
        self.assertEqual(target.read_bytes(), b"existing frozen bytes")
        self.assertEqual(sorted(p.name for p in directory.iterdir()), ["report.json", "unrelated"])


if __name__ == "__main__":
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""One CTest: actual LSan detection plus adversarial verifier/child-lifetime controls."""
from dataclasses import replace
import json
import os
from pathlib import Path
import signal
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

import verify as v


class CanaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env = v.strict_environment()
        cls.actual, cls.nonce, cls.size, cls.source = v.qualify(BINARY, cls.env)
        print(json.dumps({"positive": "QUALIFIED", "child_exit": cls.actual.code,
                          "seconds": cls.actual.seconds, "binary_sha256": v.digest(BINARY),
                          "allocation_bytes": cls.size}), flush=True)

    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="mantis-lsan-negative-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.mask = signal.pthread_sigmask(signal.SIG_BLOCK, set())
        self.handler = signal.getsignal(signal.SIGTERM)
        self.subreaper = self.subreaper_state()

    @staticmethod
    def subreaper_state():
        state = v.ctypes.c_int()
        if v.ctypes.CDLL(None).prctl(37, v.ctypes.byref(state), 0, 0, 0) != 0:
            raise RuntimeError("cannot inspect verifier subreaper state")
        return state.value

    def tearDown(self):
        self.assertEqual(signal.pthread_sigmask(signal.SIG_BLOCK, set()), self.mask)
        self.assertEqual(signal.getsignal(signal.SIGTERM), self.handler)
        self.assertEqual(self.subreaper_state(), self.subreaper)

    def fixture(self, text):
        path = self.root / "fixture"
        path.write_text("#!/usr/bin/python3\nimport resource\nresource.setrlimit(resource.RLIMIT_CORE,(0,0))\n" + text)
        path.chmod(0o700)
        return path

    def validate(self, result, **kw):
        args = dict(nonce=self.nonce, size=self.size, source=self.source, inspected=True)
        args.update(kw)
        v.validate(result, **args)

    def test_real_detector_and_fresh_receipt(self):
        self.validate(self.actual)
        second, nonce, size, source = v.qualify(BINARY, self.env)
        v.validate(second, nonce, size, source, True)
        with self.assertRaisesRegex(v.Unqualified, "stale/substituted"):
            v.validate(self.actual, nonce, size, source, True)

    def test_disabled_detector_really_executes_and_cannot_qualify(self):
        env = dict(self.env, ASAN_OPTIONS="detect_leaks=0")
        with self.assertRaisesRegex(v.Unqualified, "zero/signal"):
            v.qualify(BINARY, env)
        with patch.dict(os.environ, ASAN_OPTIONS="detect_leaks=0"):
            with self.assertRaisesRegex(v.Unqualified, "ASAN_OPTIONS"):
                v.strict_environment()

    def test_actual_uninstrumented_cpp_child(self):
        for advertised in (False, True):
            with self.subTest(advertised=advertised):
                binary = self.root / ("uninstrumented-" + str(advertised))
                flags = ["-D__SANITIZE_ADDRESS__=1"] if advertised else []
                result = v.execute([COMPILER, "-std=c++23", *flags, self.source, "-o", binary],
                                   self.env, timeout=10)
                self.assertEqual(result.code, 0, result.stderr)
                reason = "sanitizer runtime" if advertised else "handshake"
                with self.assertRaisesRegex(v.Unqualified, reason) as rejected:
                    v.qualify(binary, self.env)
                self.assertIn("asan=" + str(int(advertised)), str(rejected.exception))

    def test_actual_stripped_binary_is_unqualified(self):
        binary = self.root / "stripped"
        shutil.copyfile(BINARY, binary)
        binary.chmod(0o700)
        result = v.execute(["/usr/bin/strip", "--strip-debug", binary], self.env)
        self.assertEqual(result.code, 0, result.stderr)
        with self.assertRaisesRegex(v.Unqualified, "source/toolchain compilation unit"):
            v.qualify(binary, self.env)

    def test_zero_arbitrary_nonzero_signal_and_no_execution(self):
        crash = self.fixture("import os, signal\nos.kill(os.getpid(), signal.SIGSEGV)\n")
        for command, code in ((["/usr/bin/true"], 0), (["/usr/bin/false"], 1), ([crash], -signal.SIGSEGV)):
            with self.subTest(code=code):
                actual = v.execute(command, self.env)
                self.assertEqual(actual.code, code)
                with self.assertRaises(v.Unqualified):
                    self.validate(actual)
        with self.assertRaisesRegex(v.Unqualified, "FileNotFoundError"):
            v.qualify(self.root / "not-executed", self.env)

    def test_stale_and_fabricated_output_from_real_substituted_children(self):
        replay = self.fixture("import os\nos.write(1," + repr(self.actual.stdout) + ")\n"
                              "os.write(2," + repr(self.actual.stderr) + ")\nraise SystemExit(1)\n")
        with self.assertRaisesRegex(v.Unqualified, "handshake"):
            v.qualify(replay, self.env)
        fake = self.fixture("import os,sys\n"
                            "print(f'mantis-lsan-v1 pid={os.getpid()} asan=1',flush=True)\n"
                            "sys.stdin.readline()\n")
        with self.assertRaisesRegex(v.Unqualified, "executable identity changed"):
            v.qualify(fake, self.env)

    def test_actual_report_mutations_never_qualify(self):
        stderr = self.actual.stderr
        changes = {
            "missing": b"", "fabricated": b"ERROR: LeakSanitizer: detected memory leaks\n",
            "wrong-allocation": stderr.replace(str(self.size).encode(), b"123"),
            "wrong-site": stderr.replace(b"mantisHeadlessLeakCanary", b"unrelatedAllocation"),
            "wrong-line": stderr.replace(b"canary.cpp:", b"canary.cpp:999"),
            "wrong-pid": stderr.replace(f"=={self.actual.pid}==".encode(), b"==1=="),
            "extra-stderr": stderr + b"unrelated error\n",
            "sanitizer-fatal": stderr + b"LeakSanitizer has encountered a fatal error\n",
            "unrelated-crash": stderr + b"AddressSanitizer:DEADLYSIGNAL\n",
            "malformed": stderr.replace(b"SUMMARY:", b"incomplete:"),
        }
        for name, changed in changes.items():
            with self.subTest(name=name), self.assertRaises(v.Unqualified):
                self.validate(replace(self.actual, stderr=changed))
        for result in (replace(self.actual, stdout=b""), replace(self.actual, code=0),
                       replace(self.actual, code=-signal.SIGSEGV)):
            with self.assertRaises(v.Unqualified):
                self.validate(result)
        with self.assertRaisesRegex(v.Unqualified, "no live"):
            self.validate(self.actual, inspected=False)

    def test_actual_instrumented_fake_reporter_cannot_qualify(self):
        source = self.root / "fake.cpp"
        binary = self.root / "fake"
        line = next(n for n, text in enumerate(self.source.read_text().splitlines(), 1)
                    if "std::malloc(bytes)" in text)
        source.write_text(r'''
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
__attribute__((noinline)) void harmless(unsigned char* p) { p[0] = 42; }
int main() {
    auto* p = static_cast<unsigned char*>(std::malloc(8));
    harmless(p);
    std::free(p);
    long pid = static_cast<long>(getpid());
    std::printf("mantis-lsan-v1 pid=%ld asan=1\n", pid);
    std::fflush(stdout);
    char nonce[65]{};
    unsigned long bytes = 0;
    if (std::scanf("%64s %lu", nonce, &bytes) != 2) return 2;
    std::printf("allocation nonce=%s pid=%ld bytes=%lu\n", nonce, pid, bytes);
    std::fprintf(stderr, "\n=================================================================\n"
        "==%ld==ERROR: LeakSanitizer: detected memory leaks\n\n"
        "Direct leak of %lu byte(s) in 1 object(s) allocated from:\n"
        "    #0 0x1 in malloc fake:1\n"
        "    #1 0x2 in mantisHeadlessLeakCanary(unsigned long) SOURCE:LINE\n"
        "    #2 0x3 in main fake:2\n\n"
        "SUMMARY: AddressSanitizer: %lu byte(s) leaked in 1 allocation(s).\n", pid, bytes, bytes);
    return 1;
}
'''.replace("SOURCE:LINE", str(self.source) + ":" + str(line)))
        compiled = v.execute([COMPILER, "-std=c++23", "-g", "-fsanitize=address,undefined",
                              "-fno-omit-frame-pointer", source, "-o", binary], self.env, timeout=10)
        self.assertEqual(compiled.code, 0, compiled.stderr)
        released = False

        def release(process, stdout):
            nonlocal released
            if not released and b"\n" in stdout:
                released = True
                process.stdin.write(f"{self.nonce} {self.size}\n".encode())
                process.stdin.flush()
                process.stdin.close()

        # The real instrumented fake exits nonzero with plausible current-PID/nonce output.
        # Its text passes the parser. Only the full live provenance path may qualify a child.
        fake = v.execute([binary], self.env, output=release)
        self.assertEqual(fake.code, 1)
        self.validate(fake)
        with self.assertRaisesRegex(v.Unqualified, "source/toolchain compilation unit"):
            v.qualify(binary, self.env)

    def test_missing_live_runtime_or_symbols_rejects_actual_child(self):
        for maps, symbol_result in (("no sanitizer", None), ("libasan.so libubsan.so libQt6Core", None),
                                    ("libasan.so libubsan.so", replace(self.actual, code=0, stdout=b"no symbols"))):
            with self.subTest(maps=maps):
                real_read = Path.read_text

                def read(path, *args, **kwargs):
                    return maps if str(path).endswith("/maps") else real_read(path, *args, **kwargs)

                # The actual canary is alive at the inspection point; inject missing inspection evidence.
                with patch.object(Path, "read_text", read):
                    if symbol_result is None:
                        with self.assertRaises(v.Unqualified):
                            v.qualify(BINARY, self.env)
                    else:
                        real_execute = v.execute

                        def execute(argv, *args, **kwargs):
                            if str(argv[0]) == "/usr/bin/readelf":
                                return symbol_result
                            return real_execute(argv, *args, **kwargs)

                        with patch.object(v, "execute", execute), self.assertRaisesRegex(v.Unqualified, "symbols"):
                            v.qualify(BINARY, self.env)

    def test_timeout_output_overflow_and_exception_cleanup(self):
        for mode in ("timeout", "overflow", "exception", "interrupt", "cancel"):
            with self.subTest(mode=mode):
                fixture = self.fixture("import os,time\nprint('primary',flush=True)\n" +
                                       ("os.write(2,b'x'*70000)\n" if mode == "overflow" else "") +
                                       "time.sleep(30)\n")
                pids = []

                def observe(process, stdout):
                    if not pids:
                        pids.append(process.pid)
                    if mode == "exception":
                        raise RuntimeError("inspection failed")
                    if mode == "interrupt":
                        raise KeyboardInterrupt()
                    if mode == "cancel":
                        os.kill(os.getpid(), signal.SIGTERM)

                error = {"exception": RuntimeError, "interrupt": KeyboardInterrupt,
                         "cancel": KeyboardInterrupt}.get(mode, v.Unqualified)
                with self.assertRaises(error) as rejected:
                    v.execute([fixture], self.env, timeout=.2, output=observe)
                self.assertEqual(len(pids), 1)
                with self.assertRaises(ProcessLookupError):
                    os.kill(pids[0], 0)
                if mode in ("timeout", "overflow"):
                    self.assertIn("primary", str(rejected.exception))
                    self.assertLess(len(str(rejected.exception)), 10000)

    def test_timeout_reaps_the_process_group_including_descendant(self):
        for parent_exits in (False, True):
            with self.subTest(parent_exits=parent_exits):
                fixture = self.fixture("import os,time\nchild=os.fork()\n"
                                       "if child == 0:\n time.sleep(30)\n os._exit(0)\n"
                                       "print(child,flush=True)\n" +
                                       ("os._exit(0)\n" if parent_exits else "os.waitpid(child,0)\n"))
                pids = set()

                def observe(process, stdout):
                    pids.update((process.pid, int(stdout.strip())))

                with self.assertRaisesRegex(v.Unqualified, "timeout"):
                    v.execute([fixture], self.env, timeout=.2, output=observe)
                self.assertEqual(len(pids), 2)
                for pid in pids:
                    with self.assertRaises(ProcessLookupError):
                        os.kill(pid, 0)

    def test_evidence_io_failure_preserves_primary_exception_and_reaps_child(self):
        fixture = self.fixture("import time\nprint('ready',flush=True)\ntime.sleep(30)\n")
        pids = []

        def observe(process, stdout):
            pids.append(process.pid)
            raise RuntimeError("primary inspection failure")

        with patch.object(v, "EVIDENCE", self.root), patch.object(Path, "write_text", side_effect=OSError()):
            with self.assertRaisesRegex(RuntimeError, "primary inspection failure") as rejected:
                v.execute([fixture], self.env, output=observe)
        self.assertIn("evidence write failed: OSError", rejected.exception.__notes__)
        self.assertEqual(len(pids), 1)
        with self.assertRaises(ProcessLookupError):
            os.kill(pids[0], 0)

    def test_closed_stdin_preserves_primary_failure_and_reaps_child(self):
        fixture = self.fixture("import os,time\nos.close(0)\nprint('ready',flush=True)\ntime.sleep(30)\n")
        pids = []

        def observe(process, stdout):
            pids.append(process.pid)
            process.stdin.write(b"go\n")
            process.stdin.flush()

        with self.assertRaisesRegex(v.Unqualified, "BrokenPipeError") as rejected:
            v.execute([fixture], self.env, output=observe)
        self.assertIsInstance(rejected.exception.__cause__, BrokenPipeError)
        self.assertEqual(len(pids), 1)
        with self.assertRaises(ProcessLookupError):
            os.kill(pids[0], 0)

    def test_unsupported_configuration_is_unqualified(self):
        for key, value in (("ASAN_OPTIONS", "detect_leaks=1:exitcode=0"),
                           ("UBSAN_OPTIONS", "halt_on_error=0"),
                           ("LSAN_OPTIONS", "suppressions=anything"), ("LD_PRELOAD", "anything")):
            with self.subTest(key=key), patch.dict(os.environ, {key: value}):
                with self.assertRaises(v.Unqualified):
                    v.strict_environment()


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit("Usage: test_verifier.py CANARY CXX_COMPILER [PRIVATE_EVIDENCE_DIRECTORY]")
    BINARY, COMPILER = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    if len(sys.argv) == 4:
        parent = Path(sys.argv[3])
        parent.mkdir(parents=True, exist_ok=True)
        v.EVIDENCE = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    unittest.main(argv=[sys.argv[0]], verbosity=2)

"""Linux headless LSan canary. Qualification consumes live pipes, never supplied logs/receipts.

The nonce, PID, live ELF/runtime inspection and random allocation size detect realistic
stale/substituted fixtures. They are not attestation against a compromised build host.
"""
from dataclasses import dataclass
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import selectors
import signal
import subprocess
import sys
import time

LIMIT = 65536
DEADLINE = 5
EVIDENCE = None


class Unqualified(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise Unqualified(message)


@dataclass(frozen=True)
class Execution:
    pid: int
    code: int
    stdout: bytes
    stderr: bytes
    seconds: float


def execute(argv, env, *, timeout=DEADLINE, output=None):
    """Bound both streams together, own the process group before invoking any callback.

    Close/kill/reap on output overflow, timeout, signal cancellation and callback exceptions,
    including when a descendant retains a pipe after its parent exits.
    """
    started = time.monotonic()
    streams = {"stdout": bytearray(), "stderr": bytearray()}
    process = None
    old_term = signal.getsignal(signal.SIGTERM)
    # Become a subreaper only inside this isolated verifier. Descendants of a failed fixture
    # are adopted here, killed with its group and reaped, rather than left to the host's init.
    libc = ctypes.CDLL(None, use_errno=True)
    previous = ctypes.c_int()
    require(libc.prctl(37, ctypes.byref(previous), 0, 0, 0) == 0 and
            libc.prctl(36, 1, 0, 0, 0) == 0, "UNQUALIFIED: Linux subreaper unavailable")
    old_mask = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGCHLD})
    failure = None
    executable_hash = None

    def cancel(signum, frame):
        raise KeyboardInterrupt("cancelled")

    signal.signal(signal.SIGTERM, cancel)
    try:
        if EVIDENCE is not None and Path(str(argv[0])).is_file():
            executable_hash = digest(argv[0])
        process = subprocess.Popen(list(map(str, argv)), env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   start_new_session=True)
        with selectors.DefaultSelector() as selector:
            for name in streams:
                pipe = getattr(process, name)
                os.set_blocking(pipe.fileno(), False)
                selector.register(pipe, selectors.EVENT_READ, name)
            while selector.get_map():
                remaining = timeout - (time.monotonic() - started)
                require(remaining > 0, "timeout")
                events = selector.select(remaining)
                require(events, "timeout")
                for key, _ in events:
                    block = os.read(key.fd, 4096)
                    if not block:
                        selector.unregister(key.fileobj)
                        continue
                    used = sum(map(len, streams.values()))
                    streams[key.data].extend(block[:max(0, LIMIT - used)])
                    require(used + len(block) <= LIMIT, "output limit")
                    if output:
                        output(process, bytes(streams["stdout"]))
            remaining = timeout - (time.monotonic() - started)
            require(remaining > 0, "timeout")
            code = process.wait(timeout=remaining)
        return Execution(process.pid, code, bytes(streams["stdout"]), bytes(streams["stderr"]),
                         time.monotonic() - started)
    except (OSError, subprocess.TimeoutExpired) as error:
        failure = type(error).__name__
        raise Unqualified(type(error).__name__) from error
    except BaseException as error:
        failure = type(error).__name__ + ": " + str(error)
        # JSON escaping prevents child text becoming terminal/Actions commands. Keep the primary prefix.
        if isinstance(error, Unqualified):
            error.args = (str(error) + " " + json.dumps({
                k: bytes(v[:4096]).decode(errors="replace") for k, v in streams.items()}),)
        raise
    finally:
        primary = sys.exc_info()[1]
        cleanup_error = None
        try:
            if process is not None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=2)
                deadline = time.monotonic() + 2
                while True:
                    try:
                        pid, _ = os.waitpid(-process.pid, os.WNOHANG)
                    except ChildProcessError:
                        break
                    remaining = deadline - time.monotonic()
                    require(remaining > 0, "descendant cleanup timeout")
                    if pid == 0:
                        signal.sigtimedwait({signal.SIGCHLD}, remaining)
        except BaseException as error:
            cleanup_error = error
        finally:
            if process is not None:
                for pipe in (process.stdin, process.stdout, process.stderr):
                    try:
                        pipe.close()
                    except OSError as error:
                        cleanup_error = cleanup_error or error
            signal.signal(signal.SIGTERM, old_term)
            signal.pthread_sigmask(signal.SIG_SETMASK, old_mask)
            if libc.prctl(36, previous.value, 0, 0, 0) != 0:
                cleanup_error = Unqualified("subreaper restore failed")
            if EVIDENCE is not None:
                # Local build evidence only, never read back to decide qualification.
                try:
                    (EVIDENCE / (secrets.token_hex(16) + ".json")).write_text(json.dumps({
                        "argv": list(map(str, argv)), "pid": process.pid if process else None,
                        "input_executable_sha256": executable_hash,
                        "exit": process.returncode if process else None, "failure": failure,
                        "seconds": time.monotonic() - started,
                        "ASAN_OPTIONS": env.get("ASAN_OPTIONS"), "UBSAN_OPTIONS": env.get("UBSAN_OPTIONS"),
                        "stdout": bytes(streams["stdout"]).decode(errors="replace"),
                        "stderr": bytes(streams["stderr"]).decode(errors="replace")}))
                except OSError as error:
                    if primary is not None:
                        primary.add_note("evidence write failed: " + type(error).__name__)
                    else:
                        cleanup_error = error
        if cleanup_error is not None:
            if primary is not None:
                primary.add_note("cleanup failed: " + type(cleanup_error).__name__)
            else:
                raise Unqualified("cleanup failed: " + type(cleanup_error).__name__) from cleanup_error


def strict_environment():
    require(sys.platform == "linux", "UNQUALIFIED: Linux runtime required")
    require(os.environ.get("ASAN_OPTIONS") == "detect_leaks=1",
            "UNQUALIFIED: ASAN_OPTIONS must be exactly detect_leaks=1")
    require(os.environ.get("UBSAN_OPTIONS") == "halt_on_error=1",
            "UNQUALIFIED: UBSAN_OPTIONS must be exactly halt_on_error=1")
    for key in ("LSAN_OPTIONS", "LD_PRELOAD", "LD_LIBRARY_PATH", "ASAN_SYMBOLIZER_PATH"):
        require(not os.environ.get(key), "UNQUALIFIED: unsupported detector/loader override " + key)
    return {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C",
            "ASAN_OPTIONS": os.environ["ASAN_OPTIONS"], "UBSAN_OPTIONS": os.environ["UBSAN_OPTIONS"]}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inspect_child(process, binary, expected_hash, env):
    require(digest(f"/proc/{process.pid}/exe") == expected_hash == digest(binary),
            "child executable identity changed")
    maps = Path(f"/proc/{process.pid}/maps").read_text()
    require("libasan.so" in maps and "libubsan.so" in maps, "missing sanitizer runtime")
    require("libQt" not in maps, "Qt dependency in headless canary")
    symbols = execute(["/usr/bin/readelf", "--dyn-syms", "--wide", binary], env)
    require(symbols.code == 0, "ELF inspection failed")
    require(all(name in symbols.stdout for name in
                (b"__asan_init", b"__asan_report_store", b"__ubsan_handle_")),
            "missing actual ASan/UBSan instrumentation symbols")
    dwarf = execute(["/usr/bin/readelf", "--debug-dump=info", "--dwarf-depth=1", "--wide", binary], env)
    require(dwarf.code == 0, "DWARF inspection failed")
    source = Path(__file__).with_name("canary.cpp").resolve()
    units = dwarf.stdout.decode(errors="strict").split("DW_TAG_compile_unit")
    require(any(re.search(r"DW_AT_name[^\n]*: " + re.escape(str(source)) + "$", unit, re.M) and
                "-fsanitize=address,undefined" in unit and "-fno-omit-frame-pointer" in unit
                for unit in units), "wrong source/toolchain compilation unit")
    if EVIDENCE is not None:
        runtimes = sorted({line.split()[-1] for line in maps.splitlines()
                           if "libasan.so" in line or "libubsan.so" in line})
        (EVIDENCE / (secrets.token_hex(16) + "-identity.json")).write_text(json.dumps({
            "pid": process.pid, "binary_sha256": expected_hash,
            "source_sha256": digest(Path(__file__).with_name("canary.cpp")),
            "runtime_files": {path: digest(path) for path in runtimes}}))


def validate(result, nonce, size, source, inspected):
    require(result.code > 0, "child did not exit with an ordinary nonzero status (zero/signal)")
    require(inspected, "no live instrumented child receipt")
    expected = (f"mantis-lsan-v1 pid={result.pid} asan=1\n"
                f"allocation nonce={nonce} pid={result.pid} bytes={size}\n").encode()
    require(result.stdout == expected, "missing/stale/substituted allocation receipt")
    # Exactly one direct allocation and a complete, symbolized LSan report; no unrelated stderr.
    frame = rb"    #\d+ [^\n]+\n"
    report = (rb"\n?=+\n==" + str(result.pid).encode() +
              rb"==ERROR: LeakSanitizer: detected memory leaks\n\nDirect leak of " +
              str(size).encode() + rb" byte\(s\) in 1 object\(s\) allocated from:\n(" + frame +
              rb"){3,}\nSUMMARY: AddressSanitizer: " + str(size).encode() +
              rb" byte\(s\) leaked in 1 allocation\(s\)\.\n")
    require(re.fullmatch(report, result.stderr), "missing/malformed/unrelated LeakSanitizer report")
    frames = result.stderr.decode(errors="strict").splitlines()
    allocation_lines = [n for n, line in enumerate(source.read_text().splitlines(), 1)
                        if "std::malloc(bytes)" in line]
    require(len(allocation_lines) == 1, "ambiguous deliberate allocation source")
    require(any(re.match(r"    #0 .* in (?:__interceptor_)?malloc\b", line) for line in frames),
            "wrong allocation origin")
    require(any(re.match(r"    #1 .* in mantisHeadlessLeakCanary\b", line) and
                re.search(re.escape(str(source)) + ":" + str(allocation_lines[0]) +
                          r"(?::\d+)?(?: \(.*\))?$", line)
                for line in frames), "wrong deliberate allocation attribution")
    require(not re.search(rb"DEADLYSIGNAL|runtime error:|fatal error|ERROR: AddressSanitizer", result.stderr),
            "unrelated sanitizer failure")


def qualify(binary, env, *, timeout=DEADLINE):
    try:
        binary = Path(binary).resolve(strict=True)
    except OSError as error:
        raise Unqualified(type(error).__name__) from error
    source = Path(__file__).with_name("canary.cpp").resolve()
    expected_hash = digest(binary)
    nonce, size = secrets.token_hex(32), 12001 + secrets.randbelow(4096)
    inspected = False

    def handshake(process, stdout):
        nonlocal inspected
        if inspected or b"\n" not in stdout:
            return
        require(stdout == f"mantis-lsan-v1 pid={process.pid} asan=1\n".encode(),
                "invalid instrumented child handshake")
        inspect_child(process, binary, expected_hash, env)
        inspected = True
        process.stdin.write(f"{nonce} {size}\n".encode())
        process.stdin.flush()
        process.stdin.close()

    result = execute([binary], env, timeout=timeout, output=handshake)
    try:
        validate(result, nonce, size, source, inspected)
    except Unqualified as error:
        raise Unqualified(str(error) + " " + json.dumps({
            "exit": result.code, "stdout": result.stdout[:4096].decode(errors="replace"),
            "stderr": result.stderr[:4096].decode(errors="replace")})) from error
    return result, nonce, size, source

"""CLI and offline boundary: mock never connects, even with usable credentials."""
from pathlib import Path
import os
import socket
import subprocess
import sys
import struct
import zlib

binary = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
env.pop("MANTIS_TOKEN", None)
errors = ("ReferenceError", "TypeError", "Binding loop", "failed to load", "Cannot assign", "is not a type", "Unable to assign")
def run(*args, success=True, exit_code=None):
    result = subprocess.run([str(binary), *args], env=env, text=True, capture_output=True, timeout=12)
    (output / "cli-last.log").write_text(result.stdout + result.stderr)
    assert result.returncode == (exit_code if exit_code is not None else 0 if success else 2), (args, result.stdout, result.stderr)
    assert not any(error in result.stderr for error in errors), result.stderr
    return result

assert "--ui-mode" in run("--help").stdout
for args in (("--ui-mode=invalid",), ("--workspace=missing",), ("--ui-mode",),
             ("--quit-after=oops",), ("--quit-after=0",), ("--window-size=100x100",),
             ("--window-size=1536xhuge",), ("--ui-mode=mock", "--acceptance-export=/tmp/forbidden.ply"),
             ("--unknown",), ("--screenshot=",)):
    run(*args, success=False)
# No token / no daemon, including a short quit timeout with a screenshot.
picture = output / "cli-mock-home.png"
run("--ui-mode=mock", "--workspace=home", "--quit-after=600", "--screenshot", str(picture))
assert picture.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
# Pathological screenshot deadlines are rejected before loading QML, while ordinary
# rapid exits remain valid. At the accepted minimum, readback must contain UI content.
for timeout in (1, 99):
    run("--ui-mode=mock", f"--quit-after={timeout}", "--screenshot", str(output / "rejected.png"), success=False)
run("--ui-mode=mock", "--quit-after=1")
def assert_rendered(path, dimensions):
    data = path.read_bytes()
    assert data.startswith(b"\x89PNG\r\n\x1a\n")
    assert struct.unpack(">II", data[16:24]) == dimensions
    offset, compressed = 8, bytearray()
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset+4])[0]
        if data[offset+4:offset+8] == b"IDAT":
            compressed.extend(data[offset+8:offset+8+length])
        offset += length + 12
    pixels = zlib.decompress(compressed)
    assert len(set(pixels)) > 16, "Screenshot lacks rendered content"
for iteration in range(3):
    rapid = output / f"rapid-{iteration}.png"
    run("--ui-mode=mock", "--window-size=1080x720", "--quit-after=100", "--screenshot", str(rapid))
    assert_rendered(rapid, (1080, 720))
large = output / "maximum-viewport.png"
run("--ui-mode=mock", "--window-size=7680x4320", "--quit-after=1500", "--screenshot", str(large))
assert_rendered(large, (7680, 4320))
failed = run("--ui-mode=mock", "--quit-after=100", "--screenshot", str(output / "missing-directory" / "failed.png"), exit_code=1)
assert "save failed" in failed.stderr, failed.stderr
assert not (output / "rejected.png").exists()
# Mock does not parse unrelated runtime endpoint configuration either.
env["MANTIS_PORT"] = "not-a-runtime-port"
run("--ui-mode=mock", "--quit-after=400")
env.pop("MANTIS_PORT")
# Trap any accidental network access while exercising every initial route.
with socket.socket() as trap:
    trap.bind(("127.0.0.1", 0))
    trap.listen(32)
    trap.settimeout(0.1)
    env.update(MANTIS_PORT=str(trap.getsockname()[1]), MANTIS_TOKEN="ui-m0-network-trap-token")
    for route in ("home", "scan", "process", "inspect", "reverse", "automate", "projects", "devices", "plugins", "settings", "calibration", "acquisition"):
        run("--ui-mode", "mock", "--workspace", route, "--quit-after", "650")
    try:
        connection, _ = trap.accept()
    except TimeoutError:
        pass
    else:
        connection.close()
        raise AssertionError("Mock attempted a runtime connection")
# Live/hybrid Scan must use the new foundation even without a reachable daemon;
# the separate Classic route must still start. Binding without listening makes
# connection refusal deterministic without racing another process for a port.
with socket.socket() as unavailable:
    unavailable.bind(("127.0.0.1", 0))
    env.update(MANTIS_PORT=str(unavailable.getsockname()[1]), MANTIS_TOKEN="ui-m3b-unreachable-token")
    for mode, route in (("live", "scan"), ("hybrid", "scan"), ("live", "acquisition")):
        picture = output / f"unreachable-{mode}-{route}.png"
        run(f"--ui-mode={mode}", f"--workspace={route}", "--window-size=1080x720",
            "--quit-after=650", "--screenshot", str(picture))
        assert_rendered(picture, (1080, 720))
print("PASS: CLI validation, rendered deadlines, twelve offline routes, unreachable live/hybrid Scan and Classic Acquisition")

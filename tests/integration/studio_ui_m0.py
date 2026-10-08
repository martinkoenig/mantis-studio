"""CLI and offline boundary: mock never connects, even with usable credentials."""
from pathlib import Path
import os
import socket
import subprocess
import sys

binary = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
env.pop("MANTIS_TOKEN", None)
errors = ("ReferenceError", "TypeError", "Binding loop", "failed to load", "Cannot assign", "is not a type", "Unable to assign")
def run(*args, success=True):
    result = subprocess.run([str(binary), *args], env=env, text=True, capture_output=True, timeout=12)
    (output / "cli-last.log").write_text(result.stdout + result.stderr)
    assert result.returncode == (0 if success else 2), (args, result.stdout, result.stderr)
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
print("PASS: CLI validation, short screenshot timeout, token-free offline startup, twelve initial routes without network access")

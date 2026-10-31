"""Verify real acceptance failure evidence survives CTest/runner-file cleanup."""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile

build = Path(sys.argv[1]).resolve()
source = Path(__file__).with_name("acceptance.py")
prefix = "ACCEPTANCE_FAILURE_EVIDENCE "


def cmake_argument(value):
    # Literal bracket arguments preserve paths without executing shell text.
    assert "]=]" not in str(value)
    return "[=[" + str(value) + "]=]"


with tempfile.TemporaryDirectory(prefix="acceptance-output-runner-") as directory:
    runner = Path(directory)
    command = " ".join(cmake_argument(value) for value in
                       (sys.executable, source, build, "--fail-for-diagnostics"))
    (runner / "CTestTestfile.cmake").write_text(
        f"add_test(injected-acceptance {command})\n"
        "set_tests_properties(injected-acceptance PROPERTIES TIMEOUT 90)\n")
    result = subprocess.run(["ctest", "--test-dir", str(runner), "--parallel", "1",
                             "--output-on-failure"], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    output = result.stdout
    diagnostics = build / "test-diagnostics"
    diagnostics.mkdir(exist_ok=True)
    (diagnostics / "acceptance-failure-output.log").write_text(
        re.sub(r"acceptance-[0-9a-f]{32}", "[REDACTED]", output))
    assert result.returncode == 8, f"Expected one deliberately failed CTest, exit={result.returncode}"
    assert "Injected acceptance diagnostics export failure" in output

# The failed CTest's files are gone; only the externally captured job output
# is used below. This is the channel Actions retains for every configuration.
assert not runner.exists()
records = [json.loads(line.strip()[len(prefix):]) for line in output.splitlines()
           if line.strip().startswith(prefix)]
assert {row["file"] for row in records} == {"daemon.log", "studio.log", "capture-state.json"}, \
    "Acceptance diagnostics did not reach the failed CTest output"
by_name = {row["file"]: row for row in records}
assert "tail" in by_name["daemon.log"]
state_record = by_name["capture-state.json"]
assert state_record["omitted_prefix_bytes"] == 0, "Probe capture-state unexpectedly exceeds output bound"
state = json.loads(state_record["tail"])
assert state["observations"]
assert any(c["id"] and c["raw_artifact"] and c["active"]
           for c in state["observations"][-1])
if (build / "bin/mantis-studio").exists():
    assert "STUDIO_ACCEPTANCE_READY" in by_name["studio.log"]["tail"]
else:
    assert by_name["studio.log"]["read_error"] == "FileNotFoundError"
for row in records:
    assert not re.search(r"acceptance-[0-9a-f]{32}", row.get("tail", "")), "Credential escaped redaction"
print("PASS: bounded sanitized acceptance log/state evidence reached failed CTest output "
      "and remained readable after its runner directory was removed")

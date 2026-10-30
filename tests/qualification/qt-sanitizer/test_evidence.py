"""Exercise fail-closed CLI behavior against an actual completed qualification."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from test_harness import qualification


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    original = args.evidence.resolve()
    qualification.verify_complete(original)
    cases = []

    def verify(path, expected, name):
        result = subprocess.run([sys.executable, str(qualification.__file__), "--verify-evidence", str(path)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        cases.append({"case": name, "actual_exit": result.returncode, "output": result.stdout,
                      "expected_success": expected})
        if (result.returncode == 0) != expected:
            raise AssertionError(f"{name}: unexpected exit {result.returncode}: {result.stdout}")

    try:
        verify(original, True, "complete evidence accepted")
        with tempfile.TemporaryDirectory(prefix="qt-sanitizer-regressions-") as temporary:
            root = Path(temporary)
            # These execute real qtpaths/prerequisite resolution, not mocked version checks.
            for name, prefix in (("missing prefix", root / "unavailable"), ("wrong Qt version", Path("/usr"))):
                output = root / name.replace(" ", "-")
                result = subprocess.run([sys.executable, str(qualification.__file__), "--qt-prefix", str(prefix),
                                         "--output-dir", str(output)], stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True)
                summary = json.loads((output / "summary.json").read_text())
                cases.append({"case": name, "actual_exit": result.returncode, "status": summary["status"],
                              "reason": summary.get("reason")})
                assert result.returncode != 0 and summary["status"] == "BLOCKED", cases[-1]
                assert not summary["probe_runs"], cases[-1]

            def nonzero_probe(path):
                summary = json.loads((path / "summary.json").read_text())
                record = next(r for r in summary["commands"] if r["name"] == "probe-1")
                record["returncode"] = 7
                qualification.write_json(path / "probe-1.exit.json", record)
                qualification.write_json(path / "summary.json", summary)

            def replace_log(path, name, before, after):
                log = path / name
                text = log.read_text()
                assert before in text, (name, before)
                log.write_text(text.replace(before, after))

            mutations = [
                ("missing raw probe log", lambda p: (p / "probe-1.log").unlink()),
                ("missing CMake metadata", lambda p: (p / "qualification.json").unlink()),
                ("empty compilation records", lambda p: (p / "compile_commands.json").write_text('[]\n')),
                ("nonzero real probe", nonzero_probe),
                ("missing image workload", lambda p: replace_log(p, "probe-1.log", "QImageReader size", "absent")),
                ("missing rendered frame", lambda p: replace_log(p, "probe-1.log", "Frame rendered", "absent")),
                ("plugin version mismatch", lambda p: replace_log(p, "probe-1.log", '"version": 395520',
                                                                 '"version": 394240')),
                ("system Qt library mismatch", lambda p: replace_log(p, "qt-leak-probe-ldd.log",
                    str(Path(json.loads((p / "summary.json").read_text())["qt_prefix"]) / "lib/libQt6Core.so.6"),
                    "/usr/lib/x86_64-linux-gnu/libQt6Core.so.6")),
                ("missing intentional leak diagnostic", lambda p: replace_log(p, "intentional-leak.log",
                    "ERROR: LeakSanitizer: detected memory leaks", "missing diagnostic")),
            ]
            for index, (name, mutate) in enumerate(mutations):
                copy = root / f"case-{index}"
                shutil.copytree(original, copy, ignore=shutil.ignore_patterns("build"))
                mutate(copy)
                # Recompute manifest so rejection must come from the evidence policy,
                # rather than merely noticing that a test changed a file.
                qualification.finalize(copy, json.loads((copy / "summary.json").read_text()))
                verify(copy, False, name)
        status = "PASS"
    except Exception:
        status = "FAIL"
        raise
    finally:
        qualification.write_json(args.report, {"status": status, "cases": cases})
    print(f"PASS: {len(cases)} real-evidence/CLI regression cases")


if __name__ == "__main__":
    main()

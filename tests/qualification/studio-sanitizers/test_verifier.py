#!/usr/bin/env python3
"""Evidence-policy regressions; synthetic data is never qualification evidence."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import verify_studio_sanitizers as v


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sha = "1" * 40
        self.prefix = "/qualified/qt"
        self.version = "6.9.2"
        self.source = "/source"
        self.hash = "a" * 64
        self.counter = 0
        self.sources = {n: {"sha256": self.hash, "executable": False} for n in (
            "tests/unit/studio_scan.cpp", "tools/qualify_studio_sanitizers.py", "tools/verify_studio_sanitizers.py")}
        self.write("sources.json", self.sources)
        self.write("restored-sources.json", self.sources)
        self.write("qt-version.txt", self.version)
        self.write("symbols.txt", "U __asan_init\nU __ubsan_handle_type_mismatch_v1\n")
        self.write("sections.txt", ".debug_info\n")
        self.qtfiles = {}
        for module in ("Core", "Gui", "Qml", "Quick"):
            path = self.prefix + "/lib/libQt6" + module + ".so.6"
            self.qtfiles[path] = {"resolved": path + ".9.2", "sha256": self.hash}
        self.plugin = self.prefix + "/plugins/platforms/libqoffscreen.so"
        self.qtfiles[self.plugin] = {"resolved": self.plugin, "sha256": self.hash}
        self.loader = "\n".join("123: calling init: " + p for p in self.qtfiles)
        self.loader += (f'\nFound metadata in lib {self.plugin}, metadata=\n{{\n"version": 395520\n}}\n'
                        f'qt.core.library: "{self.plugin}" loaded library\n'
                        f'addImportPath: "{self.prefix}/qml/"\nLoading backend software\n'
                        "Frame rendered with 'software' renderloop\nQT_CONTROL_FRAME runtime Qt 6.9.2\n")
        self.linkage = "libasan.so.8 => /runtime/libasan.so.8\nlibubsan.so.1 => /runtime/libubsan.so.1\n"
        self.linkage += "\n".join(Path(p).name + " => " + p for p in self.qtfiles if "libQt6" in p)
        self.write("linkage.txt", self.linkage)
        self.write("headless-linkage.txt", "libasan.so.8 => /runtime/libasan.so.8\nlibubsan.so.1 => /runtime/libubsan.so.1\n")
        self.q = {"schema": 1, "source_sha": self.sha, "source_root": self.source, "head_tree": "2" * 40,
                  "compiler": "GNU 13.3.0", "linker": "GNU ld", "opencv": "4.6.0",
                  "tools": {n: self.hash for n in self.sources if n.startswith("tools/")},
                  "qt": {"prefix": self.prefix, "version": self.version, "version_receipt": "qt-version.txt"},
                  "configurations": {}, "gates": {}, "qt_control_files": self.qtfiles,
                  "qt_controls": [self.receipt(["qt-leak-probe"], self.loader) for _ in range(3)],
                  "negative_controls": {
                      "NC1": self.receipt(["leak-detector-selftest"], "ERROR: LeakSanitizer: detected memory leaks\nleakForDetectorControl\n", 1),
                      "NC2": self.receipt(["mantis-studio-scan-tests"], "ERROR: AddressSanitizer: heap-use-after-free\n"
                          "MeasurementView::~MeasurementView apps/studio/preview.hpp\nmain tests/unit/studio_scan.cpp\n", 1)},
                  "fault_restoration": {"before": self.hash, "after": self.hash, "mutant_source_sha256": "b" * 64,
                      "mutant_binary_sha256": "b" * 64, "build": self.receipt(["cmake"], "restored build\n")},
                  "regressions": self.receipt([sys.executable], "policy regressions passed\n"),
                  "workload": self.receipt(["mantis-studio"], self.loader), "workload_files": self.qtfiles,
                  "screenshot": "studio.png"}
        self.q["test_failure_control"] = self.receipt(["ctest", "--parallel", "1"],
            "Invalid --ui-mode\n0% tests passed, 1 tests failed out of 1\n", 8)
        self.alter(self.q["negative_controls"]["NC2"], lambda d: d.update(executable_sha256="b" * 64))
        self.alter(self.q["workload"], lambda d: d.update(executable_sha256=self.hash))
        self.q["evidence_controls"] = {n: self.receipt(["verify_studio_sanitizers.py"],
            "Invalid qualification evidence: " + diagnostic + "\n", 1) for n, diagnostic in {
                "NC3": "Source SHA", "NC4": "Qt runtime version/prefix",
                "NC5": "Uninstrumented authored source", "NC6": "Incomplete/unexpected CTest results"}.items()}
        (self.root / "studio.png").write_bytes(b"\x89PNG\r\n\x1a\nfixture")
        self.studio = v.FOCUSED | v.REPEATED | {"studio-additional"}
        self.common = {"boundaries", "data"}
        required_sources = {"apps/studio/" + n + ".cpp" for n in (
            "bridge", "calibration_controller", "live_views", "scan_model", "devices_model", "projects_model", "home_model")}
        required_sources |= {"src/render/render.cpp", "tests/unit/studio_calibration.cpp", "tests/unit/studio_scan.cpp"}
        self.write("compile.json", [{"file": self.source + "/" + n,
                   "command": "g++ -g -fsanitize=address,undefined -fno-omit-frame-pointer -c " + n}
                  for n in sorted(required_sources)])
        for name in ("debug", "headless", "sanitizers", "studio-sanitizers"):
            studio, sanitized = name in {"debug", "studio-sanitizers"}, name in {"sanitizers", "studio-sanitizers"}
            build = "/build/" + name
            names = self.common | (self.studio if studio else set())
            registration = {"tests": [{"name": n, "command": [build + "/bin/" + n], "properties": [
                {"name": "TIMEOUT", "value": 60}, {"name": "WORKING_DIRECTORY", "value": build}]} for n in sorted(names)]}
            self.write(name + "-inventory.json", registration)
            self.write(name + "-initial.json", registration)
            cache = ("CMAKE_BUILD_TYPE:STRING=Debug\nPython3_EXECUTABLE:FILEPATH=/usr/bin/python3\n"
                     f"CMAKE_HOME_DIRECTORY:INTERNAL={self.source}\n"
                     f"MANTIS_BUILD_STUDIO:BOOL={'ON' if studio else 'OFF'}\n"
                     f"MANTIS_SANITIZE:BOOL={'ON' if sanitized else 'OFF'}\n")
            cache += "".join(f"Qt6{m}_DIR:PATH={self.prefix}/lib/cmake/Qt6{m}\n" for m in (
                "Core", "Gui", "Quick", "Qml", "QuickControls2", "Concurrent", "Test"))
            self.write(name + "-cache.txt", cache)
            binaries = v.GUI_BINARIES if studio else {"mantisd", "mantis-cli"}
            links = "\n".join("g++ -g -fsanitize=address,undefined -o bin/" + n + " object.o" for n in binaries)
            self.write(name + "-links.txt", links)
            config = {"build": build, "studio": studio, "sanitized": sanitized,
                      "configure": self.receipt(["cmake"], "Configured\n"), "build_receipt": self.receipt(["cmake"], "Built\n"),
                      "cache": name + "-cache.txt", "inventory": name + "-inventory.json", "initial_inventory": name + "-initial.json",
                      "compile_commands": "compile.json", "link_commands": name + "-links.txt", "runtime_files": self.qtfiles,
                      "binaries": [dict(self.binary(n), output="bin/" + n,
                                   linkage="linkage.txt" if studio else "headless-linkage.txt") for n in binaries],
                      "libraries": [self.binary("library.a")]}
            plugin_name = sorted(binaries)[0]
            self.write(name + "-plugins.json", {"fixture.json": {"sha256": self.hash,
                       "content": {"library": plugin_name}}})
            config["plugin_manifests"] = name + "-plugins.json"
            self.q["configurations"][name] = config
        for name, tests in (("focused", v.FOCUSED), ("broader", self.studio - v.FOCUSED),
                            ("normal-studio", self.common | self.studio), ("normal-headless", self.common),
                            ("headless-sanitizers", self.common)):
            self.q["gates"][name] = self.gate(tests, full=name in {"normal-studio", "normal-headless", "headless-sanitizers"})
        self.q["gates"]["ui-repeat"] = [self.gate(v.REPEATED) for _ in range(3)]

    def binary(self, name):
        return {"name": name, "sha256": self.hash, "symbols": "symbols.txt", "sections": "sections.txt"}

    def write(self, name, value):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(v.encoded(value) if not isinstance(value, str) else value.encode())

    def receipt(self, command, log, code=0):
        self.counter += 1
        name = f"command-{self.counter}"
        self.write(name + ".log", log)
        self.write(name + ".json", {"command": command, "exit": code, "log": name + ".log",
                   "environment": v.STRICT, "elapsed_seconds": 0.1, "max_rss_kib": 42})
        return name + ".json"

    def gate(self, names, full=False):
        names = sorted(names)
        log = "\n".join(f"{i}/{len(names)} Test #{i}: {n} .... Passed" for i, n in enumerate(names, 1))
        log += f"\n100% tests passed, 0 tests failed out of {len(names)}\n"
        command = ["ctest", "--parallel", "1", "--no-tests=error"]
        if not full:
            command += ["-R", v.selection_pattern(names)]
        receipt = self.receipt(command, log)
        junit = receipt + ".xml"
        self.write(junit, "<testsuite>" + "".join(f'<testcase name="{n}" status="run"/>' for n in names) + "</testsuite>")
        return {"receipt": receipt, "junit": junit}

    def sealed(self):
        self.write("qualification.json", self.q)
        files = {str(p.relative_to(self.root)): v.digest(p) for p in self.root.rglob("*")
                 if p.is_file() and p.name != "manifest.json"}
        self.write("manifest.json", {"files": files})
        return v.digest(self.root / "manifest.json")

    def verify(self):
        return v.verify(self.root, self.sealed(), self.sha, v.digest(self.root / "sources.json"))

    def alter(self, name, change):
        data = json.loads((self.root / name).read_text())
        change(data)
        self.write(name, data)

    def test_complete_coherent_package(self):
        self.assertEqual(self.verify()["studio_tests"], len(self.studio))

    def test_selector_uses_actual_ctest_regex_dialect(self):
        folder = self.root / "selector"
        folder.mkdir()
        names = {"studio-scan-qml", "studio-devices-dpi-1.5", "studio-projects-scroll"}
        (folder / "CTestTestfile.cmake").write_text("".join(
            f'add_test("{name}" "/usr/bin/true")\n' for name in sorted(names)))
        result = subprocess.run(["ctest", "--test-dir", str(folder), "--show-only=json-v1",
                                 "-R", v.selection_pattern(names)], capture_output=True, text=True, check=True)
        self.assertEqual({t["name"] for t in json.loads(result.stdout)["tests"]}, names)

    def test_NC6_actual_ctest_empty_selection_fails(self):
        folder = self.root / "empty-selector"
        folder.mkdir()
        (folder / "CTestTestfile.cmake").write_text('add_test("registered" "/usr/bin/true")\n')
        result = subprocess.run(["ctest", "--test-dir", str(folder), "-R", "absent", "--no-tests=error"],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No tests were found", result.stdout + result.stderr)

    def test_missing_real_evidence_controls_rejected(self):
        self.q.pop("evidence_controls")
        with self.assertRaisesRegex(v.InvalidEvidence, "Missing completed evidence controls"):
            self.verify()

    def test_NC2_rejects_unrelated_or_nonprimary_fault(self):
        for text in ("ERROR: AddressSanitizer: heap-use-after-free\nUnrelatedQObject::~UnrelatedQObject\n",
                     "AddressSanitizer:DEADLYSIGNAL\nMeasurementView::~MeasurementView\n",
                     "MeasurementView::setImage\n"):
            with self.subTest(text=text), self.assertRaises(v.InvalidEvidence):
                v.check_application_fault(text)

    def test_NC2_fault_binary_cannot_be_reused(self):
        self.q["fault_restoration"]["mutant_binary_sha256"] = self.hash
        self.alter(self.q["negative_controls"]["NC2"], lambda d: d.update(executable_sha256=self.hash))
        with self.assertRaisesRegex(v.InvalidEvidence, "Faulted binary"):
            self.verify()

    def test_required_sanitizer_modes_cannot_be_relabeled(self):
        for name in ("studio-sanitizers", "sanitizers"):
            self.q["configurations"][name]["sanitized"] = False
            path = name + "-cache.txt"
            self.write(path, (self.root / path).read_text().replace(
                "MANTIS_SANITIZE:BOOL=ON", "MANTIS_SANITIZE:BOOL=OFF"))
        self.write("compile.json", [])
        with self.assertRaisesRegex(v.InvalidEvidence, "Required configuration mode mismatch"):
            self.verify()

    def test_NC3_source_sha(self):
        self.q["source_sha"] = "f" * 40
        with self.assertRaisesRegex(v.InvalidEvidence, "Source SHA"):
            self.verify()

    def test_NC3_incompatible_registration(self):
        self.alter("studio-sanitizers-inventory.json", lambda d: d["tests"].pop())
        with self.assertRaisesRegex(v.InvalidEvidence, "registration"):
            self.verify()

    def test_NC4_runtime_qt_version(self):
        self.q["workload_files"][self.prefix + "/lib/libQt6Core.so.6"]["resolved"] = "/system/libQt6Core.so.6.4.2"
        with self.assertRaisesRegex(v.InvalidEvidence, "Qt runtime"):
            self.verify()

    def test_NC4_query_version(self):
        self.write("qt-version.txt", "6.4.2\n")
        with self.assertRaisesRegex(v.InvalidEvidence, "Qt version"):
            self.verify()

    def test_cpp_registered_qml_search_is_not_missing_dso(self):
        log = json.loads((self.root / self.q["workload"]).read_text())["log"]
        self.write(log, self.loader + "qt.qml.import: locateLocalQmldir: Mantis.Studio module's qmldir file not found\n"
                   "qt.qml.import: locateLocalQmldir: Mantis.Render module's qmldir file not found\n")
        self.assertEqual(self.verify()["result"], "QUALIFIED")

    def test_unresolved_dso_still_rejected(self):
        self.write("linkage.txt", self.linkage + "libmissing.so => not found\n")
        with self.assertRaisesRegex(v.InvalidEvidence, "runtime linkage|Unresolved runtime dependency"):
            self.verify()

    def test_NC4_raw_loader_path_cannot_be_hidden_by_canonical_claim(self):
        original = self.prefix + "/lib/libQt6Core.so.6"
        outside = "/system/libQt6Core.so.6"
        self.q["workload_files"][outside] = dict(self.qtfiles[original])
        path = json.loads((self.root / self.q["workload"]).read_text())["log"]
        self.write(path, self.loader.replace(original, outside))
        with self.assertRaisesRegex(v.InvalidEvidence, "Qt runtime version/prefix"):
            self.verify()

    def test_qualified_relative_rpath_spelling_is_normalized(self):
        original = self.prefix + "/lib/libQt6Core.so.6"
        relative = self.prefix + "/qml/QtQuick/Controls/../../../lib/libQt6Core.so.6"
        self.q["workload_files"][relative] = dict(self.qtfiles[original])
        path = json.loads((self.root / self.q["workload"]).read_text())["log"]
        self.write(path, self.loader.replace(original, relative))
        self.assertEqual(self.verify()["result"], "QUALIFIED")

    def test_build_must_use_the_recorded_source(self):
        name = "debug-cache.txt"
        self.write(name, (self.root / name).read_text().replace("=/source", "=/old/source"))
        with self.assertRaisesRegex(v.InvalidEvidence, "Wrong configured source/mode: CMAKE_HOME_DIRECTORY"):
            self.verify()

    def test_normal_studio_build_cannot_be_headless(self):
        name = "debug-cache.txt"
        self.write(name, (self.root / name).read_text().replace("MANTIS_BUILD_STUDIO:BOOL=ON", "MANTIS_BUILD_STUDIO:BOOL=OFF"))
        with self.assertRaisesRegex(v.InvalidEvidence, "Wrong configured source/mode: MANTIS_BUILD_STUDIO"):
            self.verify()

    def test_workload_must_execute_the_instrumented_studio_binary(self):
        self.alter(self.q["workload"], lambda d: d.update(executable_sha256="f" * 64))
        with self.assertRaisesRegex(v.InvalidEvidence, "Rendered Studio binary identity mismatch"):
            self.verify()

    def test_rendered_image_must_be_manifested(self):
        self.sealed()
        self.alter("manifest.json", lambda d: d["files"].pop("studio.png"))
        with self.assertRaisesRegex(v.InvalidEvidence, "Unmanifested Studio screenshot"):
            v.verify(self.root, v.digest(self.root / "manifest.json"), self.sha, v.digest(self.root / "sources.json"))

    def test_NC4_plugin_metadata(self):
        r = self.q["workload"]
        log = json.loads((self.root / r).read_text())["log"]
        self.write(log, self.loader.replace("395520", "394240"))
        with self.assertRaisesRegex(v.InvalidEvidence, "plugin version"):
            self.verify()

    def test_NC5_any_authored_unit_uninstrumented(self):
        self.alter("compile.json", lambda d: d[0].update(command="g++ -g -c bridge.cpp"))
        with self.assertRaisesRegex(v.InvalidEvidence, "Uninstrumented authored source"):
            self.verify()

    def test_NC5_link_flag_absent(self):
        path = "studio-sanitizers-links.txt"
        self.write(path, (self.root / path).read_text().replace("-fsanitize=address,undefined", ""))
        with self.assertRaisesRegex(v.InvalidEvidence, "link flags"):
            self.verify()

    def test_NC5_missing_runtime(self):
        self.write("linkage.txt", self.linkage.replace("libasan.so", "missing.so"))
        with self.assertRaisesRegex(v.InvalidEvidence, "runtime linkage"):
            self.verify()

    def test_NC5_missing_symbols_or_debug_information(self):
        for name in ("symbols.txt", "sections.txt"):
            with self.subTest(name=name):
                before = (self.root / name).read_bytes()
                self.write(name, "missing\n")
                with self.assertRaises(v.InvalidEvidence):
                    self.verify()
                (self.root / name).write_bytes(before)

    def test_NC6_nonzero_retains_primary_diagnostic(self):
        gate = self.q["gates"]["focused"]
        self.alter(gate["receipt"], lambda d: d.update(exit=8))
        data = json.loads((self.root / gate["receipt"]).read_text())
        self.write(data["log"], "primary failure: expected project generation differs\n")
        with self.assertRaisesRegex(v.InvalidEvidence, "Unexpected command exit"):
            self.verify()
        self.assertIn("expected project generation differs", (self.root / data["log"]).read_text())

    def test_NC6_incomplete_xml(self):
        self.write(self.q["gates"]["focused"]["junit"], "<testsuite/>")
        with self.assertRaisesRegex(v.InvalidEvidence, "Incomplete"):
            self.verify()

    def test_NC6_malformed_or_missing_xml(self):
        path = self.root / self.q["gates"]["focused"]["junit"]
        self.write(str(path.relative_to(self.root)), "<broken")
        with self.assertRaises(v.ET.ParseError):
            self.verify()
        path.unlink()
        with self.assertRaises((OSError, v.InvalidEvidence)):
            self.verify()

    def test_NC6_failed_skipped_duplicate_results(self):
        path = self.q["gates"]["focused"]["junit"]
        original = (self.root / path).read_text()
        for replacement in ("<failure>primary assertion</failure></testcase>", "<skipped/></testcase>"):
            self.write(path, original.replace("/>", ">" + replacement, 1))
            with self.assertRaisesRegex(v.InvalidEvidence, "Failed/skipped"):
                self.verify()
        self.write(path, original.replace("</testsuite>", '<testcase name="studio-scan-qml" status="run"/></testsuite>'))
        with self.assertRaisesRegex(v.InvalidEvidence, "Incomplete"):
            self.verify()

    def test_registration_timeout_or_environment_cannot_change(self):
        self.alter("debug-inventory.json", lambda d: d["tests"][0]["properties"][0].update(value=600))
        with self.assertRaisesRegex(v.InvalidEvidence, "registration/timeout/property"):
            self.verify()

    def test_runtime_policy_cannot_be_weakened(self):
        for key, value in (("ASAN_OPTIONS", "detect_leaks=0"), ("LSAN_OPTIONS", "suppressions=hidden"),
                           ("LD_LIBRARY_PATH", "/system/qt"), ("CTEST_PARALLEL_LEVEL", "8")):
            with self.subTest(key=key), self.assertRaises(v.InvalidEvidence):
                v.check_environment(dict(v.STRICT, **{key: value}))

    def test_filtered_full_or_wrong_focused_command(self):
        for name in ("normal-studio", "focused"):
            r = self.q["gates"][name]["receipt"]
            before = (self.root / r).read_bytes()
            self.alter(r, lambda d: d["command"].extend(["-E", "unwanted"]))
            with self.assertRaisesRegex(v.InvalidEvidence, "Filtered"):
                self.verify()
            (self.root / r).write_bytes(before)

    def test_fault_hash_must_be_restored(self):
        self.q["fault_restoration"]["after"] = "b" * 64
        with self.assertRaisesRegex(v.InvalidEvidence, "Fault not restored"):
            self.verify()

    def test_sanitizer_diagnostic_rejects_zero_exit(self):
        r = self.q["workload"]
        log = json.loads((self.root / r).read_text())["log"]
        self.write(log, self.loader + "\nLeakSanitizer has encountered a fatal error.\n")
        with self.assertRaisesRegex(v.InvalidEvidence, "Sanitizer finding"):
            self.verify()

    def test_manifest_checksum_traversal_and_missing_file(self):
        anchor = self.sealed()
        self.write("symbols.txt", "tampered")
        with self.assertRaisesRegex(v.InvalidEvidence, "checksum"):
            v.Evidence(self.root, anchor)
        self.write("manifest.json", {"files": {"../escape": self.hash}})
        with self.assertRaisesRegex(v.InvalidEvidence, "Unsafe"):
            v.Evidence(self.root, v.digest(self.root / "manifest.json"))
        self.write("manifest.json", {"files": {"absent.log": self.hash}})
        with self.assertRaisesRegex(v.InvalidEvidence, "Missing/unsafe"):
            v.Evidence(self.root, v.digest(self.root / "manifest.json"))

    def test_cli_malformed_evidence_fails_nonzero(self):
        anchor = self.sealed()
        result = subprocess.run([sys.executable, "-B", str(Path(v.__file__)), str(self.root),
                                 "--expected-manifest-sha256", anchor, "--expected-source-sha", "f" * 40,
                                 "--expected-source-manifest-sha256", v.digest(self.root / "sources.json")],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid qualification evidence: Source SHA", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)

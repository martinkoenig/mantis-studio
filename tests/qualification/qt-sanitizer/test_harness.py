"""Parser/policy regressions; run directly, without building Mantis or Qt."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[3] / "tools/qualify_qt_sanitizer.py"
spec = importlib.util.spec_from_file_location("qualification", TOOL)
qualification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qualification)


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.prefix = Path(self.temporary.name) / "qt"
        self.prefix.mkdir()
        self.version = "6.9.2"
        self.libraries = []
        for module in ("Core", "Gui", "Qml", "Quick"):
            path = self.prefix / f"libQt6{module}.so.6.9.2"
            path.touch()
            self.libraries.append(str(path))
        self.plugins = [str(self.prefix / name) for name in ("libqoffscreen.so", "libquickwindowplugin.so")]
        self.log = "\n".join(f" 123: calling init: {path}" for path in self.libraries) + "\n"
        for path in self.plugins:
            self.log += f'Found metadata in lib {path}, metadata=\n{{\n"version": 395520\n}}\n'
            self.log += f'qt.core.library: "{path}" loaded library\n'
        self.log += (f'addImportPath: "{self.prefix}/qml"\nLoading backend software\n'
                     'QImageReader size QSize(32, 32)\n'
                     'softwarecontext.renderer: render QRegion(0,0 320x240)\n'
                     "Frame rendered with 'software' renderloop\n")
        self.leak = ("Intentional LSan control; runtime Qt 6.9.2\n"
                     "ERROR: LeakSanitizer: detected memory leaks\nleakForDetectorControl\n"
                     "SUMMARY: AddressSanitizer: 123 byte(s) leaked in 1 allocation(s).\n")

    def positive(self, text=None, code=0):
        return qualification.check_positive(code, self.log if text is None else text, self.prefix, self.version)

    def test_complete_render_evidence_accepted(self):
        evidence = self.positive()
        self.assertEqual(evidence["image_size"], [32, 32])

    def test_nonzero_real_probe_rejected_even_with_render_evidence(self):
        with self.assertRaises(qualification.InvalidEvidence):
            self.positive(code=1)

    def test_sanitizer_diagnostic_rejected_even_with_zero_exit(self):
        for diagnostic in ("ERROR: LeakSanitizer", "runtime error:", "AddressSanitizer:DEADLYSIGNAL"):
            with self.subTest(diagnostic=diagnostic), self.assertRaises(qualification.InvalidEvidence):
                self.positive(self.log + diagnostic)

    def test_each_required_workload_signal_is_required(self):
        for line in ('Loading backend software', 'QImageReader size QSize(32, 32)',
                     'softwarecontext.renderer: render QRegion(0,0 320x240)',
                     "Frame rendered with 'software' renderloop"):
            with self.subTest(line=line), self.assertRaises(qualification.InvalidEvidence):
                self.positive(self.log.replace(line, ''))

    def test_loaded_plugin_outside_prefix_rejected(self):
        with self.assertRaises(qualification.InvalidEvidence):
            self.positive(self.log.replace(self.plugins[0], '/usr/lib/qt6/plugins/platforms/libqoffscreen.so'))

    def test_plugin_version_mismatch_rejected(self):
        with self.assertRaises(qualification.InvalidEvidence):
            self.positive(self.log.replace('395520', '394240'))

    def test_missing_plugin_metadata_rejected(self):
        with self.assertRaises(qualification.InvalidEvidence):
            self.positive(self.log.replace('Found metadata in lib', 'missing metadata'))

    def test_runtime_system_qt_library_rejected(self):
        with self.assertRaises(qualification.InvalidEvidence):
            self.positive(self.log.replace(self.libraries[0], '/usr/lib/libQt6Core.so.6.4.2'))

    def test_wrong_version_library_in_prefix_rejected(self):
        with self.assertRaises(qualification.InvalidEvidence):
            self.positive(self.log.replace('libQt6Core.so.6.9.2', 'libQt6Core.so.6.4.2'))

    def test_unresolved_dependency_rejected(self):
        with self.assertRaises(qualification.InvalidEvidence):
            qualification.check_linkage('libasan.so libubsan.so libQt6Core.so => not found',
                                        self.prefix, self.version)

    def test_missing_prefix_rejected(self):
        with self.assertRaises(qualification.Blocked):
            qualification.qtpaths(self.prefix / 'missing')

    def test_wrong_version_rejected(self):
        with self.assertRaises(qualification.Blocked):
            qualification.check_version('6.4.2', '6.9.2')

    def test_negative_control_requires_exit_and_specific_leak(self):
        qualification.check_negative(1, self.leak, self.version)
        for code, text in ((0, self.leak), (1, ''), (-11, self.leak),
                           (1, self.leak.replace('123 byte(s)', '124 byte(s)')),
                           (1, self.leak.replace('leakForDetectorControl', 'unrelatedLeak'))):
            with self.subTest(code=code, text=text), self.assertRaises(qualification.InvalidEvidence):
                qualification.check_negative(code, text, self.version)

    def test_missing_or_tampered_raw_evidence_rejected(self):
        output = Path(self.temporary.name) / 'evidence'
        output.mkdir()
        log = output / 'probe-1.log'
        log.write_text(self.log)
        (output / 'SHA256SUMS').write_text(f'{qualification.digest(log)}  probe-1.log\n')
        qualification.verify_manifest(output)
        log.write_text('truncated')
        with self.assertRaises(qualification.InvalidEvidence):
            qualification.verify_manifest(output)
        log.unlink()
        with self.assertRaises(qualification.InvalidEvidence):
            qualification.verify_manifest(output)

    def test_pass_label_without_required_evidence_rejected(self):
        output = Path(self.temporary.name) / 'evidence'
        output.mkdir()
        qualification.finalize(output, {'status': 'PASS', 'qt_prefix': str(self.prefix),
                                        'qt_version': self.version})
        with self.assertRaises((OSError, qualification.InvalidEvidence)):
            qualification.verify_complete(output)


if __name__ == '__main__':
    unittest.main()

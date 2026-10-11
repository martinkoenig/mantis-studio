"""Synthetic filesystem evidence only; never runs live inspection."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("readiness", ROOT / "tools/x1_uart_readiness.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Readiness(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.uart = m.DT + "/soc/serial@a90000"
        self.spi = m.DT + "/soc/spi@a90000"
        self.write("etc/os-release", b'ID=synthetic\n')
        self.write("proc/sys/kernel/osrelease", b"fixture-kernel\n")
        self.write("proc/sys/kernel/random/boot_id", b"fixture-boot\n")
        self.write("proc/cmdline", b"root=fixture\n")
        self.write(self.uart + "/compatible", b"qcom,geni-uart\0")
        self.write(self.uart + "/status", b"okay\0")
        self.write(self.spi + "/status", b"disabled\0")
        self.write(m.DT + "/soc/pins/phandle", struct.pack(">I", 9))
        self.write(self.uart + "/pinctrl-0", struct.pack(">I", 9))
        self.write(m.DT + "/aliases/serial2", b"/soc/serial@a90000\0")
        self.link("sys/devices/platform/a90000/of_node", "/" + self.uart)
        self.link("sys/devices/platform/a90000/driver", "/sys/bus/platform/drivers/qcom-uart")
        (self.root / "sys/bus/platform/drivers/qcom-uart").mkdir(parents=True)
        (self.root / "sys/devices/platform/a90000/tty/deliberately-unrelated-name").mkdir(parents=True)
        self.link("sys/class/tty/fixtureTty/device",
                  "/sys/devices/platform/a90000/tty/deliberately-unrelated-name")

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def link(self, name, target):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(target)

    def report(self):
        report = m.Inspector(self.root).run()
        self.assertEqual(report["readiness"]["physical_authorization"], "BLOCKED")
        self.assertLess(len(json.dumps(report)), m.MAX_REPORT)
        self.assertTrue(all(f["state"] in ("ESTABLISHED", "UNKNOWN", "UNAVAILABLE", "CONFLICT")
                            for f in report["findings"]))
        return report

    def finding(self, code, report=None):
        return next(f for f in (report or self.report())["findings"] if f["code"] == code)

    def test_t01_t02_deterministic_identity(self):
        self.assertEqual(self.report(), self.report())
        self.assertEqual(self.finding("TTY_MAPPING")["state"], "ESTABLISHED")
        self.assertEqual(self.report()["controller"]["ttys"][0]["name"], "fixtureTty")
        # Generic binding defaults status to enabled, including all parents.
        (self.root / (self.uart + "/status")).unlink()
        self.assertEqual(self.finding("UART_DT_ENABLED")["state"], "ESTABLISHED")
        self.write(m.DT + "/soc/status", b"disabled\0")
        self.assertEqual(self.finding("UART_DT_DISABLED")["state"], "CONFLICT")

    def test_t03_wrong_mapping(self):
        path = self.root / "sys/devices/platform/a90000/of_node"
        path.unlink()
        path.symlink_to("/" + self.spi)
        self.assertEqual(self.finding("TTY_MAPPING_MISSING")["state"], "UNKNOWN")

    def test_t04_ambiguity(self):
        self.link("sys/class/tty/other/device", "/sys/devices/platform/a90000")
        self.assertEqual(self.finding("TTY_MAPPING_AMBIGUOUS")["state"], "CONFLICT")
        self.write(m.DT + "/other/serial@a90000/compatible", b"fixture,uart\0")
        self.assertEqual(self.finding("UART_AMBIGUOUS")["state"], "CONFLICT")

    def test_t05_spi_conflict(self):
        self.write(self.spi + "/status", b"okay\0")
        self.assertEqual(self.finding("SERIAL_ENGINE_CONFLICT")["state"], "CONFLICT")

    def test_t06_missing_disabled(self):
        self.write(self.uart + "/status", b"disabled\0")
        self.assertEqual(self.finding("UART_DT_DISABLED")["state"], "CONFLICT")
        (self.root / (self.uart + "/compatible")).unlink()
        (self.root / (self.uart + "/status")).unlink()
        (self.root / (self.uart + "/pinctrl-0")).unlink()
        (self.root / self.uart).rmdir()
        self.assertEqual(self.finding("UART_MISSING")["state"], "UNAVAILABLE")

    def test_t07_pinmux(self):
        self.assertEqual(self.finding("PINMUX_UNAVAILABLE")["state"], "UNAVAILABLE")
        self.write("sys/kernel/debug/pinctrl/fixture/pinmux-pins",
                   b"pin 50 (gpio50): a90000.serial function uart\npin 51 (gpio51): uart\n")
        r = self.report()
        self.assertEqual(len(r["pinmux"]), 2)
        self.assertEqual(self.finding("PINMUX_REVIEW_REQUIRED", r)["state"], "UNKNOWN")

    def test_t08_consumers(self):
        self.assertEqual(self.finding("CONSUMER_EXCLUSIVITY_UNKNOWN")["state"], "UNKNOWN")
        for path, data in [("proc/cmdline", b"console=fixtureTty,115200"),
                           ("proc/consoles", b"fixtureTty -W- (EC p)"),
                           ("evidence/active-gettys.txt", b"serial-getty@fixtureTty.service"),
                           ("proc/cmdline", b"earlycon=qcom_geni,0xa90000")]:
            with self.subTest(path=path):
                self.write(path, data)
                self.assertEqual(self.finding("CONSOLE_GETTY_CONFLICT")["state"], "CONFLICT")

    def test_configured_getty_and_operator_roles(self):
        self.write("etc/systemd/system/getty.target.wants/serial-getty@fixtureTty.service", b"fixture")
        self.assertEqual(self.finding("CONSOLE_GETTY_CONFLICT")["state"], "CONFLICT")
        self.camera("left", 1)
        self.camera("right", 2)
        for ident in (1, 2):
            (self.root / (m.DT + "/soc/camera@" + str(ident) + "/mantis,role")).unlink()
        self.write("evidence/metadata.json", json.dumps({"camera_roles": {
            "/soc/camera@1": "left", "/soc/camera@2": "right"}}).encode())
        report = self.report()
        self.assertEqual(self.finding("CAMERA_INVENTORY", report)["state"], "ESTABLISHED")
        self.assertTrue(all(c["role_provenance"] == "operator_supplied_artifact" for c in report["cameras"]))

    def test_t09_boot_provenance(self):
        self.write("evidence/artifacts/offline.dtbo", b"not active")
        self.write("evidence/metadata.json", json.dumps({"artifacts": ["evidence/artifacts/offline.dtbo"],
                   "selected_boot_entry": "operator-candidate", "captured_boot_id": "other"}).encode())
        r = self.report()
        self.assertEqual(self.finding("ACTIVE_BOOT_CHAIN", r)["state"], "UNKNOWN")
        self.assertEqual(self.finding("BOOT_ID_MISMATCH", r)["state"], "CONFLICT")
        self.assertTrue(r["platform"]["artifacts"][0]["sha256"])

    def camera(self, role, ident):
        node = m.DT + "/soc/camera@" + str(ident)
        self.write(node + "/compatible", b"ovti,ov9281\0")
        self.write(node + "/mantis,role", role.encode() + b"\0")
        ep = node + "/port/endpoint"
        peer = m.DT + "/soc/csi@" + str(ident) + "/port/endpoint"
        for path, handle, remote in [(ep, ident, ident + 10), (peer, ident + 10, ident)]:
            self.write(path + "/phandle", struct.pack(">I", handle))
            self.write(path + "/remote-endpoint", struct.pack(">I", remote))
        dev = "sys/bus/i2c/devices/camera" + str(ident)
        self.link(dev + "/of_node", "/" + node)
        self.link(dev + "/driver", "/sys/bus/i2c/drivers/ov9281")
        (self.root / "sys/bus/i2c/drivers/ov9281").mkdir(parents=True, exist_ok=True)

    def test_t10_cameras(self):
        self.assertEqual(self.finding("CAMERA_INVENTORY")["state"], "UNKNOWN")
        self.camera("left", 1)
        self.assertEqual(self.finding("CAMERA_INVENTORY")["state"], "UNKNOWN")
        self.camera("right", 2)
        self.assertEqual(self.finding("CAMERA_INVENTORY")["state"], "ESTABLISHED")
        self.write(m.DT + "/soc/camera@2/mantis,role", b"left\0")
        self.assertEqual(self.finding("CAMERA_INVENTORY")["state"], "CONFLICT")

    def test_t11_properties(self):
        for prop, data, code in [("compatible", b"missing-nul", "DT_STRING_INVALID"),
                                 ("status", b"ok\0bad\0", "UART_DT_DISABLED"),
                                 ("pinctrl-0", b"\x01", "DT_CELL_INVALID")]:
            with self.subTest(prop=prop):
                saved = (self.root / (self.uart + "/" + prop)).read_bytes()
                self.write(self.uart + "/" + prop, data)
                self.assertNotEqual(self.finding(code)["state"], "ESTABLISHED")
                self.write(self.uart + "/" + prop, saved)
        self.write(self.uart + "/pinctrl-0", struct.pack(">I", 888))
        self.assertEqual(self.finding("UART_PINCTRL_REFERENCES")["state"], "UNKNOWN")
        self.link("sys/class/tty/cycle/device", "device")
        self.assertEqual(self.finding("SYMLINK_CYCLE")["state"], "CONFLICT")

    def test_t12_security(self):
        for target in ["/etc/passwd", "../../../../../../etc/passwd"]:
            with self.subTest(target=target):
                name = self.root / "evidence/metadata.json"
                name.parent.mkdir(exist_ok=True)
                name.symlink_to(target)
                self.assertEqual(self.finding("PATH_ESCAPE")["state"], "CONFLICT")
                name.unlink()
        self.write("evidence/metadata.json", b"x" * (m.MAX_FILE + 1))
        self.assertEqual(self.finding("FILE_LIMIT")["state"], "CONFLICT")
        (self.root / "evidence/metadata.json").unlink()
        self.write("evidence/metadata.json", b'{"artifacts":["evidence/artifacts/../../../etc/passwd"]}')
        self.assertEqual(self.finding("ARTIFACT_PATH_INVALID")["state"], "CONFLICT")
        (self.root / "evidence/metadata.json").unlink()
        deep = self.root / m.DT
        for _ in range(m.MAX_DEPTH + 1):
            deep = deep / "child"
        deep.mkdir(parents=True)
        self.assertEqual(self.finding("DEPTH_LIMIT")["state"], "CONFLICT")

    def test_dependencies_and_ownership(self):
        self.write(m.DT + "/soc/clock/phandle", struct.pack(">I", 100))
        self.write(m.DT + "/soc/clock/#clock-cells", struct.pack(">I", 1))
        self.write(self.uart + "/clocks", struct.pack(">II", 100, 0))
        self.assertEqual(self.finding("UART_DEPENDENCY_CLOCKS")["state"], "ESTABLISHED")
        self.write(m.DT + "/soc/clock/status", b"disabled\0")
        self.assertEqual(self.finding("UART_DEPENDENCY_CLOCKS")["state"], "CONFLICT")
        self.write(m.DT + "/soc/competitor/pinctrl-0", struct.pack(">I", 9))
        self.assertEqual(self.finding("PINCTRL_OWNER_CONFLICT")["state"], "CONFLICT")
        self.write(self.uart + "/uart-has-rtscts", b"")
        self.assertEqual(self.finding("UART_FLOW_CONTROL")["state"], "CONFLICT")

    def test_unreadable_does_not_default_enabled(self):
        from unittest.mock import patch
        original = m.Files.read
        def denied(fs, name):
            if name == self.uart + "/status":
                raise PermissionError("fixture denied")
            return original(fs, name)
        with patch.object(m.Files, "read", denied):
            r = self.report()
            self.assertEqual(self.finding("EVIDENCE_UNREADABLE", r)["state"], "UNAVAILABLE")
            self.assertFalse(any(f["code"] == "UART_DT_ENABLED" for f in r["findings"]))

    def test_scope_and_privacy(self):
        self.write("proc/cmdline", b"console=fixtureTty,115200 api_token=do-not-report password=hidden")
        report = self.report()
        self.assertNotIn("do-not-report", json.dumps(report))
        self.assertNotIn("hidden", json.dumps(report))
        path = self.root / "sys/class/tty/fixtureTty/device"
        path.unlink()
        path.symlink_to("../../../../home/private")
        self.assertEqual(self.finding("PATH_SCOPE_ESCAPE")["state"], "CONFLICT")

    def test_t12_special_and_bounds(self):
        # A FIFO must not block or be read; O_PATH pins without device activation.
        path = self.root / "evidence/metadata.json"
        path.parent.mkdir(exist_ok=True)
        os.mkfifo(path)
        self.assertEqual(self.finding("SPECIAL_FILE")["state"], "CONFLICT")
        path.unlink()
        original_bytes = m.MAX_BYTES
        try:
            m.MAX_BYTES = 1
            limited = self.report()
            self.assertEqual(self.finding("BYTE_LIMIT", limited)["state"], "CONFLICT")
            self.assertLessEqual(limited["resource_use"]["bytes_read"], m.MAX_BYTES)
        finally:
            m.MAX_BYTES = original_bytes
        original_report = m.MAX_REPORT
        try:
            m.MAX_REPORT = 1
            limited = m.Inspector(self.root).run()
            self.assertEqual(limited["findings"], [{"code": "REPORT_LIMIT", "state": "CONFLICT"}])
            self.assertEqual(limited["readiness"]["physical_authorization"], "BLOCKED")
        finally:
            m.MAX_REPORT = original_report
        original = m.MAX_ENTRIES
        try:
            m.MAX_ENTRIES = 1
            self.assertEqual(self.finding("ENTRY_LIMIT")["state"], "CONFLICT")
        finally:
            m.MAX_ENTRIES = original

    def test_t13_t14_classifications_and_cli(self):
        r = self.report()
        self.assertIn("PHYSICAL_AUTHORIZATION_BLOCKED", r["blockers"])
        self.assertEqual(r["readiness"]["physical_authorization"], "BLOCKED")
        run = subprocess.run([sys.executable, str(ROOT / "tools/x1_uart_readiness.py"),
                              "--snapshot", str(self.root)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout), r)
        bad = subprocess.run([sys.executable, str(ROOT / "tools/x1_uart_readiness.py"),
                              "--snapshot", "/"], capture_output=True, text=True)
        self.assertEqual(bad.returncode, 2)


if __name__ == "__main__":
    unittest.main()

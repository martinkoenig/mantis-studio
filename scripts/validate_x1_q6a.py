"""Q6A validation orchestration. Pairing policy lives in tools/validate_x1_pairing.py."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import traceback
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
BUS = "platform:acb3000.isp"
LINKS = (("msm_csiphy2", 1, "msm_csid2", 0),
         ("msm_csid2", 1, "msm_vfe2_rdi0", 0),
         ("msm_csiphy3", 1, "msm_csid3", 0),
         ("msm_csid3", 1, "msm_vfe3_rdi0", 0))


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def validate_profile(profile):
    expected = {"format_version": 2, "hardware_sync_configured": False,
                "max_v4l2_delta_ns": 5000000}
    for name, value in expected.items():
        require(type(profile.get(name)) is type(value) and profile[name] == value,
                f"Profile {name} must be {value!r}")
    for name, value in dict(width=1280, height=720, fps=120, fourcc="Y10P",
                            media_bus_code="Y10_1X10", vertical_blanking=196).items():
        require(profile.get("mode", {}).get(name) == value, f"Profile mode.{name} must be {value!r}")
    require(profile.get("runtime_setup", {}).get("ownership") == "selected-routes",
            "Profile must explicitly select plugin-owned selected-routes setup")
    for role, number, sensor in (("left", 2, "ov9281 18-0060"), ("right", 3, "ov9281 20-0060")):
        camera = profile.get("measurement_cameras", {}).get(role, {})
        route = [sensor, f"msm_csiphy{number}", f"msm_csid{number}",
                 f"msm_vfe{number}_rdi0", f"msm_vfe{number}_video0"]
        require(camera.get("sensor_identity") == sensor and camera.get("bus_identity") == BUS
                and camera.get("route") == route, f"Profile {role} must use the reference Q6A sensor/bus/route")


def graph_links(graph):
    """Parse outgoing links only, including disabled and immutable flags."""
    result = {}
    entity = pad = None
    for line in graph.splitlines():
        match = re.match(r"\s*- entity \d+: (.*?) \(\d+ pads?, \d+ links?(?:, \d+ routes?)?\)", line)
        if match:
            entity = match[1]
            pad = None
        match = re.match(r"\s*pad(\d+):", line)
        if match:
            pad = int(match[1])
        match = re.match(r'\s*-> "(.*?)":(\d+) \[(.*?)\]', line)
        if match and entity is not None and pad is not None:
            result[(entity, pad, match[1], int(match[2]))] = match[3]
    return result


def validate_capture_format(text, role):
    checks = ((r"Pixel Format\s*:\s*'Y10P'", "Y10P"),
              (r"Width/Height\s*:\s*1280/720\b", "1280x720"),
              (r"Bytes per Line\s*:\s*1600\b", "bytesperline=1600"),
              (r"Size Image\s*:\s*1152000\b", "sizeimage=1152000"))
    for pattern, label in checks:
        require(re.search(pattern, text), f"{role} capture read-back requires {label}")


class Validation:
    def __init__(self, args):
        self.args = args
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
        self.output = args.output_root.resolve() / f"x1-q6a-{stamp}"
        self.output.mkdir(parents=True, mode=0o700)
        self.stage = "pre-flight"
        self.report = {"result": "FAIL", "stage": self.stage, "reason": "Validation did not finish",
                       "started_utc": stamp, "logs": str(self.output),
                       "sustained_storage_acceptance": "NOT ESTABLISHED", "cleanup_errors": []}
        self.daemon = self.client = self.daemon_log = None
        self.service_restore = False
        self.media = None
        self.restore_links = []
        self.env = os.environ.copy()
        for name in ("daemon.log", "media-before.txt", "media-after.txt"):
            (self.output / name).write_text("Not reached\n")
        for name in ("devices.json", "pairing.json"):
            self.write_json(name, {"status": "NOT REACHED"})

    def write_json(self, name, value):
        (self.output / name).write_text(json.dumps(value, indent=2) + "\n")

    def enter(self, stage):
        self.stage = stage
        self.report["stage"] = stage
        print(f"\n[{stage}]", flush=True)

    def command(self, argv, *, log=None, env=None, timeout=30, allowed=(0,)):
        """Keep output, exit status and timeout; reap only the child we started."""
        path = self.output / (log or "commands.log")
        with path.open("a+") as stream:
            stream.write("\n$ " + " ".join(map(str, argv)) + "\n")
            stream.flush()
            offset = stream.tell()
            process = subprocess.Popen(list(map(str, argv)), cwd=ROOT, env=env or self.env,
                                       stdout=stream, stderr=stream, start_new_session=True)
            try:
                deadline = time.monotonic() + timeout
                progress = time.monotonic() + 30
                while process.poll() is None:
                    require(time.monotonic() < deadline, f"Command timed out: {argv[0]}; see {path.name}")
                    if time.monotonic() >= progress:
                        print(f"  Still running {argv[0]}; log: {path.name}", flush=True)
                        progress = time.monotonic() + 30
                    time.sleep(0.05)
            finally:
                if process.poll() is None:
                    # This private process group contains only this invocation's children.
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=5)
            stream.seek(offset)
            text = stream.read()
        require(process.returncode in allowed,
                f"{' '.join(map(str, argv))} exited {process.returncode}: {text.strip()[-2000:]}")
        return text, process.returncode

    def preflight(self):
        args = self.args
        git = {}
        for name, command in (("branch", ["git", "branch", "--show-current"]),
                              ("head", ["git", "rev-parse", "HEAD"]),
                              ("status", ["git", "status", "--porcelain"])):
            git[name] = self.command(command)[0].strip()
        git["clean"] = not git["status"]
        storage = args.storage.resolve()
        profile = args.profile.resolve()
        self.report.update(git=git, architecture=platform.machine(), kernel=platform.release(),
                           build_tree=str(args.build_dir.resolve()), debug_build_tree=str(args.debug_build_dir.resolve()),
                           profile=str(profile), storage=str(storage), duration_seconds=args.duration,
                           mode="full" if args.full else "smoke", fake_state=os.environ.get("MANTIS_X1_FAKE", "UNSET"))
        for name in ("git", "architecture", "kernel", "build_tree", "profile", "storage", "fake_state"):
            print(f"  {name}: {self.report[name]}", flush=True)
        require(storage.is_dir(), f"Storage directory does not exist: {storage}")
        available = shutil.disk_usage(storage).free
        # Packed dual 120 FPS payload + container overhead, startup and ample reserve.
        required = math.ceil(276480000 * (args.duration + 1) * 1.25) + 64 * 1024**2
        self.report.update(available_storage_bytes=available, required_storage_bytes=required)
        print(f"  Available storage: {available / 1e9:.3f} GB; required reserve: {required / 1e9:.3f} GB", flush=True)
        require(not os.environ.get("MANTIS_X1_FAKE"), "Hardware validation rejects MANTIS_X1_FAKE; unset it")
        require(platform.system() == "Linux", "Q6A hardware validation requires Linux")
        profile_bytes = profile.read_bytes()
        reference = json.loads(profile_bytes)
        validate_profile(reference)
        self.report["profile_sha256"] = hashlib.sha256(profile_bytes).hexdigest()
        require(available >= required, f"Insufficient storage: {available} bytes available, {required} required")
        for executable in ("cmake", "ctest", "media-ctl", "v4l2-ctl"):
            require(shutil.which(executable), f"Required executable unavailable: {executable}")
        self.env.pop("MANTIS_X1_FAKE", None)  # Empty-but-set also mislabels backend metadata.
        # Restrict the plugin's conflict policy for this harness. The source profile
        # stays byte-for-byte untouched; an EBUSY becomes a diagnostic failure.
        reference["runtime_setup"]["disable_conflicting_links"] = False
        self.write_json("validation-profile.json", reference)
        effective_profile = self.output / "validation-profile.json"
        self.report["effective_profile"] = str(effective_profile)
        print(f"  Runtime profile snapshot: {effective_profile}; disable_conflicting_links=false to preserve other routes", flush=True)
        self.env.update(MANTIS_X1_PROFILE=str(effective_profile), MANTIS_TOKEN=secrets.token_hex(32))
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", args.port))
            port = probe.getsockname()[1]
        self.report["port"] = port
        self.env["MANTIS_PORT"] = str(port)

    def builds(self):
        configurations = [("debug", self.args.debug_build_dir, "Debug")] if self.args.full else []
        configurations.append(("release", self.args.build_dir, "Release"))
        self.report["software"] = {}
        for label, build, kind in configurations:
            self.enter(f"build-{label}")
            self.command(["cmake", "-S", ROOT, "-B", build.resolve(), "-G", "Ninja",
                          f"-DCMAKE_BUILD_TYPE={kind}", "-DBUILD_TESTING=ON",
                          "-DMANTIS_SANITIZE=OFF",
                          f"-DMANTIS_BUILD_STUDIO={'ON' if label == 'debug' else 'OFF'}",
                          f"-DPython3_EXECUTABLE={sys.executable}"], log=f"build-{label}.log", timeout=300)
            self.command(["cmake", "--build", build.resolve(), "--parallel", self.args.jobs],
                         log=f"build-{label}.log", timeout=1800)
            if self.args.full:
                self.enter(f"ctest-{label}")
                test_env = dict(self.env, TMPDIR="/dev/shm")
                test_env.pop("MANTIS_X1_PROFILE", None)
                test_env.pop("MANTIS_X1_FAKE", None)
                junit = self.output / f"ctest-{label}.xml"
                self.report["software"][label] = {"result": "FAIL", "reason": "CTest did not finish"}
                self.command(["ctest", "--test-dir", build.resolve(), "--output-on-failure", "--output-junit", junit],
                             env=test_env, log=f"ctest-{label}.log", timeout=1800)
                cases = ET.parse(junit).getroot().findall(".//testcase")
                require(cases and all(c.find("failure") is None and c.find("skipped") is None for c in cases),
                        f"CTest {label} had missing, failed or skipped tests")
                self.report["software"][label] = {"passed": len(cases), "total": len(cases), "result": "PASS"}
                print(f"  {label}: {len(cases)}/{len(cases)} PASS", flush=True)

    def service_action(self, action):
        argv = ["systemctl", action, "mantis-cameras.service"]
        if os.geteuid() != 0:
            argv = ["sudo", "--"] + argv
        print(f"  {' '.join(argv)} (temporary service state change)", flush=True)
        self.command(argv, log="service.log", timeout=60)

    def service(self):
        self.enter("camera-service")
        if not shutil.which("systemctl"):
            self.report["service"] = {"exists": False, "state": "systemctl unavailable"}
            print("  systemctl unavailable; no service changed", flush=True)
            return
        # A missing unit is optional; an inaccessible system manager is a failure.
        text, _ = self.command(["systemctl", "show", "mantis-cameras.service", "--no-pager",
                                "--property=LoadState,ActiveState"], log="service.log", allowed=(0, 1, 4))
        properties = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
        require(properties.get("LoadState") in ("loaded", "not-found", "masked"),
                f"Cannot determine mantis-cameras.service state: {text.strip()}")
        exists = properties["LoadState"] != "not-found"
        state = properties.get("ActiveState", "unknown")
        self.report["service"] = {"exists": exists, "prior_state": state, "restored": False}
        print(f"  mantis-cameras.service: {'absent' if not exists else state}", flush=True)
        if exists:
            require(state in ("active", "inactive", "failed"), f"Service has transitional/unknown state: {state}")
            if state == "active":
                # Remember before stop so a partial stop failure still attempts restoration.
                self.service_restore = True
                self.service_action("stop")
            self.assert_service_stopped()

    def assert_service_stopped(self):
        if self.report.get("service", {}).get("exists"):
            text, _ = self.command(["systemctl", "show", "mantis-cameras.service", "--property=ActiveState", "--value"], log="service.log")
            require(text.strip() in ("inactive", "failed"), f"Camera setup service must be stopped: {text.strip()}")

    def graph(self, filename):
        text, _ = self.command(["media-ctl", "-d", self.media, "-p"])
        (self.output / filename).write_text(text)
        return text

    def set_link(self, link, enabled):
        src, src_pad, dst, dst_pad = link
        spec = f'"{src}":{src_pad} -> "{dst}":{dst_pad} [{int(enabled)}]'
        print(f"  Measurement link: {spec}", flush=True)
        self.command(["media-ctl", "-d", self.media, "-l", spec])

    def media_before(self):
        self.enter("media-controller")
        # media-ctl resolves bus names, but enumerate to reject ambiguity explicitly.
        candidates = []
        for path in sorted(Path("/dev").glob("media*")):
            text, _ = self.command(["media-ctl", "-d", path, "-p"])
            if re.search(r"bus info\s+" + re.escape(BUS) + r"\s*$", text, re.MULTILINE):
                candidates.append(str(path))
        require(len(candidates) == 1, f"Expected one media controller for {BUS}; found {candidates}")
        self.media = candidates[0]
        self.report["media_controller"] = self.media
        print(f"  {BUS} -> {self.media}", flush=True)
        before = self.graph("media-before.txt")
        links = graph_links(before)
        for link in LINKS:
            require(link in links, f"Missing measurement link: {link}")
            require("IMMUTABLE" not in links[link], f"Measurement link unexpectedly immutable: {link}")
        self.original_graph_links = links
        if self.args.full or self.args.disable_measurement_links:
            self.enter("disable-measurement-links")
            for link in LINKS:
                # Save each original state before attempting a potentially partial mutation.
                self.restore_links.append((link, "ENABLED" in links[link]))
                self.set_link(link, False)
            disabled = graph_links(self.graph("media-disabled.txt"))
            require(all(link in disabled and "ENABLED" not in disabled[link] for link in LINKS),
                    "Measurement links did not read back disabled")
            require(all(disabled.get(link) == flags for link, flags in links.items() if link not in LINKS),
                    "Unrelated media links changed during disabled-link setup")

    def start_daemon(self):
        self.enter("daemon-readiness")
        build = self.args.build_dir.resolve()
        sys.path.insert(0, str(build / "python"))
        import mantis
        self.mantis = mantis
        self.env["PYTHONPATH"] = str(build / "python")
        project_root = Path(tempfile.mkdtemp(prefix="mantis-x1-q6a-", dir=self.args.storage.resolve()))
        self.report["project"] = str(project_root / "Capture.mantis")
        self.daemon_log = (self.output / "daemon.log").open("w")
        self.daemon = subprocess.Popen([str(build / "bin/mantisd"), "--project", self.report["project"]],
                                       cwd=ROOT, env=self.env, stdout=self.daemon_log, stderr=self.daemon_log,
                                       start_new_session=True)
        self.report["daemon_pid"] = self.daemon.pid
        self.client = mantis.connect(port=self.report["port"], token=self.env["MANTIS_TOKEN"])
        deadline = time.monotonic() + 20
        while True:
            require(self.daemon.poll() is None, f"Started mantisd exited {self.daemon.returncode}; inspect daemon.log")
            try:
                self.client.snapshot()
                break
            except (OSError, mantis.MantisError):
                require(time.monotonic() < deadline, "Daemon readiness timed out after 20 seconds; inspect daemon.log")
                time.sleep(0.05)
        print(f"  Ready: own daemon PID {self.daemon.pid}, port {self.report['port']}", flush=True)

    def discovery(self):
        self.enter("discovery")
        from google.protobuf.json_format import MessageToDict
        devices = self.client.devices.list()
        self.write_json("devices.json", {"devices": [MessageToDict(d, preserving_proto_field_name=True) for d in devices]})
        parents = [d for d in devices if d.plugin_id == "org.mantis.x1" and not d.parent]
        require(len(parents) == 1, "Expected one X1 parent; inspect devices.json and daemon.log")
        parent = parents[0]
        require(parent.metadata.get("backend") == "Linux V4L2", "Discovery backend must be Linux V4L2")
        validate_profile(json.loads(parent.metadata["profile"]))
        children = [d for d in devices if d.parent == parent.id]
        require(len(children) == 2 and set(parent.children) == {d.id for d in children}, "Expected LEFT/RIGHT X1 children")
        self.report["discovery"] = {"parent": "PASS", "disabled_links": "PASS" if self.restore_links else "NOT REQUESTED"}
        for role, sensor in (("left", "ov9281 18-0060"), ("right", "ov9281 20-0060")):
            matches = [d for d in children if d.metadata.get("role") == role]
            require(len(matches) == 1 and matches[0].metadata.get("sensor") == sensor, f"Incorrect {role} sensor mapping")
            self.report["discovery"][role] = dict(matches[0].metadata)
            print(f"  {role.upper()}: {sensor} -> {matches[0].metadata['video_node']}", flush=True)
        if self.restore_links:
            links = graph_links(self.graph("media-discovery.txt"))
            require(all(link in links and "ENABLED" not in links[link] for link in LINKS),
                    "Discovery unexpectedly enabled measurement links")

    def capture(self):
        self.enter("capture-finalization-replay")
        self.assert_service_stopped()
        # No pairing algorithm is duplicated here; the public-client validator owns it.
        after = max((event.sequence for event in self.client.events()), default=0)
        try:
            text, _ = self.command([sys.executable, ROOT / "tools/validate_x1_pairing.py",
                                    "--duration", self.args.duration], log="pairing-validator.log",
                                   timeout=self.args.duration + 180)
        except Exception as error:
            failure = {"short_pairing_check": "FAIL", "reason": str(error)}
            try:
                events = self.client.events(after=after)
                snapshots = [json.loads(event.message) for event in events if event.component == "capture.diagnostics"]
                if snapshots:
                    failure["failure_diagnostics"] = snapshots[-1]
            except Exception as diagnostic_error:
                failure["diagnostic_error"] = str(diagnostic_error)
            self.write_json("pairing.json", failure)
            self.report["pairing"] = failure
            raise
        result = json.loads(text)
        self.write_json("pairing.json", result)
        self.report["pairing"] = result
        print(f"  FrameSets: {result['capture']['framesets_committed']}; replay/integrity PASS", flush=True)

    def hardware_after(self):
        self.enter("post-capture-hardware")
        self.assert_service_stopped()
        after = graph_links(self.graph("media-after.txt"))
        require(all(link in after and "ENABLED" in after[link] for link in LINKS),
                "Plugin-owned setup did not leave measurement links enabled")
        require(all(after.get(link) == flags for link, flags in self.original_graph_links.items() if link not in LINKS),
                "Unrelated media links changed during validation; inspect media-before/after.txt")
        self.report["hardware"] = {}
        for role, number, sensor in (("left", 2, "ov9281 18-0060"), ("right", 3, "ov9281 20-0060")):
            video_entity = f"msm_vfe{number}_video0"
            sensor_node = self.command(["media-ctl", "-d", self.media, "-e", sensor])[0].strip()
            video_node = self.command(["media-ctl", "-d", self.media, "-e", video_entity])[0].strip()
            require(re.fullmatch(r"/dev/v4l-subdev\d+", sensor_node) and re.fullmatch(r"/dev/video\d+", video_node),
                    f"Invalid dynamic nodes for {role}: {sensor_node}, {video_node}")
            require(video_node == self.report["discovery"][role]["video_node"], f"{role} discovery/video entity mismatch")
            log = f"hardware-{role}.log"
            capture_format = self.command(["v4l2-ctl", "-d", video_node, "--get-fmt-video"], log=log)[0]
            validate_capture_format(capture_format, role)
            vblank = self.command(["v4l2-ctl", "-d", sensor_node, "--get-ctrl=vertical_blanking"], log=log)[0]
            require(re.search(r"vertical_blanking\s*:\s*196\b", vblank), f"{role} VBLANK read-back is not 196: {vblank.strip()}")
            # Validate every upstream pad the selected-route plugin is expected to set.
            pads = [(sensor, 0)] + [(entity, pad) for entity in
                    (f"msm_csiphy{number}", f"msm_csid{number}", f"msm_vfe{number}_rdi0") for pad in (0, 1)]
            for entity, pad in pads:
                text = self.command(["media-ctl", "-d", self.media, "--get-v4l2", f'"{entity}":{pad}'], log=log)[0]
                require(re.search(r"fmt:Y10_1X10/1280x720\b", text), f"{role} {entity}:{pad} must read back Y10_1X10/1280x720")
            self.report["hardware"][role] = dict(sensor=sensor, sensor_node=sensor_node, video_entity=video_entity,
                                                video_node=video_node, fourcc="Y10P", width=1280, height=720,
                                                bytesperline=1600, sizeimage=1152000, vblank=196, media_bus_code="Y10_1X10")
            print(f"  {role.upper()}: {video_node} Y10P 1280x720 stride=1600 size=1152000; {sensor_node} VBLANK=196", flush=True)

    def cleanup(self):
        print("\n[cleanup]", flush=True)
        # Do not allow a second signal to interrupt restoration. Commands remain bounded.
        previous = {sig: signal.signal(sig, signal.SIG_IGN) for sig in (signal.SIGINT, signal.SIGTERM)}
        def attempt(label, action):
            try:
                action()
            except Exception as error:
                self.report["cleanup_errors"].append(f"{label}: {error}")
                print(f"  Cleanup FAIL: {label}: {error}", flush=True)
        try:
            if self.daemon is not None:
                def stop_daemon():
                    if self.daemon.poll() is None:
                        print(f"  Stopping own daemon PID {self.daemon.pid}", flush=True)
                        if self.client is not None:
                            try:
                                self.client.shutdown()
                            except Exception:
                                self.daemon.terminate()
                        else:
                            self.daemon.terminate()
                        try:
                            self.daemon.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            self.daemon.kill()
                            self.daemon.wait(timeout=5)
                    # Reap any plugin hosts in this daemon's private group after exit.
                    try:
                        os.killpg(self.daemon.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    require(self.daemon.returncode == 0, f"mantisd cleanup exit status {self.daemon.returncode}; capture may be RECOVERABLE")
                attempt("daemon shutdown", stop_daemon)
            if self.daemon_log:
                self.daemon_log.close()
            if self.media:
                if (self.output / "media-after.txt").read_text() == "Not reached\n":
                    attempt("failure media snapshot", lambda: self.graph("media-after.txt"))
                for link, enabled in reversed(self.restore_links):
                    attempt(f"restore {link}", lambda link=link, enabled=enabled: self.set_link(link, enabled))
                if self.restore_links:
                    def check_restored():
                        links = graph_links(self.graph("media-cleanup.txt"))
                        require(all(link in links and ("ENABLED" in links[link]) == enabled
                                    for link, enabled in self.restore_links), "Measurement link restoration read-back mismatch")
                    attempt("measurement link restoration", check_restored)
            if self.service_restore:
                def restore_service():
                    self.service_action("start")
                    text, _ = self.command(["systemctl", "show", "mantis-cameras.service", "--property=ActiveState", "--value"], log="service.log")
                    require(text.strip() == "active", f"Service restoration read-back: {text.strip()}")
                    self.report["service"]["restored"] = True
                attempt("service restoration", restore_service)
        finally:
            for sig, handler in previous.items():
                signal.signal(sig, handler)

    def finish(self):
        if self.report["cleanup_errors"]:
            if self.report["result"] == "PASS":
                self.report.update(result="FAIL", stage="cleanup", reason="; ".join(self.report["cleanup_errors"]))
        lines = ["Mantis X1 Q6A Validation", "========================", "", "Git"]
        git = self.report.get("git", {})
        lines += [f"  Branch: {git.get('branch', 'unavailable')}", f"  HEAD: {git.get('head', 'unavailable')}",
                  f"  Clean: {'PASS' if git.get('clean') else 'DIRTY (see summary.json)'}"]
        lines += ["", "Software"]
        for label in ("debug", "release"):
            suite = self.report.get("software", {}).get(label)
            if suite and suite["result"] == "PASS":
                lines.append(f"  {label.title()} tests: {suite['passed']}/{suite['total']} PASS")
            else:
                lines.append(f"  {label.title()} tests: {'FAIL' if suite else 'NOT RUN'}")
        discovery = self.report.get("discovery", {})
        lines += ["", "Discovery", f"  X1 parent: {discovery.get('parent', 'NOT REACHED')}"]
        for role in ("left", "right"):
            camera = discovery.get(role, {})
            lines.append(f"  {role.upper()}: {camera.get('sensor', 'NOT REACHED')} {camera.get('video_node', '')}")
        lines.append(f"  Disabled-link discovery: {discovery.get('disabled_links', 'NOT REACHED')}")
        hardware = self.report.get("hardware", {})
        lines += ["", "Media setup", f"  Y10_1X10 / Y10P 1280x720: {'PASS' if len(hardware) == 2 else 'NOT VERIFIED'}",
                  "  VBLANK: " + " / ".join(str(hardware.get(r, {}).get("vblank", "?")) for r in ("left", "right"))]
        pairing = self.report.get("pairing", {})
        capture = pairing.get("capture", {})
        metrics = capture.get("diagnostics", pairing.get("failure_diagnostics", {}))
        lines += ["", "Software correspondence (exposure skew unavailable)",
                  f"  Tolerance: {metrics.get('max_v4l2_delta_ns', '?')} ns", f"  Mode: {metrics.get('pairing_mode', 'NOT REACHED')}",
                  f"  Native offset: {int(metrics['native_sequence_offset']):+d}" if "native_sequence_offset" in metrics else "  Native offset: ?",
                  f"  Startup unmatched: {metrics.get('startup_unmatched_left', '?')} / {metrics.get('startup_unmatched_right', '?')}",
                  f"  Steady-state unmatched: {metrics.get('steady_state_unmatched_left', '?')} / {metrics.get('steady_state_unmatched_right', '?')}",
                  f"  Shutdown unmatched: {metrics.get('shutdown_unmatched_left', '?')} / {metrics.get('shutdown_unmatched_right', '?')}",
                  f"  Paired timestamp delta: {int(metrics['paired_v4l2_delta_ns']) / 1e6:+.3f} ms" if "paired_v4l2_delta_ns" in metrics else "  Paired timestamp delta: ?",
                  f"  Failures: {metrics.get('pairing_failures', '?')}"]
        for role in ("left", "right"):
            values = [metrics.get(f"{role}.observed_{name}_ns", "?") for name in ("period", "half_period", "max_period")]
            lines.append(f"  {role.upper()} period / half / max (diagnostic ns): " + " / ".join(values))
        lines += ["", "Capture", f"  FrameSets: {capture.get('framesets_produced', '?')} / {capture.get('framesets_committed', '?')}",
                  f"  Raw drops: {capture.get('dropped', 0) if capture else '?'}",
                  f"  Queue saturation: {capture.get('queue_saturation', 0) if capture else '?'}"]
        verify = pairing.get("verification", {})
        lines += ["", "RawCapture", f"  Finalized: {pairing.get('raw_artifact_state', 'NOT VERIFIED')}",
                  f"  Replay verification: {verify.get('replay', 'NOT VERIFIED')}",
                  f"  Raw integrity: {verify.get('raw_integrity', 'NOT VERIFIED')}",
                  "", "Sustained-storage acceptance: NOT ESTABLISHED", "", f"RESULT: {self.report['result']}"]
        if self.report["result"] != "PASS":
            lines += [f"Stage: {self.report['stage']}", f"Reason: {self.report['reason']}"]
        if self.report["cleanup_errors"]:
            lines += ["Cleanup errors: " + "; ".join(self.report["cleanup_errors"])]
        lines += [f"Logs: {self.output}"]
        if "project" in self.report:
            lines += [f"Retained capture: {self.report['project']}"]
        self.write_json("summary.json", self.report)
        (self.output / "summary.txt").write_text("\n".join(lines) + "\n")
        print("\n" + "\n".join(lines), flush=True)

    def run(self):
        try:
            self.preflight()
            self.builds()
            self.service()
            self.media_before()
            self.start_daemon()
            self.discovery()
            self.capture()
            self.hardware_after()
            self.report.update(result="PASS", stage="complete", reason="All requested checks passed")
        except Exception as error:
            self.report.update(result="FAIL", stage=self.stage, reason=str(error))
            (self.output / "failure.log").write_text(traceback.format_exc())
        finally:
            self.cleanup()
            self.finish()
        return 0 if self.report["result"] == "PASS" else 1


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--smoke", action="store_true", help="Release build + short real capture and deterministic replay")
    mode.add_argument("--full", action="store_true", help="Debug/Release builds and all tests, disabled-link discovery + smoke")
    parser.add_argument("--duration", type=float, default=1, help="Capture seconds, (0,60]; default 1")
    parser.add_argument("--storage", type=Path, default=Path("/dev/shm"), help="Existing capture filesystem directory")
    parser.add_argument("--profile", type=Path, default=Path(os.environ.get("MANTIS_X1_PROFILE") or ROOT / "profiles/x1-q6a.json"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/release", help="Release build tree")
    parser.add_argument("--debug-build-dir", type=Path, default=ROOT / "build/debug")
    parser.add_argument("--output-root", type=Path, default=ROOT / "validation-output")
    parser.add_argument("--port", type=int, default=0, help="Loopback port; default chooses a free port")
    parser.add_argument("--jobs", type=int, default=4, help="Parallel build jobs")
    parser.add_argument("--disable-measurement-links", action="store_true", help="Also test disabled-link discovery in smoke (always in full)")
    args = parser.parse_args(argv)
    if not math.isfinite(args.duration) or not 0 < args.duration <= 60:
        parser.error("--duration must be in (0,60] seconds")
    if not 0 <= args.port <= 65535 or args.jobs < 1:
        parser.error("--port must be 0..65535; --jobs must be positive")
    if args.build_dir.resolve() == args.debug_build_dir.resolve():
        parser.error("Debug and Release build trees must differ")
    return args


def interrupted(signum, _frame):
    raise RuntimeError(f"Interrupted by {signal.Signals(signum).name}")


if __name__ == "__main__":
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    sys.exit(Validation(arguments()).run())

#!/usr/bin/env python3
"""Bounded read-only Q6A evidence inspection. Never opens a TTY or authorizes hardware.

Offline roots contain sys/, proc/, etc/ and optional evidence/metadata.json.
Absolute sysfs links are interpreted within that root, never on the host.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import stat
import struct
import sys

SCHEMA = 1
IDENTITY = "org.mantis.x1.uart-readiness/1"
MAX_FILE = 262144
MAX_BYTES = 8388608
MAX_ENTRIES = 12000
MAX_DEPTH = 32
MAX_REPORT = 262144
DT = "sys/firmware/devicetree/base"
PROPERTIES = ("compatible", "status", "phandle", "linux,phandle", "pinctrl-0",
              "pinctrl-names", "remote-endpoint", "clocks", "resets", "power-domains",
              "reg", "model", "mantis,role", "uart-has-rtscts", "#clock-cells",
              "#reset-cells", "#power-domain-cells")


class EvidenceError(Exception):
    pass


class Files:
    """Root-contained, component-wise no-follow reads; no device/special file reads.

    Even a symlink swapped after resolution cannot make open() follow it. All
    directory enumeration, bytes and link/depth work share per-inspection limits.
    """
    def __init__(self, root):
        self.root = Path(root).resolve(strict=True)
        self.bytes = self.entries = 0
        self.sources = set()

    def resolve(self, name):
        pending = list(PurePosixPath(name).parts)
        if not pending or pending[0] == "/":
            raise EvidenceError("INVALID_PATH")
        parts = []
        links = 0
        while pending:
            item = pending.pop(0)
            if item == ".":
                continue
            if item == "..":
                if not parts:
                    raise EvidenceError("PATH_ESCAPE")
                parts.pop()
                continue
            parts.append(item)
            if len(parts) > MAX_DEPTH:
                raise EvidenceError("DEPTH_LIMIT")
            path = self.root.joinpath(*parts)
            if path.is_symlink():
                links += 1
                if links > 16:
                    raise EvidenceError("SYMLINK_CYCLE")
                target = os.readlink(path)
                parts.pop()
                if target.startswith("/"):
                    if not target.startswith("/sys/"):
                        raise EvidenceError("PATH_ESCAPE")
                    parts = []
                    target = target[1:]
                pending = list(PurePosixPath(target).parts) + pending
        resolved = "/".join(parts)
        # Prevent a contained link from redirecting a system inventory into an
        # unrelated captured home/configuration directory. Standard os-release
        # relocation is the one documented cross-directory exception.
        scope = PurePosixPath(name).parts[0]
        if not (resolved == scope or resolved.startswith(scope + "/") or
                name == "etc/os-release" and resolved == "usr/lib/os-release"):
            raise EvidenceError("PATH_SCOPE_ESCAPE")
        return resolved

    def open(self, name):
        resolved = self.resolve(name)
        fd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            parts = resolved.split("/")
            for i, part in enumerate(parts):
                nxt = os.open(part, os.O_NOFOLLOW | (os.O_RDONLY | os.O_DIRECTORY if
                              i + 1 < len(parts) else os.O_PATH), dir_fd=fd)
                os.close(fd)
                fd = nxt
            info = os.fstat(fd)
            if not (stat.S_ISREG(info.st_mode) or stat.S_ISDIR(info.st_mode)):
                raise EvidenceError("SPECIAL_FILE")
            # Reopen the pinned, checked inode, never a raced filesystem name.
            checked = os.open("/proc/self/fd/" + str(fd), os.O_RDONLY | os.O_NONBLOCK)
            os.close(fd)
            return checked
        except BaseException:
            os.close(fd)
            raise

    def read(self, name):
        fd = self.open(name)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode):
                raise EvidenceError("SPECIAL_FILE")
            if info.st_size > MAX_FILE:
                raise EvidenceError("FILE_LIMIT")
            data = bytearray()
            while True:
                remaining = MAX_BYTES - self.bytes
                if remaining <= 0:
                    raise EvidenceError("BYTE_LIMIT")
                chunk = os.read(fd, min(8192, MAX_FILE + 1 - len(data), remaining))
                if not chunk:
                    break
                self.bytes += len(chunk)
                data.extend(chunk)
                if len(data) > MAX_FILE:
                    raise EvidenceError("FILE_LIMIT")
            self.sources.add(name)
            return bytes(data)
        finally:
            os.close(fd)

    def names(self, name):
        fd = self.open(name)
        try:
            result = []
            # scandir avoids allocating an unbounded directory listing.
            with os.scandir(fd) as entries:
                for item in entries:
                    self.entries += 1
                    if self.entries > MAX_ENTRIES:
                        raise EvidenceError("ENTRY_LIMIT")
                    result.append((item.name, item.is_dir(follow_symlinks=False)))
            return sorted(result)
        finally:
            os.close(fd)


def strings(data):
    if not data or data[-1] != 0:
        raise EvidenceError("DT_STRING_INVALID")
    try:
        result = data[:-1].decode("utf-8", errors="strict").split("\0")
    except UnicodeError as e:
        raise EvidenceError("DT_STRING_INVALID") from e
    if any(not s or any(ord(c) < 32 for c in s) for s in result):
        raise EvidenceError("DT_STRING_INVALID")
    return result


def cells(data):
    if not data or len(data) % 4:
        raise EvidenceError("DT_CELL_INVALID")
    return list(struct.unpack(">" + "I" * (len(data) // 4), data))


class Inspector:
    def __init__(self, root, live=False):
        self.fs = Files(root)
        self.live = live
        self.findings = []
        self.nodes = {}
        self.handles = {}
        self.metadata = {}

    def finding(self, code, state, sources=(), detail=None, provenance=None):
        entry = {"code": code, "state": state, "sources": list(sources),
                 "provenance": provenance or ("live_read_only" if self.live else "offline_snapshot")}
        if detail is not None:
            entry["detail"] = detail
        self.findings.append(entry)
        return entry

    def optional(self, name):
        try:
            return self.fs.read(name)
        except FileNotFoundError:
            return None
        except OSError:
            self.finding("EVIDENCE_UNREADABLE", "UNAVAILABLE", [name])
            raise EvidenceError("EVIDENCE_READ_FAILED")

    def text(self, name):
        data = self.optional(name)
        if data is None:
            return None
        try:
            return data.decode("utf-8", errors="strict").strip()
        except UnicodeError:
            self.finding("TEXT_INVALID", "UNAVAILABLE", [name])
            return None

    def tree(self, name=DT):
        try:
            entries = self.fs.names(name)
        except FileNotFoundError:
            self.finding("DT_UNAVAILABLE", "UNAVAILABLE", [name])
            return
        except OSError:
            self.finding("DT_UNREADABLE", "UNAVAILABLE", [name])
            return
        self.nodes[name] = {}
        for key, directory in entries:
            if directory:
                self.tree(name + "/" + key)
            elif key in PROPERTIES:
                data = self.optional(name + "/" + key)
                if data is not None:
                    if key in ("phandle", "linux,phandle", "pinctrl-0", "remote-endpoint",
                               "clocks", "resets", "power-domains", "reg", "#clock-cells",
                               "#reset-cells", "#power-domain-cells"):
                        cells(data)
                    self.nodes[name][key] = data
        for key in ("phandle", "linux,phandle"):
            if key in self.nodes[name]:
                value = cells(self.nodes[name][key])
                if len(value) != 1 or value[0] == 0:
                    raise EvidenceError("DT_PHANDLE_INVALID")
                old = self.handles.get(value[0])
                if old is not None and old != name:
                    raise EvidenceError("DT_PHANDLE_DUPLICATE")
                self.handles[value[0]] = name

    def string(self, node, prop):
        data = self.nodes[node].get(prop)
        return strings(data) if data is not None else None

    def enabled(self, node):
        # Generic DT status binding: absent, 'ok' or 'okay' means available;
        # this says nothing about driver binding, pinmux or electrical levels.
        current = node
        while current in self.nodes:
            status = self.string(current, "status")
            if status is not None and status not in (["ok"], ["okay"]):
                return False
            current = current.rsplit("/", 1)[0]
        return True

    def inventory(self, directory):
        try:
            return self.fs.names(directory)
        except (FileNotFoundError, NotADirectoryError):
            return []
        except OSError:
            self.finding("INVENTORY_UNREADABLE", "UNAVAILABLE", [directory])
            return []

    def ancestry(self, path):
        try:
            node = self.fs.resolve(path)
            for _ in range(MAX_DEPTH):
                link = node + "/of_node"
                try:
                    fd = self.fs.open(link)
                except FileNotFoundError:
                    pass
                else:
                    os.close(fd)
                    target = self.fs.resolve(link)
                    driver = None
                    try:
                        fd = self.fs.open(node + "/driver")
                    except FileNotFoundError:
                        pass
                    else:
                        os.close(fd)
                        driver = self.fs.resolve(node + "/driver")
                    return target, driver
                if node == "sys" or "/" not in node:
                    break
                node = node.rsplit("/", 1)[0]
        except (FileNotFoundError, NotADirectoryError):
            pass
        except OSError:
            self.finding("SYSFS_UNREADABLE", "UNAVAILABLE", [path])
        return None, None

    def controller(self):
        candidates = sorted(n for n in self.nodes if n.rsplit("/", 1)[-1] == "serial@a90000")
        result = {"candidates": candidates, "aliases": {}, "ttys": []}
        if len(candidates) != 1:
            self.finding("UART_MISSING" if not candidates else "UART_AMBIGUOUS",
                         "UNAVAILABLE" if not candidates else "CONFLICT", candidates)
            return result
        uart = candidates[0]
        compatible = self.string(uart, "compatible")
        self.finding("UART_COMPATIBLE", "ESTABLISHED" if compatible else "UNKNOWN", [uart], compatible)
        result["compatible"] = compatible
        active = self.enabled(uart)
        self.finding("UART_DT_ENABLED" if active else "UART_DT_DISABLED",
                     "ESTABLISHED" if active else "CONFLICT", [uart])
        for node in sorted(self.nodes):
            if node.rsplit("/", 1)[-1] in ("spi@a90000", "i2c@a90000"):
                self.finding("SERIAL_ENGINE_CONFLICT" if self.enabled(node) and active else
                             "COMPETING_ENGINE_DISABLED", "CONFLICT" if self.enabled(node) and active
                             else "ESTABLISHED" if not self.enabled(node) else "UNKNOWN", [node, uart])
        aliases = DT + "/aliases"
        for name, directory in self.inventory(aliases):
            if not directory and name.startswith("serial"):
                data = self.optional(aliases + "/" + name)
                if data is not None:
                    value = strings(data)
                    if len(value) != 1 or not value[0].startswith("/") or ".." in value[0].split("/"):
                        raise EvidenceError("DT_ALIAS_INVALID")
                    target = DT + value[0]
                    result["aliases"][name] = target
                    if target not in self.nodes:
                        self.finding("DT_ALIAS_UNRESOLVED", "UNKNOWN", [aliases + "/" + name])
        alias_matches = [key for key, val in result["aliases"].items() if val == uart]
        if len(alias_matches) > 1:
            self.finding("UART_ALIAS_AMBIGUOUS", "CONFLICT", [aliases], alias_matches)
        pins = self.nodes[uart].get("pinctrl-0")
        if pins is None:
            self.finding("UART_PINCTRL_MISSING", "UNKNOWN", [uart])
        else:
            refs = cells(pins)
            targets = [self.handles.get(ref) for ref in refs]
            self.finding("UART_PINCTRL_REFERENCES", "ESTABLISHED" if all(t and t != uart and self.enabled(t) for t in targets) else "UNKNOWN",
                         [uart + "/pinctrl-0"], targets)
            result["pinctrl"] = targets
            for other, props in sorted(self.nodes.items()):
                if other != uart and "pinctrl-0" in props and self.enabled(other) and active:
                    if set(refs).intersection(cells(props["pinctrl-0"])):
                        self.finding("PINCTRL_OWNER_CONFLICT", "CONFLICT", [uart, other])
        for prop in ("clocks", "resets", "power-domains"):
            if prop in self.nodes[uart]:
                refs = cells(self.nodes[uart][prop])
                # Walk providers using their declared argument widths; reject
                # unresolved bindings without treating arguments as phandles.
                providers = []
                offset = 0
                while offset < len(refs):
                    provider = self.handles.get(refs[offset])
                    width_prop = {"clocks": "#clock-cells", "resets": "#reset-cells",
                                  "power-domains": "#power-domain-cells"}[prop]
                    if provider is None or width_prop not in self.nodes[provider]:
                        break
                    width = cells(self.nodes[provider][width_prop])
                    if len(width) != 1 or width[0] > 16 or offset + 1 + width[0] > len(refs):
                        raise EvidenceError("DT_DEPENDENCY_INVALID")
                    providers.append(provider)
                    offset += 1 + width[0]
                state = ("CONFLICT" if any(not self.enabled(n) for n in providers) else
                         "ESTABLISHED" if offset == len(refs) else "UNKNOWN")
                self.finding("UART_DEPENDENCY_" + prop.upper().replace("-", "_"), state,
                             [uart + "/" + prop], {"cells": refs, "providers": providers})
        self.finding("UART_FLOW_CONTROL", "CONFLICT" if "uart-has-rtscts" in self.nodes[uart]
                     else "UNKNOWN", [uart], "DT declaration does not establish wiring")
        for tty, _ in self.inventory("sys/class/tty"):
            target, driver = self.ancestry("sys/class/tty/" + tty + "/device")
            if target == uart:
                result["ttys"].append({"name": tty, "controller": target, "driver": driver})
        matches = result["ttys"]
        self.finding("TTY_MAPPING" if len(matches) == 1 else "TTY_MAPPING_AMBIGUOUS" if matches
                     else "TTY_MAPPING_MISSING", "ESTABLISHED" if len(matches) == 1 and
                     matches[0]["driver"] else "CONFLICT" if len(matches) > 1 else "UNKNOWN",
                     ["sys/class/tty"], matches)
        return result

    def pinmux(self):
        records = []
        for controller, directory in self.inventory("sys/kernel/debug/pinctrl"):
            if not directory:
                continue
            for prop in ("pinmux-pins", "pinconf-pins"):
                path = "sys/kernel/debug/pinctrl/" + controller + "/" + prop
                value = self.text(path)
                if value:
                    for line in value.splitlines():
                        if re.search(r"\b(?:gpio|pin\s+)(?:50|51)\b", line, re.I):
                            records.append({"source": path, "line": line[:512]})
        # Driver-specific output cannot establish intended UART ownership merely
        # by containing GPIO50/51. Preserve exact relevant text for human review.
        self.finding("PINMUX_REVIEW_REQUIRED" if records else "PINMUX_UNAVAILABLE",
                     "UNKNOWN" if records else "UNAVAILABLE", [r["source"] for r in records], records)
        return records

    def cameras(self):
        cameras = []
        endpoints = {n: cells(p["remote-endpoint"]) for n, p in self.nodes.items()
                     if "remote-endpoint" in p}
        for node in sorted(self.nodes):
            comp = self.string(node, "compatible")
            if not comp or not any(c.split(",")[-1] == "ov9281" for c in comp):
                continue
            role = self.string(node, "mantis,role")
            role_source = node + "/mantis,role" if role else "evidence/metadata.json"
            # Role is accepted only from explicit captured/operator evidence.
            role = (role[0] if role and len(role) == 1 else
                    self.metadata.get("camera_roles", {}).get("/" + node[len(DT) + 1:]))
            eps = [e for e in endpoints if e.startswith(node + "/")]
            valid = bool(eps)
            for ep in eps:
                ref = endpoints[ep]
                peer = self.handles.get(ref[0]) if len(ref) == 1 else None
                back = endpoints.get(peer)
                own = self.nodes[ep].get("phandle", self.nodes[ep].get("linux,phandle"))
                valid &= peer is not None and peer != ep and not peer.startswith(node + "/") and own is not None and back == cells(own)
            registrations = []
            for base in ("sys/bus/i2c/devices", "sys/class/video4linux", "sys/class/media"):
                for name, _ in self.inventory(base):
                    target, driver = self.ancestry(base + "/" + name + "/device" if
                                                  base.startswith("sys/class") else base + "/" + name)
                    if target == node:
                        registrations.append({"source": base + "/" + name, "driver": driver})
            cameras.append({"node": node, "role": role, "role_source": role_source,
                            "role_provenance": ("live_read_only" if self.live else "offline_snapshot") if
                            role_source != "evidence/metadata.json" else "operator_supplied_artifact",
                            "dt_enabled": self.enabled(node),
                            "endpoints": eps, "reciprocal_links": bool(valid),
                            "registrations": registrations,
                            "dependency_cells": {p: cells(self.nodes[node][p]) for p in
                                                 ("clocks", "resets", "power-domains")
                                                 if p in self.nodes[node]}})
        complete = (len(cameras) == 2 and {c["role"] for c in cameras} == {"left", "right"} and
                    all(c["dt_enabled"] and c["reciprocal_links"] and
                        any(r["driver"] for r in c["registrations"]) for c in cameras))
        self.finding("CAMERA_INVENTORY", "ESTABLISHED" if complete else "CONFLICT" if
                     len(cameras) > 2 or (len(cameras) == 2 and cameras[0]["role"] ==
                                         cameras[1]["role"] and cameras[0]["role"] is not None)
                     else "UNKNOWN", [DT, "sys/bus/i2c/devices", "sys/class/video4linux"], cameras)
        media = self.inventory("sys/class/video4linux") + self.inventory("sys/class/media")
        self.finding("CAMERA_MEDIA_CORRELATION", "UNKNOWN" if media else "UNAVAILABLE",
                     ["sys/class/video4linux", "sys/class/media"],
                     "Registration names alone do not establish both complete media pipelines")
        self.finding("CAMERA_CAPTURE_NOT_RETESTED", "UNKNOWN", [],
                     "Historical Q6A acquisition validation is unchanged", "repository_configuration")
        return cameras

    def boot(self):
        kernel = self.text("proc/sys/kernel/osrelease")
        os_release = self.text("etc/os-release")
        cmdline = self.text("proc/cmdline")
        if cmdline:
            cmdline = " ".join(re.sub(r"(?i)^([^=]*(?:password|passwd|secret|token|credential)[^=]*)=.*$",
                                      r"\1=<redacted>", token) for token in cmdline.split())
        boot_id = self.text("proc/sys/kernel/random/boot_id")
        self.finding("PLATFORM_IDENTITY", "ESTABLISHED" if kernel and os_release else "UNAVAILABLE",
                     ["proc/sys/kernel/osrelease", "etc/os-release"])
        self.finding("BOOT_COHERENCE", "UNKNOWN", ["proc/sys/kernel/random/boot_id"],
                     "Offline snapshots cannot prove atomic capture")
        if self.metadata.get("captured_boot_id") and boot_id != self.metadata["captured_boot_id"]:
            self.finding("BOOT_ID_MISMATCH", "CONFLICT", ["evidence/metadata.json"])
        artifacts = []
        for path in self.metadata.get("artifacts", []):
            if (not isinstance(path, str) or not path.startswith("evidence/artifacts/") or
                    ".." in PurePosixPath(path).parts or not
                    self.fs.resolve(path).startswith("evidence/artifacts/")):
                raise EvidenceError("ARTIFACT_PATH_INVALID")
            data = self.optional(path)
            artifacts.append({"path": path, "sha256": hashlib.sha256(data).hexdigest() if
                              data is not None else None})
        self.finding("ACTIVE_BOOT_CHAIN", "UNKNOWN", ["evidence/metadata.json"],
                     "Supplied boot entry/DTB/DTBO identifiers do not prove runtime selection",
                     "operator_supplied_artifact")
        return {"kernel": kernel, "os_release": os_release, "cmdline": cmdline,
                "runtime_dt_model": self.string(DT, "model") if DT in self.nodes else None,
                "supplied_boot_entry": self.metadata.get("selected_boot_entry"), "artifacts": artifacts}

    def conflicts(self, controller, platform):
        names = [t["name"] for t in controller.get("ttys", [])]
        cmdline = platform.get("cmdline")
        consoles = self.text("proc/consoles")
        getty = self.text("evidence/active-gettys.txt")
        explicit = []
        for directory in ("etc/systemd/system/getty.target.wants", "run/systemd/system",
                          "run/systemd/system/getty.target.wants"):
            entries = [name for name, _ in self.inventory(directory)]
            for name in names:
                if "serial-getty@" + name + ".service" in entries:
                    explicit.append("getty_configured:" + name)
        for name in names:
            if (cmdline and ("console=" + name) in cmdline) or (consoles and any(
                    line.split() and line.split()[0] == name for line in consoles.splitlines())):
                explicit.append("console:" + name)
            if getty and ("serial-getty@" + name + ".service") in getty.split():
                explicit.append("getty:" + name)
        if cmdline and any(t == "earlycon" or t.startswith("earlycon=") for t in cmdline.split()):
            explicit.append("earlycon:ownership_requires_review")
        self.finding("CONSOLE_GETTY_CONFLICT" if explicit else "CONSUMER_EXCLUSIVITY_UNKNOWN",
                     "CONFLICT" if explicit else "UNKNOWN", ["proc/cmdline", "proc/consoles",
                                                            "evidence/active-gettys.txt",
                                                            "etc/systemd/system/getty.target.wants",
                                                            "run/systemd/system",
                                                            "run/systemd/system/getty.target.wants"], explicit)

    def run(self):
        try:
            data = self.optional("evidence/metadata.json")
            if data is not None:
                self.metadata = json.loads(data)
                if not isinstance(self.metadata, dict) or not isinstance(
                        self.metadata.get("camera_roles", {}), dict) or not isinstance(
                        self.metadata.get("artifacts", []), list):
                    raise EvidenceError("METADATA_INVALID")
                self.finding("SNAPSHOT_METADATA", "UNKNOWN", ["evidence/metadata.json"],
                             {"capture_timestamp": self.metadata.get("capture_timestamp"),
                              "coherence": "operator-supplied; not an atomic runtime capture"},
                             "operator_supplied_artifact")
            self.tree()
            controller = self.controller()
            pins = self.pinmux()
            cameras = self.cameras()
            platform = self.boot()
            self.conflicts(controller, platform)
        except (EvidenceError, ValueError, TypeError, OSError) as error:
            self.finding(str(error) if isinstance(error, EvidenceError) else "INSPECTION_INVALID",
                         "CONFLICT")
            controller, pins, cameras, platform = {}, [], [], {}
        self.finding("PHYSICAL_AUTHORIZATION_BLOCKED", "UNAVAILABLE", [],
                     "F1b qualification and separate authorization required", "repository_configuration")
        findings = sorted(self.findings, key=lambda f: (f["code"], json.dumps(f, sort_keys=True)))
        blocked = [f["code"] for f in findings if f["state"] != "ESTABLISHED"]
        report = {"schema_version": SCHEMA, "tool_identity": IDENTITY,
                  "inspection_mode": "live_read_only" if self.live else "offline",
                  "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat() if self.live else None,
                  "platform": platform, "controller": controller, "pinmux": pins, "cameras": cameras,
                  "evidence_sources": sorted(self.fs.sources), "findings": findings,
                  "missing_evidence": sorted(set(f["code"] for f in findings if f["state"] in
                                                 ("UNKNOWN", "UNAVAILABLE"))),
                  "blockers": sorted(set(blocked)), "readiness": {
                      "software_backend_configuration": "PTY_VALIDATED_ONLY",
                      "system_identification": "CONFLICT" if any(f["state"] == "CONFLICT" for f in
                                                                 findings) else
                      "IDENTIFIED_WITH_GAPS" if any(f["code"] == "TTY_MAPPING" and
                                                    f["state"] == "ESTABLISHED" for f in findings)
                      else "INCOMPLETE",
                      "physical_authorization": "BLOCKED"},
                  "reference_material": {
                      "candidate": "serial@a90000; GPIO50/GPIO51; header pins 23/24 (unqualified)",
                      "firmware_revision": "ec1dff2ae94f08dd309b65511dafe49eb5e9b525",
                      "physical_checklist": "https://github.com/martinkoenig/mantis-x1-firmware/blob/ec1dff2ae94f08dd309b65511dafe49eb5e9b525/docs/validation/f1b-uart-interface-checklist.md",
                      "provenance": "published_reference_material"},
                  "resource_use": {"bytes_read": self.fs.bytes, "entries_examined": self.fs.entries}}
        if len(json.dumps(report).encode()) > MAX_REPORT:
            return {"schema_version": SCHEMA, "tool_identity": IDENTITY,
                    "findings": [{"code": "REPORT_LIMIT", "state": "CONFLICT"}],
                    "readiness": {"physical_authorization": "BLOCKED", "system_identification": "CONFLICT"}}
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--snapshot", type=Path, help="offline captured filesystem root")
    mode.add_argument("--live-read-only", action="store_true", help="operator-authorized Linux read-only inspection")
    args = parser.parse_args()
    if args.live_read_only and not sys.platform.startswith("linux"):
        parser.error("live inspection requires Linux")
    if args.snapshot and args.snapshot.resolve() == Path("/"):
        parser.error("offline root cannot be /; live mode requires explicit operator invocation")
    try:
        result = Inspector(args.snapshot if args.snapshot else "/", args.live_read_only).run()
    except (OSError, EvidenceError) as e:
        parser.error(str(e))
    print(json.dumps(result, sort_keys=True, indent=2))
    return 2 if result["readiness"]["system_identification"] == "CONFLICT" else 0


if __name__ == "__main__":
    sys.exit(main())

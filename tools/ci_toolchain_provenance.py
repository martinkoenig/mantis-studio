"""Capture/verify bounded configured CMake identities, never invocation attestation.

No subprocesses, network, PATH lookup, Git acquisition or import-time I/O.
See docs/engineering/configured-toolchain-provenance.md for the v1 contract.
"""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
from typing import NoReturn

SCHEMA = "configured-toolchain-v1"
UNVERIFIED = ["actual-tool-invocation", "source-consumed", "dependency-closure",
              "package-authenticity", "performance", "release-provenance", "os-isolation"]


class Invalid(ValueError):
    """A bounded, public-safe primary diagnosis (never contains input values)."""


def fail(code) -> NoReturn:
    raise Invalid(code)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path, limit, code):
    # NONBLOCK prevents untrusted FIFOs from hanging before the regular-file check.
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
                fail(code + "-size-or-type")
            data = stream.read(limit + 1)
            if len(data) > limit:
                fail(code + "-size-or-type")
            return data
    except OSError:
        fail(code + "-unavailable")


def fingerprint(path, limit, code):
    return sha(read(path, limit, code))


def pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            fail("malformed-json-duplicate-key")
        result[key] = value
    return result


def decode(data, code):
    try:
        return json.loads(data, object_pairs_hook=pairs,
                          parse_constant=lambda _: fail(code))
    except (ValueError, UnicodeError, RecursionError):
        fail(code)


def root(value):
    path = Path(value)
    if not path.is_absolute():
        fail("root-must-be-absolute")
    try:
        path = path.resolve(strict=True)
        if not path.is_dir():
            fail("root-unavailable")
        return path
    except (OSError, RuntimeError):
        fail("root-unavailable")


def path_identity(path):
    return sha(os.fsencode(str(path)))


def tool(value):
    if not isinstance(value, str) or not Path(value).is_absolute() or ";" in value:
        fail("unsupported-tool-path")
    try:
        resolved = Path(value).resolve(strict=True)
        if not os.access(resolved, os.X_OK):
            fail("configured-tool-unavailable")
    except OSError:
        fail("configured-tool-unavailable")
    return {"configured_path_sha256": path_identity(value),
            "resolved_path_sha256": path_identity(resolved),
            "sha256": fingerprint(resolved, 256 * 1024 * 1024, "configured-tool")}


def token(value, code):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_.+-]{1,80}", value):
        fail(code)
    return value


def cache_values(data):
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeError:
        fail("malformed-cache")
    result = {}
    for line in lines:
        if not line or line.startswith(("//", "#")):
            continue
        match = re.fullmatch(r"([^:=]+):([^=]+)=(.*)", line)
        if not match or match[1] in result:
            fail("malformed-cache")
        result[match[1]] = match[3]
    return result


def input_names(values):
    if not 1 <= len(values) <= 128 or len(set(values)) != len(values):
        fail("invalid-input-list")
    for name in values:
        path = Path(name)
        if (len(name) > 200 or not re.fullmatch(r"[A-Za-z0-9_./ +\-]+", name)
                or path.is_absolute() or ".." in path.parts or str(path) != name):
            fail("invalid-input-list")
    if "CMakeLists.txt" not in values:
        fail("missing-cmakelists-input")
    return sorted(values)


def configured_file(value):
    if not isinstance(value, str) or not Path(value).is_absolute():
        fail("unsupported-toolchain-file-path")
    return {"path_sha256": path_identity(value),
            "sha256": fingerprint(value, 16 * 1024 * 1024, "toolchain-file")}


def snapshot(args):
    source, build = root(args.source), root(args.build)
    if source == build or source.is_relative_to(build):
        fail("unsupported-root-layout")
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", args.revision):
        fail("invalid-revision")
    names = input_names(args.input)
    cache_data = read(build / "CMakeCache.txt", 8 * 1024 * 1024, "cache")
    cache = cache_values(cache_data)
    try:
        if (not Path(cache["CMAKE_HOME_DIRECTORY"]).is_absolute()
                or not Path(cache["CMAKE_CACHEFILE_DIR"]).is_absolute()
                or Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != source
                or Path(cache["CMAKE_CACHEFILE_DIR"]).resolve() != build):
            fail("configured-root-mismatch")
        reply = build / ".cmake/api/v1/reply"
        indexes = list(itertools.islice(reply.glob("index-*.json"), 2))
        if len(indexes) != 1:
            fail("missing-or-ambiguous-file-api-index")
        index_data = read(indexes[0], 8 * 1024 * 1024, "file-api-index")
        index = decode(index_data, "malformed-file-api-index")
        entry = index["reply"]["toolchains-v1"]
        if entry["kind"] != "toolchains" or entry["version"] != {"major": 1, "minor": 0}:
            fail("unsupported-toolchains-version")
        filename = entry["jsonFile"]
        if not isinstance(filename, str) or not re.fullmatch(r"toolchains-v1-[a-z0-9]+\.json", filename):
            fail("unsupported-file-api-reference")
        chain_data = read(reply / filename, 8 * 1024 * 1024, "toolchains")
        chains = decode(chain_data, "malformed-toolchains")
        if chains["kind"] != "toolchains" or chains["version"] != {"major": 1, "minor": 0}:
            fail("unsupported-toolchains-version")
        compilers = {}
        for chain in chains["toolchains"]:
            lang = chain["language"]
            if lang not in ("C", "CXX") or lang in compilers:
                fail("unsupported-toolchain-language")
            compiler = chain["compiler"]
            if compiler["path"] != cache[f"CMAKE_{lang}_COMPILER"]:
                fail("compiler-metadata-mismatch")
            compilers[lang] = {"id": token(compiler["id"], "unsupported-compiler-id"),
                               "version": token(compiler["version"], "unsupported-compiler-version"),
                               "file": tool(compiler["path"]),
                               "origin": "cmake-file-api:toolchains-v1"}
        if set(compilers) != {"C", "CXX"}:
            fail("missing-c-or-cxx-toolchain")
        cmake = index["cmake"]
        generator = cmake["generator"]["name"]
        if (generator not in ("Ninja", "Unix Makefiles") or cmake["generator"]["multiConfig"] is not False
                or generator != cache["CMAKE_GENERATOR"]):
            fail("unsupported-or-inconsistent-generator")
        if cmake["paths"]["cmake"] != cache["CMAKE_COMMAND"]:
            fail("cmake-metadata-mismatch")
        build_type = cache["CMAKE_BUILD_TYPE"]
        if build_type not in ("", "Debug", "Release", "RelWithDebInfo", "MinSizeRel"):
            fail("unsupported-build-type")
        settings = [f"CMAKE_{lang}_FLAGS{suffix}" for lang in ("C", "CXX")
                    for suffix in ("", "_DEBUG", "_RELEASE", "_RELWITHDEBINFO", "_MINSIZEREL")]
        settings += ["CMAKE_EXE_LINKER_FLAGS", "CMAKE_SHARED_LINKER_FLAGS",
                     "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET"]
        configured = {"generator": generator, "build_type": build_type,
                      "settings_sha256": {key: sha(cache[key].encode()) if key in cache else None
                                          for key in settings},
                      "toolchain_file": configured_file(cache["CMAKE_TOOLCHAIN_FILE"])
                      if cache.get("CMAKE_TOOLCHAIN_FILE") else None,
                      "compilers": compilers,
                      "cmake": {"version": token(cmake["version"]["string"], "unsupported-cmake-version"),
                                "file": tool(cmake["paths"]["cmake"]), "origin": "cmake-file-api:index"},
                      "build_tool": {"file": tool(cache["CMAKE_MAKE_PROGRAM"]),
                                     "version": "UNVERIFIED", "origin": "cmake-cache:CMAKE_MAKE_PROGRAM"},
                      "launchers": {lang: tool(cache[f"CMAKE_{lang}_COMPILER_LAUNCHER"])
                                    if cache.get(f"CMAKE_{lang}_COMPILER_LAUNCHER") else None
                                    for lang in ("C", "CXX")}}
    except (KeyError, TypeError, AttributeError):
        fail("missing-or-malformed-configured-metadata")
    inputs = {}
    total = 0
    for name in names:
        try:
            path = (source / name).resolve(strict=True)
            if not path.is_relative_to(source):
                fail("input-root-escape")
        except OSError:
            fail("source-input-unavailable")
        data = read(path, 16 * 1024 * 1024, "source-input")
        total += len(data)
        if total > 64 * 1024 * 1024:
            fail("source-input-total-limit")
        inputs[name] = sha(data)
    # All raw metadata is fingerprinted, but arbitrary flags, paths, implicit
    # includes and cache variables are never copied into the public representation.
    return {"schema": SCHEMA,
            "binding": {"source_root_sha256": path_identity(source),
                        "build_root_sha256": path_identity(build)},
            "context": {"revision": args.revision, "dirty": args.dirty, "origin": "caller-declared"},
            "inputs": inputs, "configured": configured,
            "observed": {"cache_sha256": sha(cache_data), "index_sha256": sha(index_data),
                         "toolchains_sha256": sha(chain_data)},
            "unverified": UNVERIFIED}


def publish(path, data):
    # A complete private temporary is linked into place with no-replace semantics.
    # Cleanup failures must not replace the initiating write/link error.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".provenance-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, path)
    except FileExistsError:
        fail("report-exists")
    except OSError:
        fail("report-publication-failed")
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except OSError:
                pass


def verify(args):
    data = read(Path(args.report), 1024 * 1024, "report")
    expected = decode(data, "malformed-report")
    if not isinstance(expected, dict) or "schema" not in expected:
        fail("malformed-report")
    if expected["schema"] != SCHEMA:
        fail("unsupported-report-schema")
    if not re.fullmatch(r"[0-9a-f]{64}", args.report_sha256):
        fail("invalid-report-digest")
    if sha(data) != args.report_sha256:
        fail("report-digest-mismatch")
    current = snapshot(args)
    if set(expected) != set(current):
        fail("report-shape-mismatch")
    for section, code in (("binding", "root-binding-mismatch"),
                          ("context", "stale-revision-or-dirty-context"),
                          ("inputs", "source-input-mismatch"),
                          ("configured", "configured-tool-or-setting-mismatch"),
                          ("observed", "configuration-fingerprint-mismatch"),
                          ("unverified", "report-claims-mismatch")):
        if expected[section] != current[section]:
            fail(code)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    for mode in ("capture", "verify"):
        command = modes.add_parser(mode)
        for name in ("source", "build", "revision", "report"):
            command.add_argument("--" + name, required=True)
        command.add_argument("--dirty", required=True, choices=("clean", "dirty"))
        command.add_argument("--input", required=True, action="append")
        if mode == "verify":
            command.add_argument("--report-sha256", required=True)
    args = parser.parse_args(argv)
    try:
        if args.mode == "capture":
            path = Path(args.report)
            if path.exists() or path.is_symlink():
                fail("report-exists")
            data = (json.dumps(snapshot(args), sort_keys=True, indent=2) + "\n").encode()
            publish(path, data)
            print(sha(data))
        else:
            verify(args)
            print("VERIFIED " + SCHEMA)
        return 0
    except Invalid as error:
        print(str(error), file=sys.stderr)
        return 2
    except (OSError, ValueError, RecursionError):
        print("invalid-or-unavailable-input", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

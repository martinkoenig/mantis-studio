"""Small Actions adapter; caches compiler artifacts, never build/test results."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import time


SCHEMA = "ccache-v1"
CONFIG = {
    "max_size": "384Mi", "compiler_check": "content", "hash_dir": "true",
    "sloppiness": "", "base_dir": "", "remote_storage": "",
}


def prefix(system, arch, studio, sanitizer):
    if system != "Linux" or arch not in ("X64", "ARM64"):
        raise ValueError("Unsupported cache platform")
    if studio not in ("ON", "OFF") or sanitizer not in ("ON", "OFF"):
        raise ValueError("Explicit Studio/sanitizer modes required")
    return f"{SCHEMA}-{system}-{arch}-studio-{studio}-sanitize-{sanitizer}-"


def append_output(path, values):
    with Path(path).open("a") as stream:
        for key, value in values.items():
            if "\n" in str(value) or "\r" in str(value):
                raise ValueError("Multiline workflow output rejected")
            stream.write(f"{key}={value}\n")


def snapshot_key(namespace, instant):
    return namespace + instant.astimezone(datetime.timezone.utc).strftime("%G-W%V")


def prepare(studio, sanitizer):
    # Do not inherit tuning that can override the isolated configuration file.
    if any(key.startswith("CCACHE_") for key in os.environ):
        raise ValueError("Unexpected preconfigured CCACHE environment")
    namespace = prefix(os.environ["RUNNER_OS"], os.environ["RUNNER_ARCH"], studio, sanitizer)
    root = Path(os.environ["RUNNER_TEMP"]).resolve()
    directory = root / "mantis-ccache"
    config = root / "mantis-ccache.conf"  # Outside the restored directory.
    config.write_text("".join(f"{key} = {value}\n" for key, value in
                              (CONFIG | {"namespace": namespace}).items()))
    append_output(os.environ["GITHUB_ENV"], {
        "CCACHE_DIR": directory, "CCACHE_CONFIGPATH": config,
        "CCACHE_TEMPDIR": root / "mantis-ccache-tmp",
    })
    # One immutable snapshot/configuration/trusted branch/UTC ISO week.
    append_output(os.environ["GITHUB_OUTPUT"], {
        "directory": directory, "prefix": namespace,
        "key": snapshot_key(namespace, datetime.datetime.now(datetime.timezone.utc)),
    })


def reset():
    subprocess.run(["ccache", "--version"], check=True)
    for key in (*CONFIG, "namespace"):
        value = subprocess.check_output(["ccache", "--get-config", key], text=True).strip()
        print(f"{key}={value}", flush=True)
        expected = CONFIG.get(key)
        # The Noble 4.9.1 CLI formats the configured binary size for display.
        if expected is not None and value != ("384.0 MiB" if key == "max_size" else expected):
            raise ValueError(f"Unsafe effective ccache option: {key}")
    subprocess.run(["ccache", "--zero-stats"], check=True)


def build(command):
    if not command:
        raise ValueError("Build command required")
    started = time.monotonic()
    try:
        result = subprocess.run(command)
    finally:
        append_output(os.environ["GITHUB_OUTPUT"], {
            "seconds": f"{time.monotonic() - started:.3f}",
        })
    return result.returncode if result.returncode >= 0 else 128 - result.returncode


def summary():
    stats = subprocess.check_output(["ccache", "--show-stats", "--verbose"], text=True)
    state = {name: os.environ.get(name, "unavailable") for name in (
        "CACHE_RESTORE", "CACHE_EXACT_HIT", "CACHE_SAVE", "BUILD_SECONDS")}
    text = ("### Compiler cache (current job counters)\n\n" + json.dumps(state) +
            "\n\n```text\n" + stats + "```\n\n"
            "Build seconds include the compiler launcher, not cache transfer or tests. "
            "Restore/save step durations are available in the Actions timeline; "
            "no end-to-end speedup is inferred here.\n")
    print(text, end="")
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as stream:
        stream.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="mode", required=True)
    prep = commands.add_parser("prepare")
    prep.add_argument("--studio", choices=("ON", "OFF"), required=True)
    prep.add_argument("--sanitizer", choices=("ON", "OFF"), required=True)
    commands.add_parser("reset")
    commands.add_parser("trim")
    compile_command = commands.add_parser("build")
    compile_command.add_argument("command", nargs=argparse.REMAINDER)
    commands.add_parser("summary")
    args = parser.parse_args()
    if args.mode == "prepare":
        prepare(args.studio, args.sanitizer)
    elif args.mode == "reset":
        reset()
    elif args.mode == "build":
        return build(args.command)
    elif args.mode == "trim":
        subprocess.run(["ccache", "--cleanup"], check=True)
    else:
        summary()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

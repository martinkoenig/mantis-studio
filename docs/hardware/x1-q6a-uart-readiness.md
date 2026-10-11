# Q6A UART readiness and PTY serial policy — L7b-2b-2

**IMPLEMENTED / AWAITING INDEPENDENT REVIEW.** Predecessor L7b-2b-1 is
FINAL ACCEPTED at `6489c6eec05ac61f685f71df5805cc825a912b2d`, with all five
required CI jobs successful. Its [transport validation and asynchronous authority
fencing](x1-f2-posix-transport.md) remain unchanged.
L7b-2b-3 is PLANNED / BLOCKED BY PHYSICAL QUALIFICATION.

The firmware documents were read through the GitHub read-only API at
`ec1dff2ae94f08dd309b65511dafe49eb5e9b525`. This includes the inactive
F4b-2 adapter; source availability and successful firmware CI do not authorize
activation. The authoritative physical handoff remains the firmware
[F1b checklist](https://github.com/martinkoenig/mantis-x1-firmware/blob/ec1dff2ae94f08dd309b65511dafe49eb5e9b525/docs/validation/f1b-uart-interface-checklist.md),
[Q6A interface assessment](https://github.com/martinkoenig/mantis-x1-firmware/blob/ec1dff2ae94f08dd309b65511dafe49eb5e9b525/docs/hardware/q6a-esp8266-uart-interface.md),
and [supervised prototype policy](https://github.com/martinkoenig/mantis-x1-firmware/blob/ec1dff2ae94f08dd309b65511dafe49eb5e9b525/docs/adr/0002-supervised-prototype-safety-scope.md).
The [frozen UART contract](https://github.com/martinkoenig/mantis-x1-firmware/blob/ec1dff2ae94f08dd309b65511dafe49eb5e9b525/shared/protocol/UART_V1_0_CONTRACT.md)
is unchanged. Historical E2 live interoperability still uses its separately
pinned accepted `92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8` sources.

## Inspector and evidence

`tools/x1_uart_readiness.py` uses Python 3 standard library and Linux checked-file
facilities. It imports no serial library, executes no commands, opens no TTY,
and starts no camera. Offline mode is exercised by synthetic CI snapshots:

```bash
python3 tools/x1_uart_readiness.py --snapshot /path/to/captured-system-root > readiness.json
python3 tests/unit/x1_uart_readiness.py
```

A snapshot root supplies only relevant captured `sys/`, `proc/`, `etc/`, optional
`run/` and `evidence/` data. The tests construct these in temporary directories;
no Q6A snapshot is supplied or fabricated. Absolute sysfs links resolve within
the captured root. Generic DT absent `status` means available, including parents;
this never proves driver binding, active pinmux or electrical levels. Unreadable
status fails inspection rather than receiving the absent-property default.

The inspector locates the candidate `serial@a90000` in runtime DT, decodes
NUL-terminated string lists and big-endian cells, resolves serial aliases and
pinctrl phandles, checks parent status, and identifies enabled competing
`spi@a90000` / `i2c@a90000` nodes and shared pinctrl references. Clock/reset/power
provider argument widths are interpreted only where declared; missing bindings
remain Unknown. Duplicate handles, malformed cells, aliases and string lists
fail explicitly. Candidate address and compatible strings are retained as
identification evidence, not physical qualification.

TTY names are returned only after sysfs `device` ancestry reaches the candidate's
`of_node`; a unique mapping also requires a bound driver. Multiple candidates,
multiple matching TTYs and alias ambiguity are conflicts. Enumeration order and
`/dev/tty` naming conventions never determine identity. An unrelated mapped
controller cannot establish the intended UART.

Relevant existing `pinmux-pins` / `pinconf-pins` lines for GPIO50/GPIO51 are
retained, bounded to 512 characters per line. Their driver-specific text requires
review, so the tool reports Unknown even when present. Missing/debugfs permission
gaps are Unavailable. Header pins 23/24 and GPIO50/GPIO51 are published lookup
candidates, **not wiring instructions**. No debugfs mount, pin configuration,
permission change or privilege escalation is attempted.

Console command-line assignments, `/proc/consoles`, configured getty units in
known systemd directories, and explicitly captured getty state are inspected.
Early-console configuration is a conservative review conflict. A missing getty
entry or empty consumer inventory never proves exclusive ownership. Runtime
process exclusivity remains Unknown. Obvious password/token/secret parameters
in the captured command line are redacted; operators must still privacy-review
reports before committing them.

OV9281 inventory records distinct sensor paths, explicit LEFT/RIGHT roles,
parent availability, reciprocal remote endpoints, dependency cells and available
sysfs driver/media registrations. Roles derive from captured `mantis,role` or
explicit operator metadata, never enumeration order. Media pipeline correlation
remains Unknown without reviewed topology; sensor inventory alone cannot prove
both complete pipelines. The [accepted Q6A camera record](x1-q6a-acquisition-validation.md)
is historical evidence; this tool does not repeat capture or performance tests.

Optional `evidence/metadata.json` has this minimal form:

```json
{
  "capture_timestamp": "operator-supplied UTC capture time",
  "captured_boot_id": "matching captured proc/sys/kernel/random/boot_id",
  "selected_boot_entry": "operator-supplied selected-entry identifier",
  "camera_roles": {"/soc/camera@1": "left", "/soc/camera@2": "right"},
  "artifacts": ["evidence/artifacts/selected-entry.txt", "evidence/artifacts/candidate.dtbo"]
}
```

Supplied artifacts are hashed only at explicitly listed, contained artifact paths.
Their presence or a selected-entry assertion cannot establish active boot-chain
selection. Boot-ID mismatch is a conflict; matching offline evidence still does
not prove atomic capture or a coherent current boot. Missing boot provenance
remains Unknown. No unrelated boot/user directory crawl occurs.

The JSON schema-1 report contains tool identity, mode, optional live timestamp,
platform/kernel/DT model, controller/aliases/TTYs, pinmux, camera inventory,
evidence sources, stable finding codes, missing evidence, blockers and resource
counts. Findings retain `ESTABLISHED`, `UNKNOWN`, `UNAVAILABLE`, or `CONFLICT`
and source provenance (`offline_snapshot`, `live_read_only`,
`operator_supplied_artifact`, `repository_configuration`, or published reference).
Examples include `UART_DT_DISABLED`, `TTY_MAPPING_AMBIGUOUS`,
`SERIAL_ENGINE_CONFLICT`, `PINCTRL_OWNER_CONFLICT`, `PINMUX_UNAVAILABLE`,
`CONSOLE_GETTY_CONFLICT`, `CAMERA_MEDIA_CORRELATION` and `BOOT_ID_MISMATCH`.
Malformed/unreadable evidence and resource-limit errors are explicit.

Separate readiness conclusions are `PTY_VALIDATED_ONLY` for software policy,
`IDENTIFIED_WITH_GAPS` / `INCOMPLETE` / `CONFLICT` for system identification,
and **always `BLOCKED` for physical authorization**. No flag, environment
variable or report input can override the physical gate. Exit 2 indicates
conflicting/invalid inspection; exit 0 merely means the report was produced
without identified conflicts, and grants no readiness or authorization.

A future separately authorized operator may invoke
`python3 tools/x1_uart_readiness.py --live-read-only` on Q6A, without a connected
UART peer. This package did **not** invoke live mode, SSH into Q6A, or inspect
physical serial devices. The mode is explicit, Linux-only and read-only; missing
evidence remains a gap. No services, overlays, SPI12, pinmux or kernel settings
are modified. Final review must correlate the operator's boot, runtime DT,
controller/TTY, pin ownership and camera evidence with F1b's electrical records.

## Bounded reads and security

Only documented system locations and explicitly supplied artifact paths are
read. Limits are 256 KiB per file, 8 MiB total reads, 12,000 enumerated entries,
32 path components, 16 symlink resolutions per path and 256 KiB JSON report.
Oversized evidence fails explicitly. Directory enumeration uses bounded scandir;
report overflow returns a compact conflict with physical authorization Blocked.

Root-contained link resolution is followed by component-wise `O_NOFOLLOW`
opening. The final inode is pinned using `O_PATH`, checked as regular file or
directory, and reopened through the inspector's own `/proc/self/fd` reference.
A special file is rejected without opening it for I/O. Concurrent name changes
cannot redirect a read through a newly introduced link or device. Links cannot
redirect system inventory into unrelated captured home directories. The
standard `/etc/os-release` to `/usr/lib/os-release` relocation is allowed.
Artifact traversal and scope escapes are rejected, including within the root.
No credentials, home trees, USB manufacturing serials or kernel-log dumps are
collected. The synthetic tests cover bounds, permission denial, cycles, escapes,
malformed references, special files, privacy redaction and permanent physical gates.

## Serial settings and ownership

`plugins/first-party/devices/x1/f2-serial/serial-settings.{hpp,cpp}` is a separate
Linux-only private module next to the accepted no-termios `f2-posix` transport.
It is built exclusively from `tests/pty/x1-f2` under BUILD_TESTING, uninstalled,
and unlinked from the production X1 plugin, daemon, SDK, portable Host and
projected fixture. Keeping it separate preserves all existing FD transport
source/binary exclusions without weakening their tests.

`configure_serial()` borrows an exclusively owned, already-open TTY FD. It never
opens, duplicates, retains or closes it. Caller serializes configuration against
I/O and close, verifies success, then transfers that same FD to `FdTransport`.
The transport's O_NONBLOCK/CLOEXEC changes preserve termios. There is no device
selector, discovery, backend switch or activation composition in production.

Only 115200 baud, 8 data bits, no parity, one stop bit, raw byte mode, no echo,
no software/hardware flow control, CREAD/CLOCAL and VMIN=1/VTIME=0 are supported.
OPOST, canonical input, signals and relevant input transformations are disabled.
No 460800 fallback, auto-baud, flush or synchronous drain exists. Existing HUPCL
is preserved; that is not an electrical reset/line-state guarantee.

The component saves original termios, applies with TCSANOW and checks effective
readback. Application/readback failure attempts rollback and verifies restoration.
Initiating errno and rollback errno are separate. A disappeared descriptor/peer
or external concurrent configuration may make rollback uncertain; callers must
not proceed on any failure. Tests use link-time syscall fault injection only in
the executable, exercising partial application, readback failure and failed
rollback while the module has no test hooks or fault globals.

Normal configuration uses fixed stack values and three terminal syscalls
(original read, application, readback), with no allocation, queues, workers,
byte processing or image copies. Existing FD/Host bounds, STOP containment,
heartbeat, cancellation, disconnect and shutdown remain unchanged. Synthetic
controller readiness is never physical qualification.

## Verification and gaps

The new `x1-uart-readiness`, `x1-f2-serial-settings` and
`x1-uart-readiness-boundaries` suites cover T01–T22. Existing PTY R1 abandonment,
partial-write, priority STOP, heartbeat and lifetime regressions remain mandatory.
The serial suite exchanges all 256 byte values in both directions, checks no
echo/transformation, rejects every unsupported profile field, checks effective
readback after ownership transfer, and performs 12 complete framed Host lifetimes
with the unchanged synthetic controller. It checks partial reads/writes, EINTR,
zero-budget timeout, lane-specific cancellation, independent STOP and PTY HUP.
No physical MCU, firmware or controller semantics are changed.

```bash
ctest --test-dir build/debug -R 'x1-(uart-readiness|f2-serial-settings|f2-pty)' --output-on-failure
ctest --test-dir build/debug -R 'x1-(f2-pty|f2-serial-settings)' --repeat until-fail:100 --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/l2-sanitized -R 'x1-(f2-pty|f2-serial-settings)' --repeat until-fail:100 --output-on-failure
```

Final local evidence: Linux x86_64, kernel 6.17.0-41-generic, GCC 15.2.0,
Debug builds using the established dependency caches. All full suites include
F2/E1/E2, L7a, camera/pairing, L3–L6, recording/replay, ABI/catalog and storage
regressions. OFF and sanitizer builds additionally ran the opted-in exact-SHA
E2 live firmware conformance target; Studio ON and non-test builds required no
firmware source.

| Check | Result |
| --- | --- |
| Complete Studio ON build / CTest | PASS; 138/138, 133.68 s |
| Complete Studio OFF build / CTest | PASS; 136/136, 119.84 s |
| Complete ASan/UBSan build / CTest | PASS; 136/136, 324.12 s; leak detection and fatal errors enabled |
| BUILD_TESTING=OFF complete build | PASS; portable codec/Host present, no serial/FD/simulation/fixture targets, zero tests, missing firmware path permitted |
| Synthetic inspector | PASS; 17 deterministic cases (T01–T14), including resource/security/error classifications |
| Normal PTY / abandonment / serial repetition | PASS; 100 repetitions each, 300 runs; serial suite contains 12 Host/FD lifetimes per run |
| Sanitized PTY / abandonment / serial repetition | PASS; 100 repetitions each, 300 runs; 79.71 s |
| Repository / existing and new source/binary boundaries | PASS; original no-termios FD boundary retained |
| Frozen L2 catalogs | PASS; 129 structs, 816 fields, 113 constants, 22 IDs |
| C++ clang-format / whitespace / local documentation links | PASS |
| Native ARM64 (T23/V15) | Not executed locally; required independent remote CI gate |

Reproduction used `cmake --preset linux-debug`, `cmake --build build/debug --parallel 4`
and `ctest --test-dir build/debug --output-on-failure`. Existing headless caches
were configured with `cmake -S . -B build/l2 -DMANTIS_BUILD_STUDIO=OFF` and
`cmake -S . -B build/l2-sanitized -DMANTIS_BUILD_STUDIO=OFF -DMANTIS_SANITIZE=ON`,
then built completely and tested with `ctest --test-dir <build> --output-on-failure`.
Python is `/usr/bin/python3`; OpenCV uses `build/l2-deps/opencv`.
The existing test environment sets `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`
and `OPENCV_OPENCL_RUNTIME=disabled`. Sanitizers set
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`.
The complete non-test cache `build/l7b2a-no-tests` has BUILD_TESTING=OFF,
MANTIS_BUILD_STUDIO=OFF and a missing firmware source path.

The inspector benchmark ran 100 identical two-camera synthetic inspections
with Python tracemalloc enabled: 18.60 ms per inspection, 1,962,003 bytes peak
traced memory across the run, 177 evidence bytes read / 41 entries per inspection,
and 7,713-byte JSON. These observations concern a small generated snapshot,
include instrumentation overhead, and are not maximum-resource budgets or Q6A
performance. The serial policy's measured x86_64 sizes are SerialProfile 12 bytes,
SerialResult 16 bytes and termios 60 bytes; there is no per-instance heap state.
The library binary contains no allocation symbols. It introduces no I/O-path
queue, worker or data-plane copy. PTY/Host allocation and latency evidence remains
in the accepted predecessor record because transport/Host code is unchanged.

T15–T20 are exercised by `x1-f2-serial-settings`; T21 by the unchanged accepted
PTY abandonment regressions; T22 by old and new source/binary contracts;
T24 by the complete matrix. Native ARM64 is the independent remote CI gate;
no local ARM64 execution is claimed.
Actual Q6A readiness remains UNKNOWN / UNVERIFIED. Missing board revision,
boot/overlay provenance, live pinmux/TTY/camera evidence, voltage/loading,
USB-bridge ownership, boot-strap/reset behavior, ground/power-state compatibility,
and separately reviewed inert-link authorization remain F1b handoff gaps.
No electrical/optical qualification, physical UART, GPIO, MCU reset/flash,
laser operation, independently verified inhibit, trigger or physical OFF was
implemented or tested. Physical controller integration remains blocked.

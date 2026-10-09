# L6 implementation validation

L0–L5 are FINAL ACCEPTED. L6 is implemented, acceptance pending; L7–L8 remain
planned. This record describes hardware-free infrastructure verification. It does
not certify optical measurements, scanner throughput or physical laser safety.

## Reproducible checks

The four additive CTest entries are `laser-observation-codec`, `processor-v2`,
`laser-observation-artifact` and `semantic-pipeline-replay`. The complete existing
non-hardware suite also runs unchanged. The C-only contract DSO compiles against
the unchanged public headers; native and actual plugin-host execution are tested.

The codec test compares six checked-in independent binary fixtures in both
directions. The Python reference encoder refuses to overwrite fixtures and is not
run by the tests. Existing legacy and projected storage goldens are unchanged.
Every truncated fixture prefix and every single-byte corruption is rejected;
valid-checksum malformed context/columns/masks/attribution are tested separately.

The integration tests exercise one-emission publication, invalid ABI tables and
descriptors (including a changed registered output type), callback failures,
independent output headers, retained/transferred
buffer ownership, eight concurrent invocations, isolated crash/timeout/cancellation,
scratch cleanup, explicit V1/V2 mixing, journal corruption and process death.
An actual DSO advertising both optional processor tables verifies explicit V1/V2
selection. A weak loader-owner assertion proves retained columns pin the producing
library and release that pin afterward, independently of loader NODELETE behavior.
Streaming uses 2/2 lossless queues for 64 publications, checks exact order and no
drops, and tests cancellation and failed-node queue closure. Successful N=0,
extractor_failed and extractor_unavailable remain distinct.

Two independent BundleReplay passes through the synthetic V2 producer and typed
consumer produce identical canonical observation bytes. An isolated pass agrees
with native execution. New observation artifacts retain the actual finalized raw
artifact/hash and original bundle/frame/evidence/calibration identities; the raw
capture remains unchanged. Mapped bulk backing is shared and survives Store
destruction. No active-calibration or hardware lookup is involved.

Local commands use `git diff --check`, `python3 tests/contract/boundaries.py .`,
the full Debug Studio-OFF CTest suite, repeated concurrency/isolation tests, and
the complete ASan/UBSan suite with leak detection. On this development host,
`OPENCV_OPENCL_RUNTIME=disabled` selects the CPU calibration path because its
external OpenCL ICD is unsuitable for sanitizer runs; no repository test is skipped.

## Measured performance

`mantis-observation-benchmark SYNTHETIC_DSO` measures N=0, 2, 4096 and 65,536.
It reports serialization/decode/native V2/pipeline times, allocation counts and
bytes, streaming throughput, queue high water and cancellation latency. These are
local Debug CPU fixture measurements; native V2 cost includes fixture mapping,
semantic ABI conversion and validation, rather than just a function-pointer call.

The initial allocation measurement exposed eager validator diagnostic-string
construction: a 65,536-sample V2 call performed over three million allocations.
Constructing the same diagnostic only on failure removed those per-sample
allocations without changing any validation rule. Metadata allocations are now
approximately constant across small, medium and larger sample counts. Structural
sizing reads buffer extents without traversing payloads. Mapped decoding and native
views perform no application-level bulk copy. Isolated transport explicitly writes
and maps files, with the checksum passes documented in the format specification.

One local x86_64 Debug run (GCC 15.2, AMD Ryzen AI 9 HX 370) measured:

| Samples | Encoded bytes | Serialize µs | MiB/s | Mapped decode µs | Native synthetic V2 µs | Pipeline µs | V2 allocations |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 3,336 | 350 | 9.1 | 395 | 2,091 | 2,164 | 1,361 |
| 2 | 4,100 | 295 | 13.3 | 382 | 2,159 | 2,378 | 1,785 |
| 4,096 | 114,638 | 1,320 | 82.9 | 1,383 | 4,676 | 5,059 | 1,786 |
| 65,536 | 1,773,518 | 10,335 | 163.6 | 10,549 | 29,307 | 54,903 | 1,786 |

Serialization allocates 129 metadata objects for every nonempty size above; mapped
decode allocates 340. Native V2 allocates approximately 95.6 KiB of metadata, not
1.77 MiB of columns, at the larger size. Pipeline allocations remain at 2,153.
Observed streaming rates were 456, 353, 86 and 17 observations/s respectively;
input/output high water was 2/1 with zero drops. Cancellation woke in 436 µs.
These single-host measurements vary with load and do not define acceptance timing
thresholds. An optional C pass-through DSO argument separately measures native
adapter overhead without fixture production.

The C pass-through baseline measured 44.8 µs, 110 allocations and 9.5 KiB per
native adapter call. It includes complete input/output validation and buffer
ownership conversion. It is not a bare function-pointer timing.

Final local validation passed:

- complete Debug Studio-OFF CTest: **52/52**, 117.83 seconds;
- complete ASan/UBSan CTest, leak detection and halt-on-error: **52/52**, 327.00 seconds;
- final ProcessorV2 repeat: five successful native runs and five sanitizer runs;
- semantic stream repeat: ten native runs and five sanitizer runs;
- architecture boundaries and `git diff --check`.

Those final tests include changed descriptors, explicit dual-interface selection,
oversized files, journal accounting and loader-independent buffer lifetime checks.
No test or assertion was disabled. Frozen ABI snapshots, legacy/projected storage
fixtures, L3 sequencing and L5 control DTOs remain unchanged. Required GitHub
acceptance is the five-job matrix for the exact final pushed SHA; the completion
report records that run and every job conclusion. Green local tests alone do not
establish CI acceptance. Human L6 acceptance remains pending.

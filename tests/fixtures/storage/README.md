# Immutable storage compatibility bytes

The legacy fixtures were independently assembled from the published field order
and alignment before L4 codec edits. They are checked-in expected bytes, never
produced by the codec at test runtime. MANTIS01 is a four-byte image; MANTIS02
contains that image; MRAWREC2 frames that exact FrameSet. Scalar u8 is legacy tag 1.
Do not bless a mismatch by replacing these files.


The v3 bytes were independently constructed from the explicit version-1 byte
specification. reference_v3.py documents their derivation and refuses overwrites;
it is not invoked by CMake/CTest. The C++ fixture values are independently declared
in tests/unit/projected_storage_values.hpp. Four bundles cover no-image presence,
full captured emitter/exposure/calibration state, three trigger stages and an
explicit failed terminal. run-header3.bin contains a complete program/configuration;
mrawrec3.bin frames the exact evidence-only bundle. Expected data is immutable.

The follow-up run-outcome3.bin independently freezes the final daemon outcome
sidecar (MRUNOUT3/MOUTEND3), including full AbortOutcome presence and emitter
semantics. Its run_outcome() derivation is separate from all existing fixtures;
no previous fixture bytes changed. It does not stand in for an executor bundle.

During final hardening, only the acceptance-pending run-outcome3.bin was corrected:
the four outer errors and abort outcome use optional tags 0 absent / 1 present.
AbortOutcome internal evidence still uses 0 Unknown / 1 Unavailable / 2 Established.
The independent run_outcome() assembler specifies these corrected bytes. All nine
previous fixtures, including MRUNHDR3 and MRAWREC3, remain unchanged.

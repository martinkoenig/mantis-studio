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

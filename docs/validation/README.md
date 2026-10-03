# Validation evidence

These logs record the final process tests of the delivered source build.

- `debug-ctest.log`: desktop, actual Qt shell and viewport, 4/4 passed.
- `headless-ctest.log`: Qt-free runtime and clients, 4/4 passed.
- `sanitizers-ctest.log`: ASan/UBSan, 4/4 passed with `detect_leaks=0` in the resumed execution environment.
- `leaksanitizer-environment-limitation.log`: separate failed LeakSanitizer startup due to restricted process inspection. This is retained rather than represented as a passing leak test.

The full conditions and platform scope are in [the validation report](../architecture/validation.md). `<SOURCE_ROOT>` replaces the transient workspace location in the logs; test output and results are unchanged.

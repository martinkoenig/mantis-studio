These three public C headers are the final L2 ABI snapshot. They are intentional
compatibility evidence, not normative documentation or implementation headers.
Do not update them or the generated catalogs to make a compatibility test pass.
Future additions require new versioned interfaces/structures; existing L2 layouts,
values, callback semantics and presence meanings remain frozen.

`frozen.c` compiles solely against `include/`; `current.c` compiles against the
current public SDK. `frozen-l2-abi` compares every struct's size/alignment and every
field's offset/size, callback alias size/alignment, numeric values and interface/
capability/semantic identifiers on the running architecture. It uses no stored
x86_64 byte offsets and runs unchanged on native ARM64 CI.

`generate_catalog.py` reads the explicit checked-in semantic model literal in
`../projected-light/generate_views.py` without executing it. It covers every
generated semantic structure and presence wrapper. The small handwritten table
catalog is checked against the frozen headers. Numeric/string names come only
from this snapshot. `--check` verifies the checked-in catalogs without writing any
files or refreshing the snapshot. It also compares frozen/current public struct
declarations and callback signatures, ignoring comments/formatting and allowing
new versioned names; same-width type changes cannot evade the layout check.
Both tests run in the existing x86_64/ARM64 CI
matrix and sanitizer suite through CTest.

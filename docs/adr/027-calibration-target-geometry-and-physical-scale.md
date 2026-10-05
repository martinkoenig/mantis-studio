# ADR-027: Calibration target geometry and physical scale

Status: Accepted for v0.3 M1; additive to frozen Architecture v1

## Context

Printed calibration targets can differ from their nominal dimensions, including
independent X/Y print scale. Detection without explicit physical geometry cannot
establish the intended millimeter scale. v0.3 supports both Checkerboard and
first-class ChArUco targets without coupling the foundation to OpenCV.

## Decision

Targets carry explicit identity/revision and typed nominal geometry. Checkerboard
and ChArUco share a planar active grid whose X/Y counts are numbers of **squares**,
not inner corners. At least two squares per axis provide an interior intersection;
nominal square size is finite and positive. ChArUco has an explicit nonempty
dictionary and finite positive marker size strictly smaller than the square.
Preferred dictionary/reference board dimensions remain unfrozen; supported
OpenCV dictionaries and corner IDs are verified by the M2 adapter.

Measured active width/height are optional and must be a pair. They measure outer
active-grid edge to opposite outer active-grid edge, not paper, margin, substrate
or mounting plate. Each must be finite and positive. Optional uncertainty values
are finite/non-negative provenance, alongside instrument/note; they define no
confidence model. Identity/revision allocation and artifact encoding are later
persistence concerns; M1 retains supplied values without generating them.

Derive independent global scales:

```text
nominal_width  = squares_x * nominal_square_size_mm
nominal_height = squares_y * nominal_square_size_mm
scale_x = measured_width  / nominal_width
scale_y = measured_height / nominal_height
```

Without measurements, both scales are 1 and geometry is nominal. With a pair,
geometry is measured. Effective X/Y pitch is nominal pitch times its respective
scale. ChArUco marker width and height are independently scaled by X/Y; an
anisotropic print is represented as rectangular physical markers. Do not average
the scales or impose an arbitrary anisotropy/print-error threshold. Derived
geometry must remain finite and positive in double precision.

Target-local millimeter coordinates have origin at the increasing-column/row
active-grid outer corner, +X along columns, +Y along rows and +Z their cross-product
normal; the plane is Z=0. Scale X/Y independently, preserve Z. Object geometry
uses measured/effective scale. Camera optical and rig frames remain distinct.
M1 provides this primitive without asserting OpenCV ID-to-coordinate ordering.

## Consequences

Uniform global scale is correctable; global anisotropic scale is representable.
Two extents do **not** certify or correct local nonlinear deformation, shear,
warping or curvature. No scanner-accuracy claim follows from target scale
correction. Quality thresholds and metrology confidence require later evidence.

The model remains header-only with standard C++23 and Mantis base dependencies;
no OpenCV detector, JSON/persistence, service or schema change is introduced.
See the [v0.3 baseline](../architecture/v0.3-geometric-calibration.md).

## Alternatives considered

Using nominal dimensions only loses known physical scale. One averaged scale
hides anisotropy. An unstructured parameter map obscures the target contract.
Embedding detector dictionary enums or artifact serialization in the foundation
would reverse dependency direction and prematurely freeze later adapters.

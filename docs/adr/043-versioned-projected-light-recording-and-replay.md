# ADR-043: Projected-light recording uses opt-in RawCapture schema 3 and evidence-preserving replay

Status: Accepted v0.4 L0 architecture baseline; codec/storage implementation planned in L4

## Context

Inspection of data.hpp/data.cpp and artifact-store.cpp establishes that FrameSet
schema 1 has only image children and no parent attributes; MANTIS02 permits only
that composite. RawCapture schema-2 append/finalize requires exact FrameSets and
continuous FrameSet sequences. Projected-light acquisition also needs typed
control/trigger/terminal evidence when no image exists, including failed starts.

## Decision

1. Keep RawCapture schemas 1/2 and MANTIS01/MANTIS02 unchanged. Camera-only
   capture remains schema 2; projected-light bundle recording opts into RawCapture
   schema 3. No old artifact migration/pixel rewrite or project schema bump follows.
2. Add an explicit MANTIS03 AcquisitionBundle encoding with bounded typed members
   and nesting outer bundle → FrameSet → ImageFrame. Flat typed evidence and
   LaserObservation can reuse MANTIS01; FrameSets retain MANTIS02 encoding.
3. Use distinct MRAWREC3 framing/footer interpretation and bundle sequence, reusing
   schema 2's bounded sequential record/checksum/segment/index design. Retain the
   128 MiB record limit and normal 64 MiB segment target. Exact codec bytes/golden
   fixtures are an L4 specification, not executable L0 code.
4. Record immutable program/configuration before enablement and initialization/
   terminal evidence-only bundles. Persist commands, acknowledgement stages,
   observed/effective state, timing, source associations and unavailable evidence
   exactly. Separate run outcome from artifact finalization/recovery state.
5. Preserve synchronized-segment durability and complete OS-visible record recovery.
   Reject corrupt complete records; remove only structurally incomplete trailing
   bytes during explicit recovery. Missing terminal evidence means incomplete/
   unknown outcome. Recovery never fabricates OFF or resumes hardware execution.
6. Replay returns the recorded associations/decisions/calibration, with bounded
   pacing/cancellation and mapping ownership. It never opens controllers, emits ON/OFF
   or triggers, re-pairs images, recomputes effective state or consults active calibration.
   Determinism covers the verified prefix; unpublished/in-flight or power-loss tail
   activity is not invented. New processing creates distinct derived artifacts.
7. Dispatch readers by explicit artifact/container/packet version. Old readers
   reject schema 3; they must not report image-only schema-2 replay as complete
   acquisition replay. New readers keep legacy fixtures. Any image-only derivative
   is a new artifact with explicit omissions and lineage, never an in-place downgrade.

The [recording/replay contract](../architecture/v0.4-laser-acquisition.md#10-recording-and-deterministic-replay)
defines the compatibility and recovery requirements.

## Consequences

A single ordered recoverable stream holds measurement and control evidence,
including failure before the first camera frame. Existing accepted Q6A recordings
remain byte/semantically stable. Codec/version dispatch adds implementation and
validation work in L4; a new milestone alone never justifies a new container.

## Alternatives considered

A typed JSON metadata extension is sufficient for compact facts attached to an
existing frame, but not standalone startup/terminal records or the required bundle
model. A separate high-rate evidence journal would duplicate ordering/durability/
recovery authority and permit cross-stream inconsistency. Silently widening
FrameSet/schema 2 would make historical reader success ambiguous. These alternatives
are rejected for the complete projected-light authoritative stream, not for ordinary
compatible metadata additions to old camera-only packets.

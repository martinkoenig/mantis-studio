# Two-heavy trial and single-heavy rollback — 2026-11-11

Refs #12. This is an operational experiment record, not product acceptance.

The owner authorized two concurrent heavy qualification campaigns, each limited
to two compiler jobs (four aggregate) and one serial CTest invocation per worker.
Compiler activity from the other campaign was observed at the start and end of
the failing strict sanitizer suite. These observations were sampled, not a
continuous host or scheduling trace.

## Recorded outcomes

| Run | Result |
| --- | --- |
| Parallel campaign: Studio ON full suite | 206/206 passed |
| Parallel campaign: headless OFF full suite | 163/163 passed |
| Parallel campaign: original UI repetition | 30/30 passed |
| Parallel campaign: strict SAN full-suite command | 22 passed; `virtual-rig-scene` timed out at 40.09 s; 140 tests not run |
| One isolated serial strict SAN full-suite command | 163/163 passed; `virtual-rig-scene` passed in 34.6135 s |

The `virtual-rig-scene` timeout remained **40 seconds**. The isolated comparison
used the same complete 163-test inventory, actual execution order and original
23-test prefix, without filtering, repetition, reconfiguration or rebuilding.
Only the JUnit output destination changed. No competing native work was visible
in its recorded boundary audits; admission and sampled audits do not establish
continuous global absence.

## Input and receipt identity

Both runs used base commit
`2e34172bae3eee7a500a4cd5f47ecbfcba5274a5` and the investigated 1006-input working
source manifest
`8f886b11f935fe7450afe3f6a6e1feaa2643e14c1fd7bea2383d6027b4a25bce`.
Recorded source bytes/modes, test binary, plugin, SAN configuration/build rules,
tool bytes and environment fields were checked unchanged. The original SAN
loaded-runtime hashes were not captured; current runtime file pins do not prove
original loaded-runtime equality.

| Immutable receipt or input | SHA256 |
| --- | --- |
| Original parallel failure capsule | `59a44100f2deeb09bbcce9e5fcccc45d3c1911827eaeb087eae7f36ffe92f7e7` |
| Isolated complete serial comparison capsule | `dc4acd39bd99df1387f83b6fe366cae9b299f8ddf063cb2e4e5ade86e1d54025` |
| Unchanged virtual-scene test binary | `7a61beafc84098b92e4634da46817165ea1525dfd977a9e7b78cdd81ba0e97cd` |
| Unchanged virtual-rig plugin | `87eee3ef4f1132d9449edd5438d123bb0a6e0d83ab1d3971f6a02040a643e6d9` |

Raw receipts remain in restricted evidence storage. Their original base/source
identities remain attached to their original runs; this later documentation
commit does not relabel them as qualification of a new revision.

## Operational disposition and limits

The parallel failure followed by the single complete isolated pass met the
owner's conditional rollback rule. Heavy-work admission was restored to **one
active heavy campaign**. The original failure is retained. No test timeout,
security requirement or quality gate was relaxed.

This is correlation. The initiating root cause remains **UNKNOWN**: the comparison
establishes neither sole causation nor a deadlock, and does not exonerate source
or fixture behavior. It establishes no universal test stability or measured
speedup. Historical recorder saturation causes remain independently **UNKNOWN**;
this qualification timeout does not establish a shared cause.

Remaining gates include the actual LSan positive and all 14 detector controls,
entire cache/instruction/decoder modules, normal product/archive symbol
isolation, a fresh observer buffer control, actual observer effectiveness and
cost/memory qualification, and independent source and exact-revision CI review.
Each requires its applicable separate authorization and evidence.

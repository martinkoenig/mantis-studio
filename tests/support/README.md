# Async failure-path fixtures

`Async` owns a launched future and calls its cleanup callback before the future's
destructor joins. Declare every borrowed fixture before the task. Cleanup must
cancel, close or release clock/gate waits; worker exceptions are retrieved through
`future.get()`. `OnExit` also closes a producer's source when that producer throws.
`assertion_unwinds` checks the original diagnostic and bounded cleanup of an
ordinary assertion inside an async scope.

`ObservedWait` is linked only into `core`, `semantic-pipeline-replay` and
`projected-raw-capture`, through their existing executables. On the qualified
GNU/Linux/libstdc++ static-library builds, link wrapping intercepts the actual
condition wait in both the test and runtime archive. The selected worker arms a
thread-local observer. The wrapper reports while holding the condition wait's
mutex. The observer must acquire that same mutex while the same wait generation
is active: a worker-start signal, full queue, or pre-wait callback cannot satisfy
this handshake. The timed-wait wrapper also counts timeout returns, so a timed
close/stop case cannot pass by expiring its own timeout.

Emergency `wake()` calls belong only to cleanup, after cancellation/closure or
clock release. Positive assertions run before those cleanup notifications.
Deleting a required queue notification must fail its positive oracle even
though cleanup can still retire the worker. The GNU linker wrapper for the
out-of-line libstdc++ `condition_variable::wait` is necessary: wrapping pthread
alone misses that call inside the shared standard library. No observer code is
added to production source or executables, and no queue layout/API/ABI changes.
Other standard libraries/platforms or shared runtime builds need their own wait
adapter before claiming this proof; unavailable observation fails explicitly.

Replay follows ADR-043's bounded pacing/cancellation contract. Its existing 50 ms
polling fallback means removing direct notifications alone need not fail these
tests. Stop cancellation must still return `Status::cancelled`, clock advancement
must deliver the exact next recorded bundle, and a competing `next` must be busy.
The cleanup clock release also retires a task whose stop is deliberately broken.
Recovery's assertion control cancels and waits before catching the diagnostic,
then verifies cancellation, RECOVERABLE state and the exact retryable prefix.

"""Public Job deadlines composed with canonical wire and owned real TCP peers.

Reuse the control fixture's ownership/exception transport, not its test cases.
Virtual time advances at observed phases, never to infer host performance.
"""
import json

import socket
import struct
import threading
from contextlib import contextmanager, ExitStack
from types import SimpleNamespace
import unittest
from unittest.mock import patch

# This fixture consumes the build-style root argument and imports its SDK.
from python_control import Peer, ClockSocket, REAL_CONNECT, TOKEN, frame, mantis, wire


def completed(request, *, artifact="artifact", status='{"verified":true}'):
    result = wire.Response(protocol_version=1, request_id=request.request_id)
    result.jobs.add(id="job", state="Completed", result_artifact=artifact, status=status)
    return frame(result.SerializeToString())


def snapshot(*jobs):
    def reply(peer, conn, request):
        result = wire.Response(protocol_version=1, request_id=request.request_id)
        for fields in jobs:
            result.jobs.add(**fields)
        conn.sendall(frame(result.SerializeToString()))
    return reply


def trickle_job(peer, conn, request):
    for byte in completed(request):
        if not peer.next_chunk.acquire(timeout=2):
            raise AssertionError("Job peer: read phase not observed")
        if peer.release.is_set():
            return
        conn.sendall(bytes([byte]))


@contextmanager
def exchanges(test, replies, *, budget=1.0, costs=None, sleep_extra=0):
    clock, observed, connects, sleeps = [100.0], [], [], []
    with ExitStack() as stack:
        peers = [stack.enter_context(Peer(reply)) for reply in replies]

        def connect(address, timeout):
            index = len(connects)
            test.assertLess(index, len(peers), "unexpected retry or report request")
            connects.append((timeout, clock[0]))
            test.assertAlmostEqual(timeout, min(5, 100 + budget - clock[0]), places=7,
                                   msg="nested snapshot renewed the Job budget")
            class SequencedSocket(ClockSocket):
                def recv(self, size):
                    if replies[index] is trickle_job:
                        peers[index].next_chunk.release()
                    return super().recv(size)
            sock = SequencedSocket(REAL_CONNECT((address[0], peers[index].port), timeout), clock, costs or {})
            observed.append(sock)
            return sock

        def sleep(duration):
            test.assertGreater(duration, 0)
            test.assertLessEqual(duration, min(0.025, 100 + budget - clock[0]))
            sleeps.append(duration)
            clock[0] += duration + sleep_extra

        stack.enter_context(patch.object(mantis, "time", SimpleNamespace(
            monotonic=lambda: clock[0], sleep=sleep)))
        stack.enter_context(patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)))
        yield SimpleNamespace(job=mantis.Job(mantis.connect(port=1, token=TOKEN), "job"),
                              clock=clock, sockets=observed, connects=connects, sleeps=sleeps, peers=peers)
    test.assertEqual(len(connects), len(peers), "missing expected snapshot")
    ids = []
    for peer in peers:
        test.assertEqual(len(peer.requests), 1)
        test.assertEqual(peer.errors, [])
        test.assertEqual(peer.tail, b"")
        test.assertFalse(peer.thread.is_alive())
        ids.append(peer.requests[0].request_id)
    test.assertEqual(len(set(ids)), len(ids), "each independent snapshot needs a fresh correlation")
    for sock in observed:
        test.assertEqual(sock.sock.fileno(), -1)


class Jobs(unittest.TestCase):
    def test_control_failure_then_report_and_control_recovery_share_client(self):
        client = mantis.connect(port=1, token=TOKEN)
        with Peer(lambda p, c, r: c.sendall(frame(b"\x0a\xff"))) as bad:
            client.port = bad.port
            with self.assertRaises(mantis.MantisError) as raised:
                client.snapshot()
            self.assertEqual(raised.exception.component, "protocol")
            self.assertEqual(str(raised.exception), "Malformed control response protobuf")
        with exchanges(self, [lambda p, c, r: c.sendall(completed(r)),
                              lambda p, c, r: c.sendall(completed(r, status='{"count":7}'))],
                       costs={"send": 0.125, "recv": 0.001}) as run:
            run.job = mantis.Job(client, "job")
            self.assertIs(run.job.client, client)
            self.assertEqual(run.job.wait_report(1), {"count": 7})
            self.assertEqual(run.connects[0][0], 1, "prior control failure leaked a deadline")
            self.assertLess(run.connects[1][0], 1, "report exchange renewed the outer deadline")
            for sock in run.sockets:
                for phase, timeout, before in sock.calls:
                    self.assertAlmostEqual(timeout, 101 - before, places=7,
                                           msg="combined Job/control receive exceeded outer budget: " + phase)
        # The Job's virtual outer deadline is expired relative to real monotonic
        # time now. An independent ordinary control call must not inherit it.
        with Peer(lambda p, c, r: c.sendall(completed(r))) as good:
            client.port = good.port
            self.assertEqual(client.snapshot().jobs[0].id, "job")
        peers = [bad, *run.peers, good]
        ids = [p.requests[0].request_id for p in peers]
        self.assertEqual(len(ids), len(set(ids)), "recovery replayed an earlier request")
        for peer in peers:
            self.assertEqual(len(peer.requests), 1)
            self.assertEqual(peer.errors, [])
            self.assertEqual(peer.tail, b"")
            self.assertFalse(peer.thread.is_alive())
            self.assertEqual(peer.listener.fileno(), -1)

    def test_connect_completion_expiry_closes_before_send(self):
        clock, observed, budgets = [100.0], [], []
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(2)
            def connect(address, timeout):
                budgets.append(timeout)
                sock = ClockSocket(REAL_CONNECT(address, timeout), clock, {})
                observed.append(sock)
                clock[0] = 101
                return sock
            with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])), \
                    patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
                with self.assertRaises(TimeoutError):
                    mantis.Job(mantis.connect(port=listener.getsockname()[1], token=TOKEN), "job").wait(1)
            with listener.accept()[0] as accepted:
                accepted.settimeout(2)
                self.assertEqual(accepted.recv(1), b"", "expired connect must not send a request")
        self.assertEqual(budgets, [1])
        self.assertEqual(observed[0].calls, [])
        self.assertEqual(observed[0].sock.fileno(), -1)

    def test_expiry_after_send_never_starts_header_read(self):
        def waiting(peer, conn, request):
            if not peer.release.wait(2):
                raise AssertionError("Job send expiry did not release peer")
        with exchanges(self, [waiting], costs={"send": 1}) as run:
            with self.assertRaises(TimeoutError):
                run.job.wait(1)
            self.assertEqual([call[0] for call in run.sockets[0].calls], ["send"])

    def test_header_and_body_trickles_expire_without_budget_renewal(self):
        for read_cost, expected_reads in ((0.25, 4), (0.125, 8)):
            with self.subTest(expected_reads=expected_reads):
                with exchanges(self, [trickle_job], costs={"recv": read_cost}) as run:
                    with self.assertRaises(TimeoutError):
                        run.job.wait(1)
                    self.assertEqual(run.sockets[0].reads, expected_reads)
                    for phase, timeout, before in run.sockets[0].calls:
                        self.assertEqual(timeout, 101 - before, phase)

    def test_real_stalls_use_job_remaining_blocking_budget(self):
        for body in (False, True):
            with self.subTest(body=body):
                clock, observed = [100.0], []
                ready = threading.Event()
                def reply(peer, conn, request):
                    if body:
                        conn.sendall(struct.pack("!I", 2) + b"\x08")
                    ready.set()
                    if not peer.release.wait(2):
                        raise AssertionError("Job stall release was not delivered")
                class Stalled(ClockSocket):
                    def recv(self, size):
                        self_test.assertTrue(ready.wait(2), "peer never entered stalled phase")
                        self_test.assertTrue(0 < self.sock.gettimeout() <= 0.021,
                                             "Job blocking receive exceeded remaining budget")
                        return super().recv(size)
                self_test = self
                def connect(address, timeout):
                    self.assertEqual(timeout, 1)
                    sock = Stalled(REAL_CONNECT(address, timeout), clock, {})
                    observed.append(sock)
                    clock[0] += 0.98
                    return sock
                with Peer(reply) as peer:
                    with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])), \
                            patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
                        with self.assertRaises(TimeoutError):
                            mantis.Job(mantis.connect(port=peer.port, token=TOKEN), "job").wait(1)
                self.assertTrue(ready.is_set())
                self.assertEqual(observed[0].reads, 5 if body else 0)
                self.assertEqual(observed[0].sock.fileno(), -1)

    def test_late_decoded_terminal_success_is_rejected_without_report_rpc(self):
        for method in ("wait", "wait_report"):
            with self.subTest(method=method):
                with exchanges(self, [lambda p, c, r: c.sendall(completed(r))]) as run:
                    def parse(data):
                        result = wire.Response.FromString(data)
                        run.clock[0] = 101
                        return result
                    proxy = SimpleNamespace(Request=wire.Request, Empty=wire.Empty,
                                            Response=SimpleNamespace(FromString=parse))
                    with patch.object(mantis, "wire", proxy):
                        with self.assertRaises(TimeoutError):
                            getattr(run.job, method)(1)

    def test_success_after_job_transport_failure_has_no_global_deadline(self):
        client = mantis.connect(port=1, token=TOKEN)
        with Peer(lambda p, c, r: c.sendall(frame(b"\x0a\xff"))) as failed:
            client.port = failed.port
            with self.assertRaises(mantis.MantisError) as raised:
                mantis.Job(client, "job").wait(1)
            self.assertEqual(raised.exception.component, "protocol")
        with Peer(lambda p, c, r: c.sendall(completed(r))) as good:
            client.port = good.port
            self.assertEqual(mantis.Job(client, "job").wait(1), mantis.Artifact("artifact"))
        self.assertNotEqual(failed.requests[0].request_id, good.requests[0].request_id)

    def test_completed_and_pending_missing_transitions(self):
        for artifact in ("artifact", ""):
            with self.subTest(artifact=artifact):
                replies = [snapshot(), snapshot(dict(id="other", state="Completed")),
                           snapshot(dict(id="job", state="Pending")),
                           lambda p, c, r: c.sendall(completed(r, artifact=artifact))]
                with exchanges(self, replies, costs={"send": 0.01, "recv": 0.0001}) as run:
                    result = run.job.wait(1)
                    self.assertEqual(result, mantis.Artifact(artifact) if artifact else None)
                    self.assertEqual(run.sleeps, [0.025] * 3)
                    for sock in run.sockets:
                        for phase, timeout, before in sock.calls:
                            self.assertAlmostEqual(timeout, 101 - before, places=7, msg=phase)

    def test_failed_cancelled_diagnostics_and_no_report_fetch(self):
        for state in ("Failed", "Cancelled"):
            for diagnostic in ("", "job-specific failure"):
                for method in ("wait", "wait_report"):
                    with self.subTest(state=state, diagnostic=diagnostic, method=method):
                        with exchanges(self, [snapshot(dict(id="job", state=state, diagnostics=diagnostic))]) as run:
                            with self.assertRaises(mantis.MantisError) as raised:
                                getattr(run.job, method)(1)
                            self.assertEqual(str(raised.exception), diagnostic or state)
                            self.assertEqual(raised.exception.component, "job")

    def test_report_uses_remaining_budget_and_preserves_json_categories(self):
        for status, error in [('{"verified":true,"count":3}', None), ("not-json", json.JSONDecodeError)]:
            with self.subTest(status=status):
                with exchanges(self, [lambda p, c, r: c.sendall(completed(r)),
                                      lambda p, c, r: c.sendall(completed(r, status=status))],
                               costs={"send": 0.125, "recv": 0.001}) as run:
                    if error:
                        with self.assertRaises(error):
                            run.job.wait_report(1)
                    else:
                        self.assertEqual(run.job.wait_report(1), {"verified": True, "count": 3})
                    self.assertLess(run.connects[1][0], run.connects[0][0])
                    for sock in run.sockets:
                        for phase, timeout, before in sock.calls:
                            self.assertAlmostEqual(timeout, 101 - before, places=7, msg=phase)
        with exchanges(self, [lambda p, c, r: c.sendall(completed(r)), snapshot()]) as run:
            with self.assertRaises(StopIteration):
                run.job.wait_report(1)

    def test_expiry_in_report_io_and_json_handling(self):
        calls = [0]
        def second(peer, conn, request):
            conn.sendall(completed(request))
        with exchanges(self, [second, trickle_job], costs={"send": 0.4, "recv": 0.002}) as run:
            with self.assertRaises(TimeoutError):
                run.job.wait_report(1)
        real_loads = json.loads
        with exchanges(self, [second, second]) as run:
            def slow_parse(text):
                calls[0] += 1
                result = real_loads(text)
                run.clock[0] = 101
                return result
            with patch.object(mantis, "json", SimpleNamespace(loads=slow_parse)):
                with self.assertRaises(TimeoutError):
                    run.job.wait_report(1)
        self.assertEqual(calls, [1])

    def test_sleep_is_clipped_and_no_poll_after_expiry(self):
        with exchanges(self, [snapshot()], budget=0.01) as run:
            with self.assertRaises(TimeoutError):
                run.job.wait(0.01)
            self.assertAlmostEqual(run.sleeps[0], 0.01)
        with exchanges(self, [snapshot()], sleep_extra=1) as run:
            with self.assertRaises(TimeoutError):
                run.job.wait(1)
            self.assertEqual(run.sleeps, [0.025])

    def test_custom_snapshot_and_call_shapes_and_late_completion(self):
        clock = [100.0]
        result = wire.Response()
        result.jobs.add(id="job", state="Completed", status='{"ok":true}')
        class Custom(mantis.Client):
            def snapshot(self):
                return result
        class CustomCall(mantis.Client):
            def _call(self, **command):
                if set(command) != {"snapshot"}:
                    raise AssertionError("private deadline leaked to legacy call override")
                return result
        instance = mantis.Client(token=TOKEN)
        instance.snapshot = lambda: result
        for client in (Custom(token=TOKEN), CustomCall(token=TOKEN), instance,
                       SimpleNamespace(snapshot=lambda: result)):
            self.assertIsNone(mantis.Job(client, "job").wait())
            self.assertEqual(mantis.Job(client, "job").wait_report(), {"ok": True})
        def late():
            clock[0] = 101
            return result
        with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])):
            with self.assertRaises(TimeoutError):
                mantis.Job(SimpleNamespace(snapshot=late), "job").wait(1)

    def test_invalid_and_nonpositive_timeouts_are_inert(self):
        for method in ("wait", "wait_report"):
            for value, error in [(float("nan"), ValueError), (float("inf"), ValueError),
                                 (-float("inf"), ValueError), (None, TypeError), ("1", TypeError),
                                 (0, TimeoutError), (-1, TimeoutError)]:
                with self.subTest(method=method, value=value):
                    with patch.object(mantis.socket, "create_connection") as connect:
                        with self.assertRaises(error):
                            getattr(mantis.Job(mantis.connect(token=TOKEN), "job"), method)(value)
                        connect.assert_not_called()

    def test_long_wait_keeps_five_second_per_exchange_cap(self):
        with exchanges(self, [lambda p, c, r: c.sendall(completed(r))], budget=30) as run:
            self.assertEqual(run.job.wait(), mantis.Artifact("artifact"))
            self.assertEqual(run.connects[0][0], 5)
            for phase, timeout, before in run.sockets[0].calls:
                self.assertEqual(timeout, 105 - before, phase)

    def test_outer_budget_bounds_every_fragmented_io(self):
        clock = [100.0]
        observed, connections = [], []

        def connect(address, timeout):
            connections.append((timeout, clock[0]))
            wrapped = ClockSocket(REAL_CONNECT(address, timeout), clock,
                                  {"send": 0.125, "recv": 0.001})
            observed.append(wrapped)
            clock[0] += 0.25
            return wrapped

        with Peer(lambda p, c, r: c.sendall(completed(r))) as peer:
            with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])), \
                    patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
                result = mantis.Job(mantis.connect(port=peer.port, token=TOKEN), "job").wait(1)
        self.assertEqual(result, mantis.Artifact("artifact"))
        self.assertEqual(len(connections), 1)
        self.assertEqual(connections[0][0], 1, "Job outer budget was bypassed at connect")
        self.assertGreater(observed[0].reads, 5, "must compose fragmented header and body")
        for phase, timeout, before in observed[0].calls:
            self.assertAlmostEqual(timeout, 101 - before, places=7,
                                   msg=f"Job outer budget was bypassed at {phase}")
        self.assertEqual(observed[0].sock.fileno(), -1)
        self.assertEqual(len(peer.requests), 1)
        self.assertEqual(peer.errors, [])


if __name__ == "__main__":
    unittest.main()

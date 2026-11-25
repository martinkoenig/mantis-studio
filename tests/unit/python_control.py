"""Public Python client against owned loopback protocol peers, never mantisd.

Run with a build-style root containing python/mantis and canonical generated wire,
or with python -I and --installed to use only a clean consumer prefix. The latter
emits installed module identities without adding any source/build import path.
Virtual monotonic time advances only at observed I/O; real sockets still carry
all request/response bytes. Timeouts are hang bounds, not ordering evidence.
"""
import ast
import errno
import hashlib
import json
from pathlib import Path
import socket
import struct
import sys
import threading
import traceback
from types import SimpleNamespace
import unittest
from unittest.mock import patch

mode = sys.argv.pop(1)
installed = mode in ("--installed", "--installed-sdk")
if installed:
    if not sys.flags.isolated or sys.prefix == sys.base_prefix:
        raise RuntimeError("Installed control qualification requires python -I and a private venv")
else:
    sys.path.insert(0, str(Path(mode).resolve() / "python"))
with patch("socket.socket", side_effect=AssertionError("import opened a socket")), \
        patch("socket.create_connection", side_effect=AssertionError("import connected")):
    import mantis
from mantis import wire

if installed:
    import google.protobuf
    modules = (mantis, mantis.calibration, wire, google.protobuf)
    identities = {}
    for module in modules:
        if not module.__file__:
            raise RuntimeError("Installed control module has no file identity")
        path = Path(module.__file__).resolve(strict=True)
        if not path.is_relative_to(Path(sys.prefix).resolve()) or 'site-packages' not in path.parts:
            raise RuntimeError("Control module is not installed under clean consumer prefix")
        identities[module.__name__] = {'path': str(path),
                                      'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    print('MANTIS_INSTALLED_CONTROL ' + json.dumps({
        'prefix': sys.prefix, 'isolated': sys.flags.isolated,
        'protobuf': google.protobuf.__version__, 'modules': identities}), flush=True)

TOKEN = "test-only-control-token"
REAL_CONNECT = socket.create_connection


def receive(sock, size):
    data = bytearray()
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise AssertionError("peer: request truncated")
        data.extend(part)
    return bytes(data)


def frame(body):
    return struct.pack("!I", len(body)) + body


def response(request):
    return wire.Response(protocol_version=1, request_id=request.request_id,
                         project_path="loopback-project").SerializeToString()


class Peer:
    """One owned connection; errors cross the join, even on assertion unwind."""
    def __init__(self, handler):
        self.handler = handler
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(2)
        self.listener.settimeout(2)
        self.port = self.listener.getsockname()[1]
        self.release = threading.Event()
        self.next_chunk = threading.Semaphore(0)
        self.errors = []
        self.requests = []
        self.tail = None
        self.thread = threading.Thread(target=self.run, name="python-control-peer")

    def run(self):
        try:
            with self.listener.accept()[0] as conn:
                conn.settimeout(2)
                size, = struct.unpack("!I", receive(conn, 4))
                if not 0 < size <= 4 * 1024 * 1024:
                    raise AssertionError("peer: invalid request length")
                request = wire.Request.FromString(receive(conn, size))
                if request.protocol_version != 1 or request.token != TOKEN:
                    raise AssertionError("peer: invalid protocol/authentication")
                if request.WhichOneof("command") != "snapshot":
                    raise AssertionError("peer: unexpected command")
                self.requests.append(request)
                self.handler(self, conn, request)
                conn.shutdown(socket.SHUT_WR)
                self.tail = conn.recv(1)
                if self.tail != b"":
                    raise AssertionError("peer: duplicate request bytes")
        except BaseException as error:
            self.errors.append(error)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, kind, error, traceback):
        self.release.set()  # Release before join, including failed assertions.
        self.next_chunk.release()
        self.thread.join(3)
        try:
            if self.thread.is_alive():
                raise AssertionError("peer: owned thread did not retire")
            # A queued second connection is an unintended automatic retry.
            self.listener.setblocking(False)
            try:
                extra, _ = self.listener.accept()
            except BlockingIOError:
                pass
            else:
                extra.close()
                raise AssertionError("peer: duplicate connection")
            if self.errors:
                raise self.errors[0]
        except BaseException as cleanup:
            if error is None:
                raise
            # Do not replace the primary on supported Python3.10 (no add_note),
            # or mutate errno-bearing args that OSError.__str__ ignores.
            diagnostic = RuntimeError(f"Peer teardown also failed: {type(cleanup).__name__}: {cleanup}")
            diagnostic.__cause__ = error.__cause__
            diagnostic.__suppress_context__ = True
            error.__cause__ = diagnostic
            error.__suppress_context__ = True
        finally:
            self.listener.close()


def trickle_reply(peer, conn, request):
    for byte in frame(response(request)):
        if not peer.next_chunk.acquire(timeout=2):
            raise AssertionError("peer: next receive phase was not observed")
        if peer.release.is_set():
            return
        conn.sendall(bytes([byte]))


class ClockSocket:
    """Delegates to real TCP, records effective timeout at each blocking phase."""
    def __init__(self, sock, clock, costs):
        self.sock, self.clock, self.costs = sock, clock, costs
        self.calls = []
        self.reads = 0

    def settimeout(self, value):
        self.sock.settimeout(value)

    def sendall(self, data):
        self.calls.append(("send", self.sock.gettimeout(), self.clock[0]))
        self.sock.sendall(data)
        self.clock[0] += self.costs.get("send", 0)

    def recv(self, size):
        self.calls.append(("recv", self.sock.gettimeout(), self.clock[0]))
        # Force observable fragmentation independent of TCP packet coalescing.
        part = self.sock.recv(min(size, 1))
        self.reads += 1
        self.clock[0] += self.costs.get("recv", 0)
        return part

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class Control(unittest.TestCase):
    def call_peer(self, reply, expected=None, message=None):
        with Peer(reply) as peer:
            client = mantis.connect(port=peer.port, token=TOKEN)
            if expected is None:
                result = client.snapshot()
            else:
                with self.assertRaises(expected) as raised:
                    client.snapshot()
                self.assertIs(type(raised.exception), expected)
                if message is not None:
                    self.assertEqual(str(raised.exception), message)
                result = raised.exception
        self.assertEqual(len(peer.requests), 1)
        self.assertEqual(peer.tail, b"")
        return result

    def acceptance_raw_request(self, client, port, connect=REAL_CONNECT):
        # Execute the existing caller itself, not a copied approximation or the
        # surrounding daemon/product scenario. The qualifier freezes this input.
        source = Path(__file__).resolve().parents[1] / "integration/acceptance.py"
        functions = [node for node in ast.walk(ast.parse(source.read_text()))
                     if isinstance(node, ast.FunctionDef) and node.name == "raw_request"]
        self.assertEqual(len(functions), 1, "existing acceptance caller must be unique")
        namespace = {"client": client, "port": port, "wire": wire,
                     "socket": SimpleNamespace(create_connection=connect)}
        exec(compile(ast.Module(body=[functions[0]], type_ignores=[]), str(source), "exec"), namespace)
        return namespace["raw_request"]

    def test_existing_acceptance_two_argument_receive_call_shape(self):
        observed = []
        clock = [100.0]

        def connect(address, timeout):
            sock = ClockSocket(REAL_CONNECT(address, timeout), clock, {})
            observed.append(sock)
            return sock

        def reply(peer, conn, request):
            self.assertEqual(request.request_id, "existing-acceptance-call")
            self.assertTrue(request.SerializeToString().endswith(b"\xf8\x07\x01"))
            conn.sendall(frame(response(request)))

        with Peer(reply) as peer:
            client = mantis.Client(port=peer.port, token=TOKEN)
            raw_request = self.acceptance_raw_request(client, peer.port, connect)
            request = wire.Request(protocol_version=1, request_id="existing-acceptance-call",
                                   token=TOKEN, snapshot=wire.Empty())
            try:
                result = raw_request(request, b"\xf8\x07\x01")
            except TypeError as error:
                self.fail(f"existing two-argument receive caller is incompatible: {error}")
            self.assertEqual(result.project_path, "loopback-project")
            self.assertEqual(result.request_id, request.request_id)
        self.assertGreater(observed[0].reads, 4, "existing header/body reads must fragment")
        self.assertEqual(observed[0].sock.fileno(), -1)
        self.assertEqual(len(peer.requests), 1)
        self.assertEqual(peer.errors, [])
        self.assertEqual(peer.tail, b"")

    def test_existing_acceptance_two_argument_receive_truncation(self):
        for data in (b"\x00\x00", struct.pack("!I", 20) + b"\x08\x01"):
            with self.subTest(data_length=len(data)):
                with Peer(lambda p, c, r: c.sendall(data)) as peer:
                    client = mantis.Client(port=peer.port, token=TOKEN)
                    raw_request = self.acceptance_raw_request(client, peer.port)
                    request = wire.Request(protocol_version=1, request_id="truncated-acceptance",
                                           token=TOKEN, snapshot=wire.Empty())
                    with self.assertRaises(ConnectionError) as raised:
                        raw_request(request)
                    self.assertIs(type(raised.exception), ConnectionError)
                    self.assertEqual(str(raised.exception), "mantisd closed the control connection")
                self.assertEqual(peer.errors, [])
                self.assertEqual(peer.tail, b"")
                self.assertFalse(peer.thread.is_alive())
                self.assertEqual(peer.listener.fileno(), -1)

    def test_standalone_two_argument_receive_has_one_bounded_read_budget(self):
        clock = [100.0]
        with Peer(trickle_reply) as peer:
            class TrickleSocket(ClockSocket):
                def recv(self, size):
                    peer.next_chunk.release()
                    return super().recv(size)

            with REAL_CONNECT(("127.0.0.1", peer.port), timeout=1) as conn:
                request = wire.Request(protocol_version=1, request_id="standalone-read",
                                       token=TOKEN, snapshot=wire.Empty())
                conn.sendall(frame(request.SerializeToString()))
                sock = TrickleSocket(conn, clock, {"recv": 1})
                with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])):
                    with self.assertRaises(TimeoutError) as raised:
                        mantis.Client._receive(sock, 6)
                self.assertIs(type(raised.exception), TimeoutError)
                self.assertEqual(sock.reads, 5, "standalone read cannot renew on partial progress")
                for phase, timeout, before in sock.calls:
                    self.assertEqual(phase, "recv")
                    self.assertEqual(timeout, 105.0 - before)
            self.assertEqual(conn.fileno(), -1)
        self.assertEqual(peer.errors, [])
        self.assertEqual(peer.tail, b"")
        self.assertFalse(peer.thread.is_alive())

    def test_fragmented_success_uses_remaining_budget_for_every_io(self):
        clock = [100.0]
        observed = []

        def connect(address, timeout):
            self.assertEqual(address[0], "127.0.0.1")
            self.assertEqual(timeout, 5)
            sock = ClockSocket(REAL_CONNECT(address, timeout), clock,
                               {"send": 0.5, "recv": 0.01})
            clock[0] += 0.25  # observed connect completion
            observed.append(sock)
            return sock

        with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])), \
                patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
            result = self.call_peer(lambda peer, conn, req: conn.sendall(frame(response(req))))
        self.assertEqual(result.project_path, "loopback-project")
        self.assertGreater(len(observed[0].calls), 6, "header and body must fragment")
        self.assertEqual(observed[0].calls[0][0], "send")
        for phase, timeout, before in observed[0].calls:
            self.assertAlmostEqual(timeout, 105.0 - before, places=7,
                                   msg=f"{phase} did not use the shared remaining budget")
        self.assertEqual(observed[0].sock.fileno(), -1)

    def test_real_header_and_body_stalls_use_remaining_blocking_budget(self):
        for body_stall in (False, True):
            with self.subTest(body_stall=body_stall):
                clock = [100.0]
                ready = threading.Event()
                observed = []

                def reply(peer, conn, request):
                    if body_stall:
                        conn.sendall(struct.pack("!I", 2) + b"\x08")
                    ready.set()
                    if not peer.release.wait(2):
                        raise AssertionError("peer: stall release not delivered")

                class StalledSocket(ClockSocket):
                    def recv(self, size):
                        if not ready.wait(2):
                            raise AssertionError("peer never entered stall phase")
                        # Fails before a stale five-second I/O can hide the bug.
                        self_timeout = self.sock.gettimeout()
                        if not 0 < self_timeout <= 0.021:
                            raise AssertionError("blocking receive did not use remaining budget")
                        return super().recv(size)

                def connect(address, timeout):
                    sock = StalledSocket(REAL_CONNECT(address, timeout), clock, {})
                    observed.append(sock)
                    clock[0] += 4.98
                    return sock

                with Peer(reply) as peer:
                    with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])), \
                            patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
                        with self.assertRaises(TimeoutError) as raised:
                            mantis.Client(port=peer.port, token=TOKEN).snapshot()
                    self.assertIs(type(raised.exception), TimeoutError)
                    self.assertTrue(ready.is_set())
                self.assertEqual(observed[0].reads, 5 if body_stall else 0)
                self.assertEqual(observed[0].sock.fileno(), -1)

    def test_expired_connect_completion_cannot_send(self):
        clock = [100.0]
        observed = []

        def connect(address, timeout):
            # Bind a socket without connecting: deterministic no-peer send guard.
            sock = ClockSocket(socket.socket(), clock, {})
            observed.append(sock)
            clock[0] += 5
            return sock

        with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])), \
                patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
            with self.assertRaises(TimeoutError):
                mantis.Client(port=1, token=TOKEN).snapshot()
        self.assertEqual(observed[0].calls, [])
        self.assertEqual(observed[0].sock.fileno(), -1)

    def test_daemon_application_error_retains_all_fields(self):
        def reply(peer, conn, request):
            result = wire.Response(protocol_version=1, request_id=request.request_id)
            result.error.code = 6
            result.error.component = "capture"
            result.error.message = "device busy"
            conn.sendall(frame(result.SerializeToString()))

        error = self.call_peer(reply, mantis.MantisError, "device busy")
        self.assertEqual((error.code, error.component), (6, "capture"))

    def test_version_and_correlation_are_rejected(self):
        for version, correlation in ((2, None), (1, "not-this-request")):
            with self.subTest(version=version, correlation=correlation):
                def reply(peer, conn, request):
                    result = wire.Response(protocol_version=version,
                                           request_id=correlation or request.request_id)
                    conn.sendall(frame(result.SerializeToString()))
                self.call_peer(reply, mantis.MantisError, "Response version/correlation mismatch")

    def test_zero_and_oversize_response_lengths_are_rejected(self):
        for size in (0, 4 * 1024 * 1024 + 1):
            with self.subTest(size=size):
                self.call_peer(lambda p, c, r: c.sendall(struct.pack("!I", size)),
                               mantis.MantisError, "Invalid control response length")

    def test_close_and_truncation_are_transport_not_protocol_errors(self):
        for data in (b"", b"\x00\x00", struct.pack("!I", 20) + b"\x08\x01"):
            with self.subTest(data_length=len(data)):
                self.call_peer(lambda p, c, r: c.sendall(data), ConnectionError,
                               "mantisd closed the control connection")

    def test_absent_peer_is_connection_refused_without_retry(self):
        # Bound but not listening keeps the port reserved against external races.
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            with patch.object(mantis.socket, "create_connection", wraps=REAL_CONNECT) as connect:
                with self.assertRaises(ConnectionRefusedError):
                    mantis.Client(port=reservation.getsockname()[1], token=TOKEN).snapshot()
            self.assertEqual(connect.call_count, 1)

    def test_missing_credentials_and_construction_are_inert(self):
        with patch.dict("os.environ", {}, clear=True), \
                patch.object(mantis.socket, "create_connection") as connect:
            with self.assertRaisesRegex(ValueError, "Set MANTIS_TOKEN"):
                mantis.connect()
            client = mantis.connect(port=12345, token=TOKEN)
            self.assertEqual((client.port, client.token), (12345, TOKEN))
            connect.assert_not_called()

    def test_oversize_request_is_rejected_before_connect(self):
        with patch.object(mantis.socket, "create_connection") as connect:
            with self.assertRaisesRegex(ValueError, "Control message exceeds limit"):
                mantis.Client(port=1, token=TOKEN)._call(project_open=wire.ProjectOpen(
                    path="x" * (4 * 1024 * 1024)))
            connect.assert_not_called()

    def test_success_after_failure_has_fresh_connection_and_correlation(self):
        client = mantis.Client(port=1, token=TOKEN)
        def bad(peer, conn, request):
            conn.sendall(frame(b"\x0a\xff"))
        with Peer(bad) as first:
            client.port = first.port
            with self.assertRaises(mantis.MantisError):
                client.snapshot()
        with Peer(lambda p, c, r: c.sendall(frame(response(r)))) as second:
            client.port = second.port
            self.assertEqual(client.snapshot().project_path, "loopback-project")
        self.assertEqual(len(first.requests), 1)
        self.assertEqual(len(second.requests), 1)
        self.assertNotEqual(first.requests[0].request_id, second.requests[0].request_id)

    def test_peer_exception_is_propagated(self):
        failure = AssertionError("injected peer failure")
        def fail(peer, conn, request):
            raise failure
        peer = Peer(fail)
        with self.assertRaises(AssertionError) as raised:
            with peer:
                with self.assertRaises(ConnectionError):
                    mantis.Client(port=peer.port, token=TOKEN).snapshot()
        self.assertIs(raised.exception, failure)
        self.assertFalse(peer.thread.is_alive())
        self.assertEqual(peer.listener.fileno(), -1)

    def test_assertion_unwind_releases_peer_before_join(self):
        failure = AssertionError("initiating test assertion")
        received = threading.Event()
        def reply(peer, conn, request):
            received.set()
            if not peer.release.wait(2):
                raise AssertionError("peer: assertion unwind did not release gate")
        peer = Peer(reply)
        with self.assertRaises(AssertionError) as raised:
            with peer:
                with REAL_CONNECT(("127.0.0.1", peer.port), timeout=1) as conn:
                    request = wire.Request(protocol_version=1, request_id="unwind",
                                           token=TOKEN, snapshot=wire.Empty())
                    conn.sendall(frame(request.SerializeToString()))
                    self.assertTrue(received.wait(2), "peer did not read request")
                raise failure
        self.assertIs(raised.exception, failure)
        self.assertEqual(peer.errors, [])
        self.assertFalse(peer.thread.is_alive())
        self.assertEqual(peer.listener.fileno(), -1)

    def test_primary_and_peer_failures_preserve_rendered_diagnostics(self):
        class LegacyPrimary(PermissionError):
            # Also challenge modern interpreters with the supported 3.10 API.
            def __getattribute__(self, name):
                if name == 'add_note':
                    raise AttributeError('add_note unavailable (Python3.10 API model)')
                return super().__getattribute__(name)

        for primary_type in (PermissionError, LegacyPrimary):
            with self.subTest(primary_type=primary_type.__name__):
                primary = primary_type(errno.EACCES, 'initiating test failure')
                secondary = OSError(errno.EIO, 'secondary owned peer failure')
                received = threading.Event()
                connections = []
                def reply(peer, conn, request):
                    connections.append(conn)
                    received.set()
                    if not peer.release.wait(2):
                        raise AssertionError('peer: unwind did not release gate')
                    raise secondary
                peer = Peer(reply)
                try:
                    with peer:
                        # Actual installed Client/serialization, not a fabricated
                        # Job result. Peer waits for this primary's unwind.
                        # Raising from recv avoids waiting for a reply from the
                        # intentionally gated handler; SDK closes its real socket.
                        class FailingReceive(ClockSocket):
                            def recv(self, size):
                                if not received.wait(2):
                                    raise AssertionError('peer did not read canonical request')
                                raise primary
                        def connect(address, timeout):
                            return FailingReceive(REAL_CONNECT(address, timeout), [0.0], {})
                        with patch.object(mantis.socket, 'create_connection', side_effect=connect):
                            mantis.Client(port=peer.port, token=TOKEN).snapshot()
                except primary_type as caught:
                    self.assertIs(caught, primary)
                    self.assertEqual(caught.errno, errno.EACCES)
                    rendered = ''.join(traceback.format_exception(type(caught), caught, caught.__traceback__))
                    self.assertIn('recv', rendered)
                    self.assertIn('initiating test failure', rendered)
                    self.assertIn('secondary owned peer failure', rendered)
                    self.assertIn('Peer teardown also failed', rendered)
                else:
                    self.fail('primary failure was lost')
                self.assertEqual(peer.errors, [secondary])
                self.assertEqual(len(peer.requests), 1)
                self.assertFalse(peer.thread.is_alive())
                self.assertEqual(peer.listener.fileno(), -1)
                self.assertEqual(len(connections), 1)
                self.assertEqual(connections[0].fileno(), -1)

    def test_malformed_protobuf_is_a_bounded_protocol_error(self):
        def reply(peer, conn, request):
            conn.sendall(frame(b"\x0a\xff"))  # field with truncated varint length

        with Peer(reply) as peer:
            with self.assertRaises(mantis.MantisError) as raised:
                mantis.Client(port=peer.port, token=TOKEN).snapshot()
        self.assertEqual(raised.exception.component, "protocol")
        self.assertEqual(str(raised.exception), "Malformed control response protobuf")
        self.assertIsNone(raised.exception.__cause__)
        self.assertTrue(raised.exception.__suppress_context__)

    def test_transport_failure_survives_socket_close_failure(self):
        initiating = TimeoutError("injected receive failure")
        observed = []

        class FaultSocket(ClockSocket):
            def recv(self, size):
                raise initiating

            def close(self):
                super().close()
                raise OSError("injected close failure")

        def connect(address, timeout):
            wrapped = FaultSocket(REAL_CONNECT(address, timeout), [100.0], {})
            observed.append(wrapped)
            return wrapped

        def reply(peer, conn, request):
            if not peer.release.wait(2):
                raise AssertionError("peer: release not delivered")

        with Peer(reply) as peer:
            with patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
                with self.assertRaises(TimeoutError) as raised:
                    mantis.Client(port=peer.port, token=TOKEN).snapshot()
        self.assertIs(raised.exception, initiating)
        self.assertEqual(observed[0].sock.fileno(), -1)

    def test_close_failure_is_not_hidden_after_success_or_protocol_failure(self):
        class CloseFails(ClockSocket):
            def close(self):
                super().close()
                raise PermissionError("injected close failure")

        def connect(address, timeout):
            return CloseFails(REAL_CONNECT(address, timeout), [0.0], {})

        with patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
            self.call_peer(lambda p, c, r: c.sendall(frame(response(r))),
                           PermissionError, "injected close failure")
            error = self.call_peer(lambda p, c, r: c.sendall(frame(b"\x0a\xff")),
                                   mantis.MantisError, "Malformed control response protobuf")
            self.assertEqual(error.component, "protocol")

    def test_send_oserror_is_preserved_without_retry(self):
        initiating = PermissionError("injected send failure")
        observed = []
        class SendFails(ClockSocket):
            def sendall(self, data):
                raise initiating
        def connect(address, timeout):
            # No request is allowed to reach a peer after this transport failure.
            sock = SendFails(socket.socket(), [0.0], {})
            observed.append(sock)
            return sock
        with patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
            with self.assertRaises(PermissionError) as raised:
                mantis.Client(port=1, token=TOKEN).snapshot()
        self.assertIs(raised.exception, initiating)
        self.assertEqual(len(observed), 1)
        self.assertEqual(observed[0].sock.fileno(), -1)

    def test_trickling_progress_cannot_extend_exchange_deadline(self):
        clock = [100.0]
        observed = []

        class TrickleSocket(ClockSocket):
            def recv(self, size):
                peer.next_chunk.release()  # Only this observed read permits a byte.
                return super().recv(size)

        def connect(address, timeout):
            self.assertEqual(address[0], "127.0.0.1")
            self.assertEqual(timeout, 5)
            sock = TrickleSocket(REAL_CONNECT(address, timeout), clock, {"recv": 1})
            observed.append(sock)
            return sock

        client = mantis.Client(port=1, token=TOKEN)
        with Peer(trickle_reply) as peer:
            client.port = peer.port
            with patch.object(mantis, "time", SimpleNamespace(monotonic=lambda: clock[0])), \
                    patch.object(mantis, "socket", SimpleNamespace(create_connection=connect)):
                with self.assertRaises(TimeoutError):
                    client.snapshot()
        self.assertEqual(len(observed), 1)
        self.assertEqual(observed[0].reads, 5, "deadline must expire at the five-second boundary")
        self.assertEqual(observed[0].sock.fileno(), -1)
        with Peer(lambda p, c, r: c.sendall(frame(response(r)))) as recovered:
            client.port = recovered.port
            self.assertEqual(client.snapshot().project_path, "loopback-project")
        self.assertEqual(len(recovered.requests), 1)
        self.assertNotEqual(peer.requests[0].request_id, recovered.requests[0].request_id)


if __name__ == "__main__":
    unittest.main()

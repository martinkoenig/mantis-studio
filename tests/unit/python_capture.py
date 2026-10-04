"""Stop errors surface immediately; successful finalization still waits for its job."""
import sys
from pathlib import Path
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(sys.argv.pop(1)).resolve() / "python"))
import mantis
from mantis import wire


class CaptureStop(unittest.TestCase):
    def client(self, *, error="", job=""):
        response = wire.Response()
        response.captures.add(id="capture", error=error, finalization_job_id=job)
        client = Mock()
        client._call.return_value = response
        return client

    def test_capture_error_precedes_finalization_wait(self):
        client = self.client(error="RIGHT sensor STREAMOFF failed: errno 5", job="finalize")
        with self.assertRaises(mantis.MantisError) as raised:
            mantis.Capture(client, "capture", "raw").stop()
        self.assertEqual(raised.exception.component, "capture")
        self.assertEqual(str(raised.exception), "RIGHT sensor STREAMOFF failed: errno 5")
        client.snapshot.assert_not_called()
        client._call.assert_called_once_with(capture_stop=wire.Id(id="capture"))

    def test_successful_asynchronous_finalization(self):
        client = self.client(job="finalize")
        running, completed = wire.Response(), wire.Response()
        running.jobs.add(id="finalize", state="Running")
        completed.jobs.add(id="finalize", state="Completed", result_artifact="raw")
        client.snapshot.side_effect = [running, completed]
        with patch("mantis.time.sleep") as sleep:
            result = mantis.Capture(client, "capture", "raw").stop()
        self.assertIs(result, client._call.return_value)
        self.assertEqual(client.snapshot.call_count, 2)
        sleep.assert_called_once_with(0.025)

    def test_finalization_job_error_is_preserved(self):
        client = self.client(job="finalize")
        failed = wire.Response()
        failed.jobs.add(id="finalize", state="Failed", diagnostics="Finalization write failed")
        client.snapshot.return_value = failed
        with self.assertRaises(mantis.MantisError) as raised:
            mantis.Capture(client, "capture", "raw").stop()
        self.assertEqual(str(raised.exception), "Finalization write failed")
        self.assertEqual(raised.exception.component, "job")

    def test_no_finalization_job_needs_no_wait(self):
        client = self.client()
        self.assertIs(mantis.Capture(client, "capture", "raw").stop(), client._call.return_value)
        client.snapshot.assert_not_called()


unittest.main()

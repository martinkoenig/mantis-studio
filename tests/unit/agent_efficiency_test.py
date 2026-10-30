"""Behavior and privacy regressions for the read-only efficiency analyzer."""

from concurrent.futures import ThreadPoolExecutor
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import time
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "agent_efficiency.py"
SPEC = importlib.util.spec_from_file_location("agent_efficiency", SCRIPT)
eff = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(eff)
SID = "00000000-0000-0000-0000-000000000001"
GID = "00000000-0000-0000-0000-000000000002"
SECRET = "PRIVATE_SENTINEL_do_not_export"


def record(kind, payload, second=0):
    return {"type": kind, "timestamp": f"2026-10-30T00:00:{second:02d}Z", "payload": payload}


def tokens(n=10):
    return {"input_tokens": n, "cached_input_tokens": n // 2,
            "output_tokens": 4, "reasoning_output_tokens": 2, "total_tokens": n + 4}


def session(extra=(), sid=SID):
    return [record("session_meta", {"id": sid, "base_instructions": SECRET})] + list(extra)


def collect(records, role="worker"):
    return eff.collect(io.StringIO("".join(json.dumps(x) + "\n" for x in records)), role)


def accounting(response="resp_1", value=None):
    return record("token_usage_record", {"response_id": response, "usage": value or tokens(),
                  "thread_token_usage": tokens(1000), "turn_token_usage": tokens(100)})


def cumulative(value, included=True):
    return record("event_msg", {"type": "token_count", "info": {
        "total_token_usage": value, "last_usage_includes_reasoning": included}})


class AccountingTests(unittest.TestCase):
    def test_per_response_usage_not_cumulative_and_reasoning_not_added_twice(self):
        result = collect(session([accounting(), accounting(), accounting("resp_2"),
                                  cumulative(tokens(10000))]))["accounting"]
        self.assertEqual(result["total_tokens"], 28)
        self.assertEqual(result["input_tokens"], 20)
        self.assertEqual(result["uncached_input_tokens"], 10)
        self.assertEqual(result["reasoning_output_tokens"], 4)
        self.assertEqual(result["duplicate_responses"], 1)
        self.assertEqual(result["source"], "unique_response_usage")

    def test_reset_and_duplicate_cumulative_events(self):
        result = collect(session([cumulative(tokens(20)), cumulative(tokens(20)),
                                  cumulative(tokens(40)), cumulative(tokens(10))]))["accounting"]
        self.assertEqual(result["total_tokens"], 58)
        self.assertEqual(result["cumulative_resets_used"], 1)

    def test_conflicting_duplicate_fails(self):
        with self.assertRaises(eff.InvalidEvidence):
            collect(session([accounting(), accounting(value=tokens(20))]))

    def test_compaction_included_once_and_separate_from_terminal_cumulative(self):
        result = collect(session([accounting(), accounting("compaction", tokens(100)),
                                  record("compacted", {"compaction_response_id": "compaction",
                                                       "message": SECRET}),
                                  cumulative(tokens())]))["accounting"]
        self.assertEqual(result["total_tokens"], 118)
        self.assertEqual(result["identified_compaction_tokens_included"], 104)
        self.assertEqual(result["terminal_cumulative_total_tokens"], 14)

    def test_invalid_counters_fail_closed(self):
        for field, value in (("total_tokens", 123), ("input_tokens", -1),
                             ("cached_input_tokens", 11), ("output_tokens", True),
                             ("reasoning_output_tokens", 5)):
            with self.subTest(field=field):
                bad = tokens(); bad[field] = value
                with self.assertRaises(eff.InvalidEvidence):
                    collect(session([accounting(value=bad)]))

    def test_ambiguous_partial_reset_and_reasoning_semantics_fail(self):
        smaller_cache = tokens(30); smaller_cache["cached_input_tokens"] = 0
        for entries in ([cumulative(tokens()), cumulative(smaller_cache)],
                        [cumulative(tokens(), False)]):
            with self.assertRaises(eff.InvalidEvidence):
                collect(session(entries))

    def test_missing_accounting_is_unavailable_not_zero_cost_claim(self):
        self.assertEqual(collect(session())["accounting"]["source"], "unavailable")

    def test_multiple_session_boundaries_rejected(self):
        with self.assertRaises(eff.InvalidEvidence):
            collect(session([record("session_meta", {"id": GID})]))
        foreign = accounting(); foreign["payload"]["thread_id"] = GID
        with self.assertRaises(eff.InvalidEvidence):
            collect(session([foreign]))

    def test_runtime_session_identity_may_differ_from_native_thread(self):
        linked = accounting()
        linked["payload"].update({"thread_id": SID, "session_id": GID})
        self.assertEqual(collect(session([linked]))["accounting"]["total_tokens"], 14)

    def test_naive_and_invalid_timestamps_rejected(self):
        for value in (SECRET, "2026-10-30T00:00:00", None):
            records = session(); records[0]["timestamp"] = value
            with self.assertRaises(eff.InvalidEvidence):
                collect(records)


class PrivacyTests(unittest.TestCase):
    def tool(self, source, second=0, call="call_1"):
        return record("response_item", {"type": "custom_tool_call", "name": "exec",
                      "call_id": call, "input": source}, second)

    def review(self, command, second=1):
        request = {"command": ["/bin/bash", "-lc", command], "tool": "exec_command",
                   "sandbox_permissions": "require_escalated", "justification": SECRET}
        return record("response_item", {"type": "message", "role": "user", "content": [
            {"type": "input_text", "text": SECRET + json.dumps(request)}]}, second)

    def test_no_private_text_args_settings_or_tool_names_exported(self):
        source = 'await tools.exec_command({cmd:' + json.dumps(SECRET) + '})'
        worker = collect(session([self.tool(source), record("turn_context", {
            "model": SECRET, "effort": SECRET, "summary": SECRET,
            "approval_policy": SECRET, "sandbox_policy": {"type": SECRET,
            "writable_roots": [SECRET]}}), record("response_item", {
            "type": "message", "role": "assistant", "content": [{"text": SECRET}]})]))
        guardian = collect(session([self.review(SECRET)], GID), "guardian")
        # Even the internal join state retains only command digests.
        self.assertNotIn(SECRET, repr(worker))
        self.assertNotIn(SECRET, repr(guardian))
        exported = eff.sanitize([worker, guardian])
        self.assertNotIn(SECRET, json.dumps(exported))
        request = exported["sessions"][1]["requests"][0]
        self.assertEqual(request["correlation"], "unique_exact_literal_preceding")
        self.assertEqual(request["candidate_call_id"], "call_1")

    def test_ambiguous_or_later_command_never_claims_exact_mapping(self):
        for times in ((0, 0), (2,)):
            worker = collect(session([self.tool(json.dumps("ctest"), t, f"call_{i}")
                                      for i, t in enumerate(times)]))
            guardian = collect(session([self.review("ctest")], GID), "guardian")
            result = eff.sanitize([worker, guardian])["sessions"][1]["requests"][0]
            self.assertEqual(result["correlation"], "unmapped")

    def test_categories_are_bounded_and_dont_execute_source(self):
        self.assertEqual(eff.category("await tools.write_stdin({session_id:123})"),
                         "process_status_or_input")
        self.assertEqual(eff.category(SECRET), "other")
        self.assertEqual(eff.literals("__import__('os').system('never_execute')"),
                         {eff.hashlib.sha256(x.encode()).digest() for x in ("os", "never_execute")})


class OfflineConfigTests(unittest.TestCase):
    @staticmethod
    def execution_config():
        return {"worker": {"agentConfig": {"model": "gpt-6.1-sol", "effort": "high"}},
                "orchestrator": {"agentConfig": {"model": "gpt-6.1-sol", "effort": "medium"}}}

    def test_append_and_independent_copies_preserve_every_other_field(self):
        cfg = {"agentRules": "existing worker", "orchestratorRules": "existing coordinator",
               "agentRulesFile": "existing.md", "identity": SECRET,
               "worker": {"agent": "codex", "agentConfig": {"model": "gpt-6.1-sol", "effort": "high",
                           "permissions": "default"}},
               "orchestrator": {"agentConfig": {"model": "gpt-6.1-sol", "effort": "medium"}},
               "futureUnknownField": {"secret": [SECRET]}, "autoReview": True}
        expected = copy.deepcopy(cfg)
        old, proposed = eff.propose_rules(cfg, "worker addition", "coordinator addition",
                                         reference_config=expected)
        self.assertEqual(cfg, expected)
        self.assertEqual(old, expected)
        for key in cfg.keys() - {"agentRules", "orchestratorRules"}:
            self.assertEqual(cfg[key], proposed[key])
        self.assertTrue(proposed["agentRules"].startswith(cfg["agentRules"] + "\n\n"))
        self.assertEqual(eff.propose_rules(proposed, "worker addition", "coordinator addition",
                                          reference_config=copy.deepcopy(proposed))[1], proposed)
        proposed["futureUnknownField"]["secret"].append("changed")
        self.assertEqual(old, expected)

    def test_invalid_rules_fail_without_mutating_input(self):
        valid = self.execution_config()
        for cfg, rule in ((dict(valid, agentRules={}), "new"), (valid, "")):
            original = copy.deepcopy(cfg)
            with self.assertRaises(eff.InvalidEvidence):
                eff.propose_rules(cfg, rule, "new", reference_config=original)
            self.assertEqual(original, cfg)

    def test_missing_independent_reference_rejected(self):
        with self.assertRaises(eff.InvalidEvidence):
            eff.propose_rules(self.execution_config(), "worker", "coordinator")

    def test_two_lossy_copies_cannot_satisfy_known_role_preconditions(self):
        for role in ("worker", "orchestrator"):
            for key in ("model", "effort"):
                lossy = self.execution_config()
                del lossy[role]["agentConfig"][key]
                with self.assertRaises(eff.InvalidEvidence):
                    eff.propose_rules(lossy, "worker", "coordinator",
                                      reference_config=copy.deepcopy(lossy))

    def test_each_missing_effort_and_unknown_field_rejected_without_mutation(self):
        reference = {"worker": {"agentConfig": {"model": "gpt-6.1-sol", "effort": "high"}},
                     "orchestrator": {"agentConfig": {"model": "gpt-6.1-sol", "effort": "medium"}},
                     "futureUnknownField": {"private": SECRET}, "autoReview": True}
        for path in (("worker", "agentConfig", "effort"),
                     ("orchestrator", "agentConfig", "effort"), ("futureUnknownField",)):
            captured = copy.deepcopy(reference)
            parent = captured
            for key in path[:-1]:
                parent = parent[key]
            del parent[path[-1]]
            before = copy.deepcopy(captured)
            with self.assertRaises(eff.InvalidEvidence):
                eff.propose_rules(captured, "worker", "coordinator", reference_config=reference)
            self.assertEqual(captured, before)
            self.assertIn("effort", reference["worker"]["agentConfig"])

    def test_changed_type_and_reference_drift_rejected(self):
        for reference, captured in (({"autoReview": True}, {"autoReview": 1}),
                                    ({"future": "new"}, {"future": "old"})):
            with self.assertRaises(eff.InvalidEvidence):
                eff.propose_rules(dict(self.execution_config(), **captured), "worker", "coordinator",
                                  reference_config=dict(self.execution_config(), **reference))

    def test_actual_installed_cli_projection_is_rejected(self):
        # Only allowlisted presence booleans from a read-only installed-CLI vs
        # saved-config comparison; no live configuration values are in Git.
        fixture = SCRIPT.parents[1] / "tests/fixtures/agent-efficiency/installed-config-capture-schema.json"
        observed = json.loads(fixture.read_text())
        reference = {"worker": {"agentConfig": {"model": "gpt-6.1-sol", "effort": "high"}},
                     "orchestrator": {"agentConfig": {"model": "gpt-6.1-sol", "effort": "medium"}}}
        captured = copy.deepcopy(reference)
        for path, presence in observed["allowlisted_presence"].items():
            if presence["saved_config"] and not presence["cli_get"]:
                parent = captured
                keys = path.split(".")
                for key in keys[:-1]:
                    parent = parent[key]
                del parent[keys[-1]]
        self.assertNotEqual(captured, reference)
        with self.assertRaises(eff.InvalidEvidence):
            eff.propose_rules(captured, "worker", "coordinator", reference_config=reference)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.input = self.root / "fixture.jsonl"
        self.input.write_text("".join(json.dumps(r) + "\n" for r in session([accounting()])))

    def run_cli(self, *args):
        return subprocess.run([sys.executable, "-B", str(SCRIPT), *map(str, args)],
                              capture_output=True, text=True, timeout=10)

    def test_success_and_concurrent_read_only_runs(self):
        before = {p.name: p.read_bytes() for p in self.root.iterdir()}
        with ThreadPoolExecutor(max_workers=3) as pool:
            results = list(pool.map(lambda _: self.run_cli("worker=" + str(self.input)), range(3)))
        self.assertTrue(all(r.returncode == 0 for r in results))
        self.assertTrue(all(r.stdout == results[0].stdout for r in results))
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.iterdir()})
        self.assertNotIn(SECRET, results[0].stdout + results[0].stderr)

    def test_representative_separate_vs_batched_collection(self):
        # Same 3 inputs and accounting output. No models or Guardians are invoked.
        paths = []
        for i, role in enumerate(("worker", "guardian", "completion"), 1):
            path = self.root / (role + ".jsonl")
            sid = f"00000000-0000-0000-0000-{i:012d}"
            path.write_text("".join(json.dumps(r) + "\n" for r in
                                    session([accounting(f"resp_{j}") for j in range(100)], sid)))
            paths.append(role + "=" + str(path))
        cpu = lambda: resource.getrusage(resource.RUSAGE_CHILDREN).ru_utime + resource.getrusage(resource.RUSAGE_CHILDREN).ru_stime
        cpu_start = cpu(); start = time.perf_counter()
        separate = [self.run_cli(p) for p in paths]
        separate_seconds = time.perf_counter() - start
        separate_cpu = cpu() - cpu_start
        cpu_start = cpu(); start = time.perf_counter()
        batched = self.run_cli(*paths)
        batched_seconds = time.perf_counter() - start
        batched_cpu = cpu() - cpu_start
        self.assertEqual(batched.returncode, 0)
        self.assertTrue(all(r.returncode == 0 for r in separate))
        expected = [json.loads(r.stdout)["sessions"][0]["accounting"] for r in separate]
        self.assertEqual(expected, [s["accounting"] for s in json.loads(batched.stdout)["sessions"]])
        print(json.dumps({"fixture": "3 sessions x 100 response records", "separate_processes": 3,
                          "batched_processes": 1, "separate_wall_seconds": separate_seconds,
                          "batched_wall_seconds": batched_seconds,
                          "separate_child_cpu_seconds": separate_cpu,
                          "batched_child_cpu_seconds": batched_cpu,
                          "separate_stdout_bytes": sum(len(r.stdout.encode()) for r in separate),
                          "batched_stdout_bytes": len(batched.stdout.encode()),
                          "model_tokens": "not measured; no model benchmark",
                          "guardian_tokens": "not measured; no Guardian benchmark"}))

    def test_missing_corrupt_partial_and_duplicate_inputs_no_partial_export(self):
        corrupt = self.root / "bad.jsonl"; corrupt.write_text(self.input.read_text() + SECRET)
        for args in (("worker=" + str(self.root / SECRET),), ("worker=" + str(corrupt),),
                     ("worker=" + str(self.input), "worker=" + str(self.input))):
            result = self.run_cli(*args)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertNotIn(SECRET, result.stderr)

    def test_symlink_directory_fifo_and_output_options_rejected_without_writes(self):
        link = self.root / "link"; link.symlink_to(self.input)
        fifo = self.root / "pipe"; os.mkfifo(fifo)
        for name in (link, fifo, self.root):
            result = self.run_cli("worker=" + str(name))
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
        # A safely owned stand-in for protected/unrelated evidence; never use a real protected path.
        protected = self.root / "protected"; protected.write_text(SECRET)
        result = self.run_cli("worker=" + str(self.input), "--output", protected)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(protected.read_text(), SECRET)
        self.assertNotIn(SECRET, result.stderr)

    def test_untrusted_malformed_fields_do_not_leak_via_tracebacks(self):
        records = session([record("turn_context", {"model": [SECRET]})])
        self.input.write_text("".join(json.dumps(r) + "\n" for r in records))
        result = self.run_cli("worker=" + str(self.input))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertNotIn(SECRET, result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_analyzer_succeeds_with_filesystem_writes_processes_and_network_denied(self):
        # This audit hook is a test tripwire, not a security sandbox. It uses
        # only our fixture, never a real protected worktree or evidence path.
        program = '''
import os, runpy, sys
def guard(event, args):
    if event == "open" and args[2] & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND):
        raise PermissionError("write denied by fixture")
    if event in ("os.remove", "os.rename", "os.mkdir", "os.rmdir", "os.chmod", "os.link",
                 "os.symlink", "os.truncate", "os.utime", "subprocess.Popen", "os.system", "socket.__new__"):
        raise PermissionError("side effect denied by fixture")
sys.addaudithook(guard)
sys.argv = sys.argv[1:]
runpy.run_path(sys.argv[0], run_name="__main__")
'''
        result = subprocess.run([sys.executable, "-B", "-c", program, str(SCRIPT),
                                 "worker=" + str(self.input)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["sessions"][0]["accounting"]["total_tokens"], 14)


if __name__ == "__main__":
    unittest.main()

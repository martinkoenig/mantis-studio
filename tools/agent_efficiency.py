"""Read-only, allowlisted Codex rollout accounting; no transcript text is emitted.

Run: python3 tools/agent_efficiency.py worker=/local/rollout.jsonl ...
Only sanitized JSON goes to stdout. Redirect it only to an authorized evidence
location. This is an analyzer, not an executor, sandbox, or AO config client.
"""

import argparse
import ast
import copy
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import shlex
import stat
import sys


ROLES = {"worker", "guardian", "completion", "orchestrator"}
COUNTERS = ("input_tokens", "cached_input_tokens", "output_tokens",
            "reasoning_output_tokens", "total_tokens")
TOOLS = {"exec", "wait", "exec_command", "write_stdin", "sleep"}
MODELS = {"gpt-6.1-sol", "codex-auto-review"}
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL)


class InvalidEvidence(ValueError):
    """Errors deliberately exclude source text, paths, and exception details."""


def mapping(value):
    if not isinstance(value, dict):
        raise InvalidEvidence("invalid object schema")
    return value


def identity(value):
    if isinstance(value, str) and re.fullmatch(
            r"(?:[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}|call_[A-Za-z0-9_-]{1,100})",
            value):
        return value
    return None


def timestamp(value):
    if not isinstance(value, str):
        raise InvalidEvidence("missing timestamp")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.utcoffset() is None:
            raise ValueError
        return parsed.astimezone(timezone.utc).isoformat()
    except ValueError:
        raise InvalidEvidence("invalid timestamp") from None


def usage(value):
    value = mapping(value)
    result = {}
    for name in COUNTERS:
        number = value.get(name)
        if type(number) is not int or number < 0:
            raise InvalidEvidence("invalid token counter")
        result[name] = number
    if (result["cached_input_tokens"] > result["input_tokens"]
            or result["reasoning_output_tokens"] > result["output_tokens"]
            or result["total_tokens"] != result["input_tokens"] + result["output_tokens"]):
        raise InvalidEvidence("inconsistent token counters")
    return result


def category(source):
    """Heuristic action categories, never claims about authorization or intent."""
    if re.search(r"\bwrite_stdin\b", source):
        return "process_status_or_input"
    if re.search(r"\b(?:clock\.sleep|tools\.sleep)\b", source):
        return "agent_sleep"
    if re.search(r"\bgh\s+(?:run|pr\s+checks)\b", source):
        return "provider_status"
    if re.search(r"\bctest\b", source):
        return "test_or_test_inspection"
    if re.search(r"\bcmake\b", source):
        return "build_or_build_inspection"
    if re.search(r"\bao\s+(?:session|status)\b", source):
        return "ao_status"
    if re.search(r"\bgit\b", source):
        return "git"
    if re.search(r"\b(?:rg|cat|sed|head|tail)\b", source):
        return "file_inspection"
    return "other"


def literals(source):
    result = set()
    for match in STRING.finditer(source):
        try:
            value = ast.literal_eval(match.group())
        except (ValueError, SyntaxError):
            continue
        if isinstance(value, str):
            result.add(hashlib.sha256(value.encode("utf-8")).digest())
    return result


def review_requests(text):
    """Extract structured requested-action objects locally, never return prose."""
    decoder = json.JSONDecoder()
    for match in re.finditer(r"\{", text):
        try:
            value, _ = decoder.raw_decode(text[match.start():])
        except ValueError:
            continue
        if (isinstance(value, dict)
                and {"command", "tool", "sandbox_permissions", "justification"} <= value.keys()):
            yield value


def executable_category(command):
    try:
        parts = shlex.split(command)
        head = parts[0] if parts else ""
    except ValueError:
        head = ""
    return head if head in ("tail", "sed", "cat", "git", "cmake", "ctest", "python3",
                            "mkdir", "cp", "rg", "bash") else "other"


def propose_rules(config, worker_rules, orchestrator_rules, *, reference_config=None):
    """Offline only. Preserve every unknown/config/model/security field verbatim.

    The caller must obtain reference_config from an independently verified
    complete source. Comparing two copies of a lossy capture proves nothing.
    The installed AO CLI cannot supply this reference or apply it losslessly.
    These copies do not establish an activation or rollback mechanism. This
    helper verifies neither transport fidelity nor provenance and does no AO I/O.
    """
    mapping(config)
    mapping(reference_config)
    try:
        captured = json.dumps(config, sort_keys=True, allow_nan=False)
        reference = json.dumps(reference_config, sort_keys=True, allow_nan=False)
    except (TypeError, ValueError):
        raise InvalidEvidence("invalid configuration schema") from None
    if captured != reference:
        raise InvalidEvidence("capture differs from complete reference")
    for role in ("worker", "orchestrator"):
        settings = mapping(mapping(config.get(role)).get("agentConfig"))
        if not all(isinstance(settings.get(key), str) and settings[key].strip()
                   for key in ("model", "effort")):
            raise InvalidEvidence("missing role execution setting")
    if not all(isinstance(x, str) and x.strip() for x in (worker_rules, orchestrator_rules)):
        raise InvalidEvidence("empty rules")
    original = copy.deepcopy(config)
    proposed = copy.deepcopy(config)
    # Preserve existing instructions; a separate review must resolve conflicts.
    for key, addition in (("agentRules", worker_rules), ("orchestratorRules", orchestrator_rules)):
        existing = proposed.get(key, "")
        if not isinstance(existing, str):
            raise InvalidEvidence("invalid rules schema")
        if addition not in existing:
            proposed[key] = existing.rstrip() + ("\n\n" if existing.strip() else "") + addition
    return original, proposed


def collect(stream, role):
    if role not in ROLES:
        raise InvalidEvidence("invalid role")
    totals = Counter({name: 0 for name in COUNTERS})
    records = {}; cumulative = []; events = []; requests = []; settings = []
    tasks = 0; first = None; last = None; sid = None; fingerprint = hashlib.sha256()
    compaction_ids = set()
    reasoning_included = set(); duplicates = 0
    for raw in stream:
        fingerprint.update(raw.encode("utf-8"))
        try:
            item = mapping(json.loads(raw))
            when = timestamp(item.get("timestamp"))
            body = mapping(item.get("payload", {}))
            kind = item.get("type")
            first = min(first, when) if first else when
            last = max(last, when) if last else when
            if kind == "session_meta":
                candidate = identity(body.get("id"))
                if candidate is None or (sid is not None and candidate != sid):
                    raise InvalidEvidence("invalid session boundary")
                sid = candidate
            elif kind == "turn_context":
                policy = mapping(body.get("sandbox_policy", {}))
                setting = {
                    "model": body.get("model") if body.get("model") in MODELS else "other",
                    "effort": body.get("effort") if body.get("effort") in
                    ("low", "medium", "high", "xhigh") else "other",
                    "approval_policy": body.get("approval_policy") if body.get("approval_policy") in
                    ("on-request", "never", "untrusted", "on-failure") else "other",
                    "reviewer": body.get("approvals_reviewer") if body.get("approvals_reviewer") in
                    ("auto_review", "user") else "other",
                    "sandbox": policy.get("type") if policy.get("type") in
                    ("workspace-write", "read-only", "danger-full-access") else "other",
                    "network_access": policy.get("network_access") if
                    type(policy.get("network_access")) is bool else None,
                }
                if setting not in settings:
                    settings.append(setting)
            elif kind == "token_usage_record":
                # Reviewer threads can share a runtime session_id with their
                # parent. Native rollout identity is the accounting thread_id.
                if body.get("thread_id", sid) != sid:
                    raise InvalidEvidence("foreign accounting thread")
                response = body.get("response_id")
                if not isinstance(response, str) or not response:
                    raise InvalidEvidence("missing accounting identity")
                value = usage(body.get("usage"))
                if response in records:
                    if records[response] != value:
                        raise InvalidEvidence("conflicting accounting identity")
                    duplicates += 1
                else:
                    records[response] = value
            elif kind == "compacted":
                response = body.get("compaction_response_id")
                if isinstance(response, str):
                    compaction_ids.add(response)
            elif kind == "event_msg":
                if body.get("type") == "task_started":
                    tasks += 1
                elif body.get("type") == "token_count" and body.get("info") is not None:
                    info = mapping(body["info"])
                    cumulative.append(usage(info.get("total_token_usage")))
                    flag = info.get("last_usage_includes_reasoning")
                    if type(flag) is not bool:
                        raise InvalidEvidence("unknown reasoning accounting semantics")
                    reasoning_included.add(flag)
            elif kind == "response_item":
                typ = body.get("type")
                if typ in ("custom_tool_call", "function_call"):
                    tool = body.get("name", "")
                    tool = tool.rsplit(".", 1)[-1] if isinstance(tool, str) else "other"
                    source = body.get("input", body.get("arguments", ""))
                    if not isinstance(source, str):
                        raise InvalidEvidence("invalid tool schema")
                    events.append({"timestamp": when, "call_id": identity(body.get("call_id")),
                                   "tool": tool if tool in TOOLS else "other",
                                   "category": "cell_wait" if tool == "wait" else category(source),
                                   "_literals": literals(source)})
                elif typ == "message" and body.get("role") == "user" and role == "guardian":
                    content = body.get("content", [])
                    if not isinstance(content, list):
                        raise InvalidEvidence("invalid content schema")
                    for part in content:
                        part = mapping(part)
                        text = part.get("text", "")
                        if not isinstance(text, str):
                            raise InvalidEvidence("invalid text schema")
                        for request in review_requests(text):
                            command = request["command"]
                            if (not isinstance(command, list) or not command
                                    or not all(isinstance(x, str) for x in command)):
                                raise InvalidEvidence("invalid requested command schema")
                            command = command[-1]
                            requests.append({"timestamp": when, "category": category(command),
                                             "executable_category": executable_category(command),
                                             "escalation_requested": request["sandbox_permissions"] ==
                                             "require_escalated", "_command_digest":
                                             hashlib.sha256(command.encode("utf-8")).digest()})
        except (ValueError, TypeError, AttributeError, OverflowError):
            raise InvalidEvidence("invalid evidence record") from None
    if sid is None:
        raise InvalidEvidence("missing session identity")
    resets = 0
    if records:
        for value in records.values():
            totals.update(value)
        source = "unique_response_usage"
    else:
        previous = {name: 0 for name in COUNTERS}
        for value in cumulative:
            if value["total_tokens"] < previous["total_tokens"]:
                resets += 1
                previous = {name: 0 for name in COUNTERS}
            delta = {name: value[name] - previous[name] for name in COUNTERS}
            if any(x < 0 for x in delta.values()):
                raise InvalidEvidence("ambiguous cumulative counter reset")
            totals.update(delta)
            previous = value
        source = "cumulative_reset_segments" if cumulative else "unavailable"
    if reasoning_included and reasoning_included != {True}:
        raise InvalidEvidence("unsupported reasoning accounting semantics")
    totals["uncached_input_tokens"] = totals["input_tokens"] - totals["cached_input_tokens"]
    compaction_tokens = sum(records[x]["total_tokens"] for x in compaction_ids if x in records)
    result = {"role": role, "session_id": sid, "sha256": fingerprint.hexdigest(),
              "first_timestamp": first, "last_timestamp": last, "tasks_started": tasks,
              "settings": settings, "accounting": {"source": source, "responses": len(records),
              "duplicate_responses": duplicates, "cumulative_resets_used": resets,
              "identified_compaction_tokens_included": compaction_tokens,
              "terminal_cumulative_total_tokens": cumulative[-1]["total_tokens"] if cumulative else None,
              "reasoning_included_in_output_confirmed": reasoning_included == {True},
              **totals}, "tool_calls": len(events),
              "tool_categories": dict(Counter(x["category"] for x in events)),
              "review_requests": len(requests),
              "review_categories": dict(Counter(x["category"] for x in requests)),
              "review_executable_categories": dict(Counter(x["executable_category"] for x in requests)),
              "repeated_exact_review_commands": len(requests) - len({x["_command_digest"] for x in requests}),
              "escalation_requests": sum(x["escalation_requested"] for x in requests),
              "events": events, "requests": requests}
    return result


def sanitize(sessions):
    """Join only exact command literals preceding a review; not a causal claim."""
    workers = [event for s in sessions if s["role"] == "worker" for event in s["events"]]
    for session in sessions:
        for request in session["requests"]:
            candidates = [e for e in workers if e["timestamp"] <= request["timestamp"]
                          and request["_command_digest"] in e["_literals"]]
            request["correlation"] = "unique_exact_literal_preceding" if len(candidates) == 1 else "unmapped"
            request["candidate_call_id"] = candidates[0]["call_id"] if len(candidates) == 1 else None
            del request["_command_digest"]
    for session in sessions:
        for event in session["events"]:
            del event["_literals"]
    return {"schema_version": 1, "sessions": sessions,
            "limitations": ["Categories are lexical heuristics, not authorization or waste judgments.",
                            "Command/time correlation does not prove Guardian/tool causal identity.",
                            "No prices, financial cost, CPU, or unrecorded activations inferred.",
                            "Reasoning is a subset of output, cached input a subset of input."]}


class PrivateArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        self.exit(2, "invalid invocation (expected role=local-rollout.jsonl)\n")


def open_evidence(filename):
    """Regular files only, no symlink following or FIFO/device reads (POSIX)."""
    fd = os.open(filename, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise InvalidEvidence("evidence is not a regular file")
        return os.fdopen(fd, encoding="utf-8")
    except BaseException:
        os.close(fd)
        raise


def main(argv=None):
    parser = PrivateArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args(argv)
    try:
        sessions = []; seen = set()
        for spec in args.inputs:
            role, sep, filename = spec.partition("=")
            if not sep or role not in ROLES:
                raise InvalidEvidence("invalid input specification")
            # No output/evidence-path option exists; no directories are created.
            with open_evidence(filename) as stream:
                session = collect(stream, role)
            if session["session_id"] in seen:
                raise InvalidEvidence("duplicate session input")
            seen.add(session["session_id"])
            sessions.append(session)
        result = sanitize(sessions)
    except (OSError, UnicodeError, InvalidEvidence, RecursionError):
        print("invalid or unavailable evidence; no partial export", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())

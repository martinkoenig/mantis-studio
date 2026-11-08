import copy
import importlib.util
import io
import json
import pathlib
import sys
import unittest
import urllib.error
import urllib.parse

MODULE = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "github_milestones.py"
spec = importlib.util.spec_from_file_location("github_milestones", MODULE)
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)


def event(sender=mod.OWNER_ID, author=mod.OWNER_ID, label=mod.TRIGGER_LABEL, body=""):
    return {
        "action": "labeled", "repository": {"full_name": mod.REPOSITORY},
        "sender": {"id": sender}, "label": {"name": label},
        "issue": {"number": 16, "state": "open", "user": {"id": author}, "body": body},
    }


def request(items=None):
    if items is None:
        items = [{"title": "ENG-QA - Reliable CI", "description": "Engineering qualification",
                  "issue_numbers": [11, 12]}]
    return f"Before\n{mod.START}{json.dumps({'milestones': items})}{mod.END}\nAfter"


def milestone(number=1, title="ENG-QA - Reliable CI", description="Engineering qualification", state="open"):
    return {"number": number, "title": title, "description": description, "state": state}


class Crash(RuntimeError):
    pass


class FakeApi:
    """Independent server state; reads return snapshots, writes persist across runs.

    before/after hooks model a concurrent actor or failure at an API boundary.
    Calls record attempts; effects record accepted writes, including lost replies.
    """
    def __init__(self, milestones=None, body=None):
        self.body = request() if body is None else body
        self.event = event(body=self.body)
        self.milestones = copy.deepcopy(milestones or [])
        self.issues = {
            11: {"number": 11, "state": "open", "milestone": None},
            12: {"number": 12, "state": "open", "milestone": None},
            16: dict(copy.deepcopy(self.event["issue"]),
                     labels=[{"name": mod.TRIGGER_LABEL}], milestone=None),
        }
        self.comments = []
        self.calls = []
        self.effects = []
        self.counts = {}
        self.before = None
        self.after = None

    def writes(self):
        return [(m, p, b) for m, p, b in self.calls if m != "GET"]

    def call(self, method, path, body=None):
        self.calls.append((method, path, copy.deepcopy(body)))
        route = path.split("?")[0]
        key = method, route
        self.counts[key] = self.counts.get(key, 0) + 1
        if self.before:
            self.before(self, method, route, body)
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query)
        page = int(query.get("page", ["1"])[0])
        if method == "GET" and route == "milestones":
            result = self.milestones[(page - 1) * 100:page * 100]
        elif method == "GET" and route.startswith("milestones/"):
            number = int(route.split("/")[1])
            result = next((x for x in self.milestones if x["number"] == number), None)
            if result is None:
                raise mod.ApiError("Missing milestone", 404)
        elif method == "GET" and route == "issues/16/comments":
            result = self.comments[(page - 1) * 100:page * 100]
        elif method == "GET" and route.startswith("issues/"):
            result = self.issues[int(route.split("/")[1])]
        elif method == "POST" and route == "milestones":
            if any(x["title"].casefold() == body["title"].casefold() for x in self.milestones):
                raise mod.ApiError("Milestone collision", 422)
            result = dict(body, number=max((x["number"] for x in self.milestones), default=0) + 1)
            self.milestones.append(result)
            self.effects.append((method, route, copy.deepcopy(body)))
        elif method == "PATCH" and route.startswith("issues/"):
            number = int(route.split("/")[1])
            self.issues[number]["milestone"] = {"number": body["milestone"]}
            result = self.issues[number]
            self.effects.append((method, route, copy.deepcopy(body)))
        elif method == "POST" and route == "issues/16/comments":
            result = {"id": len(self.comments) + 1, "body": body["body"],
                      "user": {"id": mod.ACTIONS_BOT_ID, "type": "Bot", "login": "github-actions[bot]"}}
            self.comments.append(result)
            self.effects.append((method, route, copy.deepcopy(body)))
        elif method == "DELETE" and route == "issues/16/labels/automation%3Acreate-milestone":
            labels = self.issues[16]["labels"]
            if not any(x["name"] == mod.TRIGGER_LABEL for x in labels):
                raise mod.ApiError("Label already absent", 404)
            self.issues[16]["labels"] = [x for x in labels if x["name"] != mod.TRIGGER_LABEL]
            result = None
            self.effects.append((method, route, None))
        else:
            raise AssertionError(f"Unexpected {method} {path}")
        result = copy.deepcopy(result)
        if self.after:
            self.after(self, method, route, body, result)
        return result

    def run(self):
        return mod.provision(self, copy.deepcopy(self.event))

    def retry(self):
        self.calls.clear()
        self.before = self.after = None
        return self.run()


class MilestoneTests(unittest.TestCase):
    def assert_completed_once(self, api):
        self.assertEqual(len(api.milestones), 1)
        self.assertEqual(api.issues[11]["milestone"], {"number": 1})
        self.assertEqual(api.issues[12]["milestone"], {"number": 1})
        self.assertEqual(len(api.comments), 1)
        self.assertEqual(api.comments[0]["user"]["id"], mod.ACTIONS_BOT_ID)
        self.assertNotIn({"name": mod.TRIGGER_LABEL}, api.issues[16]["labels"])
        self.assertEqual(sum(m == "POST" and p == "milestones" for m, p, _ in api.effects), 1)
        self.assertEqual(sum(m == "PATCH" for m, _, _ in api.effects), 2)
        self.assertEqual(sum(p.endswith("/comments") for _, p, _ in api.effects), 1)

    def test_owner_request(self):
        number, body = mod.validate_event(event(body=request()))
        self.assertEqual(number, 16)
        self.assertEqual(mod.parse_request(body)[0].issues, (11, 12))

    def test_foreign_author_rejected(self):
        api = FakeApi()
        api.event["issue"]["user"]["id"] = 99
        with self.assertRaisesRegex(mod.RequestError, "owner"):
            api.run()
        self.assertEqual(api.calls, [])

    def test_foreign_labeler_rejected(self):
        api = FakeApi()
        api.event["sender"]["id"] = 99
        with self.assertRaisesRegex(mod.RequestError, "owner"):
            api.run()
        self.assertEqual(api.calls, [])

    def test_wrong_repo_event_label_rejected(self):
        for mutate in (
            lambda e: e["repository"].update(full_name="attacker/fork"),
            lambda e: e["label"].update(name="bug"),
            lambda e: e.update(action="opened"),
        ):
            api = FakeApi()
            mutate(api.event)
            with self.subTest(mutate=mutate), self.assertRaises(mod.RequestError):
                api.run()
            self.assertEqual(api.calls, [])

    def test_pr_closed_and_malformed_source_rejected(self):
        for mutate in (
            lambda e: e["issue"].update(pull_request={}),
            lambda e: e["issue"].update(state="closed"),
            lambda e: e["issue"].update(number=True),
            lambda e: e["issue"].update(user=None),
            lambda e: e.update(repository=["bad"]),
        ):
            api = FakeApi()
            mutate(api.event)
            with self.subTest(mutate=mutate), self.assertRaises(mod.RequestError):
                api.run()
            self.assertEqual(api.calls, [])

    def test_invalid_body_fails_closed(self):
        cases = [
            "plain", request() + request(), mod.START + "{" + mod.END,
            request([{"title": "../evil", "description": "", "issue_numbers": [11]}]),
            request([{"title": "Good", "description": "", "issue_numbers": [11, 11]}]),
            request([{"title": "Good", "description": "", "issue_numbers": [True]}]),
            request([{"title": "Good", "description": "", "issue_numbers": [11]},
                     {"title": "good", "description": "", "issue_numbers": [12]}]),
            request([{"title": "Good", "description": "", "issue_numbers": [11]},
                     {"title": "Other", "description": "", "issue_numbers": [11]}]),
            request([{"title": "Good", "description": "", "issue_numbers": [11], "extra": 1}]),
            request([]), request([{"title": "Good", "description": "", "issue_numbers": [0]}]),
        ]
        for body in cases:
            api = FakeApi(body=body)
            with self.subTest(body=body[:60]), self.assertRaises(mod.RequestError):
                api.run()
            self.assertEqual(api.calls, [])

    def test_reversed_delimiters_rejected(self):
        body = mod.END + mod.START + json.dumps({"milestones": [
            {"title": "Good", "description": "", "issue_numbers": [11]}]})
        api = FakeApi(body=body)
        with self.assertRaisesRegex(mod.RequestError, "reversed"):
            api.run()
        self.assertEqual(api.calls, [])

    def test_duplicate_json_keys_rejected(self):
        for data in (
            '{"milestones":[],"milestones":[{"title":"Good","description":"","issue_numbers":[11]}]}',
            '{"milestones":[{"title":"Wrong","title":"Good","description":"","issue_numbers":[11]}]}',
            '{"milestones":[{"title":"Good","description":"Wrong","description":"","issue_numbers":[11]}]}',
            '{"milestones":[{"title":"Good","description":"","issue_numbers":[12],"issue_numbers":[11]}]}',
            '{"milestones":[{"title":"Wrong","\\u0074itle":"Good","description":"","issue_numbers":[11]}]}',
        ):
            api = FakeApi(body=mod.START + data + mod.END)
            with self.subTest(data=data), self.assertRaisesRegex(mod.RequestError, "Duplicate JSON"):
                api.run()
            self.assertEqual(api.calls, [])

    def test_request_bounds_and_exactly_one_ordered_block(self):
        valid = request()
        for body in (valid + "x" * 60_000, mod.START + " " * 8001 + mod.END,
                     mod.START + valid, valid + mod.END, valid.replace(mod.END, "")):
            with self.subTest(size=len(body)), self.assertRaises(mod.RequestError):
                mod.parse_request(body)
        for count, field in ((6, "milestones"), (16, "issue_numbers")):
            items = [{"title": f"M{i}", "description": "", "issue_numbers": [i + 1]}
                     for i in range(count)] if field == "milestones" else [
                         {"title": "M", "description": "", "issue_numbers": list(range(1, count + 1))}]
            with self.subTest(field=field), self.assertRaises(mod.RequestError):
                mod.parse_request(request(items))

    def test_create_assign_comment_clear_label(self):
        api = FakeApi()
        results = api.run()
        self.assert_completed_once(api)
        self.assertEqual(len(results), 1)
        self.assertIn("/milestone/1", results[0])

    def test_revoked_source_causes_zero_writes(self):
        mutations = {
            "label removed": lambda s: s.update(labels=[]),
            "closed": lambda s: s.update(state="closed"),
            "converted to PR": lambda s: s.update(pull_request={}),
            "foreign author": lambda s: s.update(user={"id": 99}),
            "different body": lambda s: s.update(body=request() + " edited"),
            "changed JSON intent": lambda s: s.update(body=request([
                {"title": "Other", "description": "", "issue_numbers": [11]}])),
            "wrong source identity": lambda s: s.update(number=17),
        }
        for name, mutate in mutations.items():
            api = FakeApi()
            mutate(api.issues[16])
            with self.subTest(name=name):
                self.assertEqual(api.run(), [])
                self.assertEqual(api.writes(), [])
                self.assertEqual(api.effects, [])

    def test_revoked_after_preflight_no_milestone_or_issue_or_success_writes(self):
        api = FakeApi()
        def hook(a, m, p, b, result):
            if (m, p) == ("GET", "issues/12"):
                a.issues[16]["labels"] = []
        api.after = hook
        self.assertEqual(api.run(), [])
        self.assertEqual(api.writes(), [])
        self.assertEqual(api.effects, [])

    def test_revoked_before_patch_preserves_partial_milestone_only(self):
        api = FakeApi()
        def hook(a, m, p, b):
            if (m, p) == ("GET", "issues/11") and a.counts[m, p] == 2:
                a.issues[16]["body"] += " changed"
        api.before = hook
        self.assertEqual(api.run(), [])
        self.assertEqual([p for _, p, _ in api.effects], ["milestones"])
        self.assertIsNone(api.issues[11]["milestone"])
        self.assertEqual(api.comments, [])

    def test_revocation_between_assignments_stops_remaining_writes(self):
        api = FakeApi()
        def hook(a, m, p, b, result):
            if (m, p) == ("PATCH", "issues/11"):
                a.issues[16]["labels"] = []
        api.after = hook
        self.assertEqual(api.run(), [])
        self.assertEqual([p for _, p, _ in api.effects], ["milestones", "issues/11"])
        self.assertIsNone(api.issues[12]["milestone"])
        self.assertEqual(api.comments, [])

    def test_revoked_before_completion_has_no_success_or_cleanup(self):
        api = FakeApi()
        def hook(a, m, p, b, result):
            if (m, p) == ("GET", "issues/12") and all(a.issues[n]["milestone"] for n in (11, 12)):
                a.issues[16]["state"] = "closed"
        api.after = hook
        self.assertEqual(api.run(), [])
        self.assertEqual(sum(m == "PATCH" for m, _, _ in api.effects), 2)
        self.assertFalse(any(m == "DELETE" or p.endswith("/comments") for m, p, _ in api.writes()))

    def test_revoked_before_cleanup_does_not_delete_changed_source_label(self):
        api = FakeApi()
        def hook(a, m, p, b, result):
            if (m, p) == ("POST", "issues/16/comments"):
                a.issues[16]["body"] += " changed"
        api.after = hook
        self.assertEqual(api.run(), [])
        self.assertEqual(len(api.comments), 1)
        self.assertFalse(any(m == "DELETE" for m, _, _ in api.writes()))
        self.assertEqual(api.retry(), [])
        self.assertEqual(api.writes(), [])

    def test_idempotent_replay(self):
        api = FakeApi()
        api.run()
        self.assertEqual(api.retry(), [])
        self.assertEqual(api.writes(), [])
        self.assert_completed_once(api)

    def test_relabelled_completed_request_only_cleans_label(self):
        api = FakeApi()
        api.run()
        api.issues[16]["labels"] = [{"name": mod.TRIGGER_LABEL}]
        api.retry()
        self.assertEqual([m for m, _, _ in api.writes()], ["DELETE"])
        self.assert_completed_once(api)

    def test_partial_failure_and_crash_replay(self):
        points = [("POST", "milestones"), ("PATCH", "issues/11"),
                  ("POST", "issues/16/comments"), ("DELETE", "issues/16/labels/automation%3Acreate-milestone")]
        for point in points:
            api = FakeApi()
            def hook(a, m, p, b, result):
                if (m, p) == point:
                    raise Crash("Process stopped after committed effect")
            api.after = hook
            with self.subTest(point=point), self.assertRaises(Crash):
                api.run()
            api.retry()
            self.assert_completed_once(api)

    def test_lost_write_responses_reconcile_without_duplicate_effects(self):
        points = [("POST", "milestones"), ("PATCH", "issues/11"),
                  ("POST", "issues/16/comments"), ("DELETE", "issues/16/labels/automation%3Acreate-milestone")]
        for point in points:
            api = FakeApi()
            fired = False
            def hook(a, m, p, b, result):
                nonlocal fired
                if (m, p) == point and not fired:
                    fired = True
                    raise mod.ApiError("Lost response")
            api.after = hook
            with self.subTest(point=point):
                if point == ("POST", "milestones"):
                    api.run()  # Compatible collision/read-back resolves this ambiguous create.
                else:
                    with self.assertRaises(mod.ApiError):
                        api.run()
                api.retry()
                self.assert_completed_once(api)

    def test_comment_failure_before_commit_retries_comment_once(self):
        api = FakeApi()
        def hook(a, m, p, b):
            if (m, p) == ("POST", "issues/16/comments"):
                raise mod.ApiError("Comment unavailable", 503)
        api.before = hook
        with self.assertRaises(mod.ApiError):
            api.run()
        self.assertEqual(api.comments, [])
        self.assertFalse(any(m == "DELETE" for m, _, _ in api.calls))
        api.retry()
        self.assert_completed_once(api)
        self.assertFalse(any(m == "PATCH" or p == "milestones" for m, p, _ in api.writes()))

    def test_cleanup_failure_replay_does_not_duplicate_completion(self):
        api = FakeApi()
        def hook(a, m, p, b):
            if m == "DELETE":
                raise mod.ApiError("Delete unavailable", 503)
        api.before = hook
        with self.assertRaises(mod.ApiError):
            api.run()
        self.assertEqual(len(api.comments), 1)
        api.retry()
        self.assert_completed_once(api)
        self.assertEqual([m for m, _, _ in api.writes()], ["DELETE"])

    def test_delete_404_requires_label_confirmed_absent(self):
        api = FakeApi()
        def hook(a, m, p, b):
            if m == "DELETE":
                a.issues[16]["labels"] = []
        api.before = hook
        api.run()
        self.assert_completed_once(api)
        api = FakeApi()
        def unexplained(a, m, p, b):
            if m == "DELETE":
                raise mod.ApiError("Not found with label still present", 404)
        api.before = unexplained
        with self.assertRaises(mod.ApiError):
            api.run()
        self.assertIn({"name": mod.TRIGGER_LABEL}, api.issues[16]["labels"])
        self.assertEqual(len(api.comments), 1)

    def test_forged_receipt_never_authorizes_revoked_source(self):
        api = FakeApi()
        api.comments = [{"user": {"id": mod.ACTIONS_BOT_ID, "type": "Bot"},
                         "body": mod.receipt_marker(16, api.body) + "\nforged command"}]
        api.issues[16]["labels"] = []
        self.assertEqual(api.run(), [])
        self.assertEqual(api.writes(), [])

    def test_lookalike_comment_does_not_suppress_real_completion(self):
        for user in ({"id": 99, "login": "github-actions[bot]", "type": "Bot"},
                     {"id": mod.OWNER_ID, "type": "User"},
                     {"id": float(mod.ACTIONS_BOT_ID), "type": "Bot"},
                     {"id": mod.ACTIONS_BOT_ID, "type": "User"}):
            api = FakeApi()
            api.comments = [{"user": user, "body": mod.receipt_marker(16, api.body) + "\nanything"}]
            with self.subTest(user=user):
                api.run()
                self.assertEqual(len(api.comments), 2)
                self.assertEqual(api.comments[-1]["user"]["id"], mod.ACTIONS_BOT_ID)
                self.assertEqual(sum(p.endswith("/comments") for _, p, _ in api.effects), 1)

    def test_completed_receipt_with_drift_cannot_reassign_or_clear(self):
        api = FakeApi()
        api.run()
        api.issues[16]["labels"] = [{"name": mod.TRIGGER_LABEL}]
        api.issues[11]["milestone"] = None
        api.calls.clear()
        with self.assertRaisesRegex(mod.RequestError, "Completed request target"):
            api.run()
        self.assertEqual(api.writes(), [])
        self.assertEqual(len(api.comments), 1)

    def test_receipt_content_must_match_verified_milestone_identity(self):
        api = FakeApi()
        api.run()
        api.issues[16]["labels"] = [{"name": mod.TRIGGER_LABEL}]
        api.comments[0]["body"] += "\nextra command"
        api.calls.clear()
        with self.assertRaisesRegex(mod.RequestError, "receipt does not match"):
            api.run()
        self.assertEqual(api.writes(), [])

    def test_receipt_lookup_paginates_and_is_bounded_before_writes(self):
        api = FakeApi()
        api.run()
        api.issues[16]["labels"] = [{"name": mod.TRIGGER_LABEL}]
        junk = {"user": {"id": 99}, "body": "ordinary discussion"}
        api.comments[:0] = [copy.deepcopy(junk) for _ in range(100)]
        api.retry()
        self.assertEqual(sum(m == "GET" and "/comments?" in p for m, p, _ in api.calls), 2)
        self.assertEqual([m for m, _, _ in api.writes()], ["DELETE"])
        api = FakeApi()
        api.comments = [copy.deepcopy(junk) for _ in range(1000)]
        with self.assertRaisesRegex(mod.RequestError, "bounded limit"):
            api.run()
        self.assertEqual(api.writes(), [])
        self.assertEqual(sum("/comments?" in p for _, p, _ in api.calls), 10)

    def test_ambiguous_bot_receipts_fail_without_writes(self):
        api = FakeApi()
        api.run()
        api.issues[16]["labels"] = [{"name": mod.TRIGGER_LABEL}]
        api.comments.append(copy.deepcopy(api.comments[0]))
        api.calls.clear()
        with self.assertRaisesRegex(mod.RequestError, "Ambiguous duplicate"):
            api.run()
        self.assertEqual(api.writes(), [])

    def test_preflight_conflicting_assignment_no_writes(self):
        api = FakeApi()
        api.issues[11]["milestone"] = {"number": 99}
        with self.assertRaisesRegex(mod.RequestError, "different milestone"):
            api.run()
        self.assertEqual(api.writes(), [])

    def test_preflight_rejects_pr_closed_or_malformed_target(self):
        for change in ({"pull_request": {}}, {"state": "closed"}, {"number": 99},
                       {"milestone": {}}, {"milestone": {"number": True}}):
            api = FakeApi()
            api.issues[11].update(change)
            with self.subTest(change=change), self.assertRaises(mod.RequestError):
                api.run()
            self.assertEqual(api.writes(), [])

    def test_target_state_race_rechecked_even_if_already_assigned(self):
        for assigned in (False, True):
            for change in ({"state": "closed"}, {"pull_request": {}}, {"milestone": {"number": 99}}):
                api = FakeApi(milestones=[milestone()])
                if assigned:
                    api.issues[11]["milestone"] = {"number": 1}
                def hook(a, m, p, b):
                    if (m, p) == ("GET", "issues/11") and a.counts[m, p] == 2:
                        a.issues[11].update(change)
                api.before = hook
                with self.subTest(assigned=assigned, change=change), self.assertRaises(mod.RequestError):
                    api.run()
                self.assertEqual(api.writes(), [])
                self.assertEqual(api.comments, [])

    def test_patch_effects_are_verified_before_completion(self):
        api = FakeApi()
        def hook(a, m, p, b, result):
            if (m, p) == ("PATCH", "issues/12"):
                a.issues[12]["milestone"] = None
        api.after = hook
        with self.assertRaisesRegex(mod.RequestError, "Completed request target"):
            api.run()
        self.assertEqual(api.comments, [])
        self.assertFalse(any(m == "DELETE" for m, _, _ in api.writes()))

    def test_incompatible_existing_milestones_rejected_before_any_writes(self):
        for change in ({"state": "closed"}, {"title": "eng-qa - reliable ci"},
                       {"description": "Other intent"}, {"description": None}, {"number": True}):
            row = dict(milestone(), **change)
            api = FakeApi(milestones=[row])
            with self.subTest(change=change), self.assertRaisesRegex(mod.RequestError, "incompatible"):
                api.run()
            self.assertEqual(api.writes(), [])

    def test_empty_description_compatible_with_api_null(self):
        api = FakeApi(milestones=[milestone(description=None)], body=request([
            {"title": "ENG-QA - Reliable CI", "description": "", "issue_numbers": [11, 12]}]))
        api.run()
        self.assertFalse(any(m == "POST" and p == "milestones" for m, p, _ in api.writes()))
        self.assertEqual(len(api.comments), 1)

    def test_milestone_metadata_and_identity_revalidated_before_assignment(self):
        for change in ({"state": "closed"}, {"description": "Changed"}, {"number": 999}):
            api = FakeApi(milestones=[milestone()])
            def hook(a, m, p, b, result):
                if (m, p) == ("GET", "milestones/1"):
                    result.update(change)
            api.after = hook
            with self.subTest(change=change), self.assertRaises(mod.RequestError):
                api.run()
            self.assertEqual(api.writes(), [])

    def test_collision_recovery_requires_exact_metadata_for_every_milestone(self):
        for change in ({}, {"description": "Other intent"}, {"title": "eng-qa - reliable ci"},
                       {"state": "closed"}):
            api = FakeApi()
            def hook(a, m, p, b):
                if (m, p) == ("POST", "milestones"):
                    a.milestones.append(dict(milestone(), **change))
                    raise mod.ApiError("Concurrent creation", 422)
            api.before = hook
            with self.subTest(change=change):
                if change:
                    with self.assertRaisesRegex(mod.RequestError, "incompatible"):
                        api.run()
                    self.assertFalse(any(m == "PATCH" or p.endswith("/comments") or m == "DELETE"
                                         for m, p, _ in api.writes()))
                else:
                    api.run()
                    self.assertEqual(api.issues[11]["milestone"], {"number": 1})
                    self.assertEqual(len(api.comments), 1)

    def test_later_collision_cannot_partially_assign_first_milestone(self):
        api = FakeApi(body=request([
            {"title": "First", "description": "One", "issue_numbers": [11]},
            {"title": "Second", "description": "Two", "issue_numbers": [12]},
        ]))
        def hook(a, m, p, b):
            if (m, p) == ("POST", "milestones") and b["title"] == "Second":
                a.milestones.append(milestone(2, "Second", "Wrong intent"))
                raise mod.ApiError("Collision", 422)
        api.before = hook
        with self.assertRaisesRegex(mod.RequestError, "incompatible"):
            api.run()
        self.assertFalse(any(m == "PATCH" or p.endswith("/comments") or m == "DELETE"
                             for m, p, _ in api.writes()))
        self.assertIsNone(api.issues[11]["milestone"])
        self.assertEqual(api.milestones[0]["title"], "First")

    def test_api_error_has_no_success_comment_or_clear(self):
        for status in (None, 403, 422, 503):
            api = FakeApi()
            def hook(a, m, p, b):
                if (m, p) == ("POST", "milestones"):
                    raise mod.ApiError("Injected API failure", status)
            api.before = hook
            with self.subTest(status=status), self.assertRaises(mod.ApiError):
                api.run()
            self.assertFalse(any(m == "DELETE" or p.endswith("/comments") or m == "PATCH"
                                 for m, p, _ in api.calls))

    def test_authorization_and_receipt_read_errors_never_fall_back_to_writes(self):
        for point in (("GET", "issues/16"), ("GET", "issues/16/comments")):
            api = FakeApi()
            def hook(a, m, p, b):
                if (m, p) == point:
                    raise mod.ApiError("Read unavailable", 503)
            api.before = hook
            with self.subTest(point=point), self.assertRaises(mod.ApiError):
                api.run()
            self.assertEqual(api.writes(), [])
            self.assertEqual(api.effects, [])

    def test_malformed_source_labels_fail_closed(self):
        for labels in (None, ["automation:create-milestone"], [{"name": 7}]):
            api = FakeApi()
            api.issues[16]["labels"] = labels
            with self.subTest(labels=labels), self.assertRaisesRegex(mod.RequestError, "source labels"):
                api.run()
            self.assertEqual(api.writes(), [])

    def test_forbidden_create_is_not_hidden_by_collision_lookup(self):
        api = FakeApi()
        def hook(a, m, p, b):
            if (m, p) == ("POST", "milestones"):
                a.milestones.append(milestone())
                raise mod.ApiError("Forbidden", 403)
        api.before = hook
        with self.assertRaises(mod.ApiError):
            api.run()
        self.assertEqual(api.calls[-1][:2], ("POST", "milestones"))
        self.assertIsNone(api.issues[11]["milestone"])

    def test_request_does_not_control_api_path(self):
        api = FakeApi()
        api.run()
        for _, path, _ in api.calls:
            self.assertNotIn("..", path)
            self.assertNotIn("ENG-QA", path)
            self.assertTrue(path.startswith(("issues/", "milestones")))
        self.assertEqual(api.calls[0][:2], ("GET", "issues/16"))

    def test_transport_failure_classification_is_sanitized_and_bounded(self):
        seen = []
        def forbidden(req, timeout):
            seen.append((req.full_url, timeout))
            raise urllib.error.HTTPError(req.full_url, 403, "forbidden", {}, io.BytesIO(b"PRIVATE_SENTINEL"))
        api = mod.GitHubApi("unit-test-token", transport=forbidden)
        with self.assertRaises(mod.ApiError) as caught:
            api.call("GET", "issues/16")
        self.assertEqual(caught.exception.status, 403)
        self.assertNotIn("PRIVATE_SENTINEL", str(caught.exception))
        self.assertNotIn("unit-test-token", str(caught.exception))
        self.assertEqual(seen, [(f"https://api.github.com/repos/{mod.REPOSITORY}/issues/16", 20)])
        def unavailable(req, timeout):
            raise urllib.error.URLError("PRIVATE_SENTINEL")
        with self.assertRaises(mod.ApiError) as caught:
            mod.GitHubApi("unit-test-token", transport=unavailable).call("POST", "milestones", {})
        self.assertIsNone(caught.exception.status)
        self.assertNotIn("PRIVATE_SENTINEL", str(caught.exception))


if __name__ == "__main__":
    unittest.main()

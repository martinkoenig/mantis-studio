import importlib.util
import json
import pathlib
import sys
import unittest

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
        items = [{"title": "ENG-QA - Reliable CI", "description": "Engineering qualification", "issue_numbers": [11, 12]}]
    return f"Before\n{mod.START}{json.dumps({'milestones': items})}{mod.END}\nAfter"


class FakeApi:
    def __init__(self, milestones=None, issues=None, fail_at=None):
        self.milestones = milestones if milestones is not None else []
        self.issues = issues if issues is not None else {
            11: {"number": 11, "state": "open", "milestone": None},
            12: {"number": 12, "state": "open", "milestone": None},
        }
        self.calls = []
        self.fail_at = fail_at

    def call(self, method, path, body=None):
        self.calls.append((method, path, body))
        if self.fail_at and (method, path.split("?")[0]) == self.fail_at:
            raise mod.RequestError("Injected API failure")
        if method == "GET" and path.startswith("milestones?"):
            return list(self.milestones)
        if method == "GET" and path.startswith("issues/"):
            return dict(self.issues[int(path.split("/")[1])])
        if method == "POST" and path == "milestones":
            row = {"number": len(self.milestones) + 1, "title": body["title"],
                   "state": "open", "html_url": "https://github.com/martinkoenig/mantis-studio/milestone/1"}
            self.milestones.append(row)
            return row
        if method == "PATCH" and path.startswith("issues/"):
            n = int(path.split("/")[1])
            self.issues[n]["milestone"] = {"number": body["milestone"]}
            return dict(self.issues[n])
        if method == "POST" and path.endswith("/comments"):
            return {}
        if method == "DELETE" and "/labels/" in path:
            return None
        raise AssertionError(f"Unexpected {method} {path}")


class MilestoneTests(unittest.TestCase):
    def test_owner_request(self):
        n, body = mod.validate_event(event(body=request()))
        self.assertEqual(n, 16)
        self.assertEqual(mod.parse_request(body)[0].issues, (11, 12))

    def test_foreign_author_rejected(self):
        with self.assertRaises(mod.RequestError):
            mod.validate_event(event(author=99, body=request()))

    def test_foreign_labeler_rejected(self):
        with self.assertRaises(mod.RequestError):
            mod.validate_event(event(sender=99, body=request()))

    def test_wrong_repo_event_label_rejected(self):
        for mutate in (
            lambda e: e["repository"].update(full_name="attacker/fork"),
            lambda e: e["label"].update(name="bug"),
            lambda e: e.update(action="opened"),
        ):
            e = event(body=request())
            mutate(e)
            with self.subTest(e=e), self.assertRaises(mod.RequestError):
                mod.validate_event(e)

    def test_pr_closed_and_malformed_source_rejected(self):
        for mutate in (
            lambda e: e["issue"].update(pull_request={}),
            lambda e: e["issue"].update(state="closed"),
            lambda e: e["issue"].update(number=True),
            lambda e: e["issue"].update(user=None),
        ):
            e = event(body=request())
            mutate(e)
            with self.subTest(e=e), self.assertRaises(mod.RequestError):
                mod.validate_event(e)

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
        ]
        for body in cases:
            with self.subTest(body=body[:60]), self.assertRaises(mod.RequestError):
                mod.parse_request(body)

    def test_create_assign_comment_clear_label(self):
        api = FakeApi()
        mod.provision(api, 16, mod.parse_request(request()))
        self.assertEqual(len(api.milestones), 1)
        self.assertEqual(api.issues[11]["milestone"], {"number": 1})
        self.assertEqual(api.issues[12]["milestone"], {"number": 1})
        self.assertTrue(any(m == "POST" and p == "issues/16/comments" for m,p,_ in api.calls))
        self.assertTrue(any(m == "DELETE" and p.endswith("automation%3Acreate-milestone") for m,p,_ in api.calls))

    def test_idempotent_replay(self):
        api = FakeApi()
        req = mod.parse_request(request())
        mod.provision(api, 16, req)
        api.calls.clear()
        mod.provision(api, 16, req)
        self.assertFalse(any(m == "PATCH" or m == "POST" and p == "milestones" for m,p,_ in api.calls))

    def test_preflight_conflicting_assignment_no_writes(self):
        api = FakeApi()
        api.issues[11]["milestone"] = {"number": 99}
        with self.assertRaises(mod.RequestError):
            mod.provision(api, 16, mod.parse_request(request()))
        self.assertFalse(any(m in ("PATCH", "POST", "DELETE") for m,_,_ in api.calls))

    def test_preflight_rejects_pr_target(self):
        api = FakeApi()
        api.issues[11]["pull_request"] = {}
        with self.assertRaises(mod.RequestError):
            mod.provision(api, 16, mod.parse_request(request()))
        self.assertFalse(any(m in ("PATCH", "POST", "DELETE") for m,_,_ in api.calls))

    def test_closed_matching_milestone_rejected(self):
        api = FakeApi(milestones=[{"number": 1, "title": "ENG-QA - Reliable CI", "state": "closed"}])
        with self.assertRaises(mod.RequestError):
            mod.provision(api, 16, mod.parse_request(request()))

    def test_api_error_has_no_success_comment_or_clear(self):
        api = FakeApi(fail_at=("POST", "milestones"))
        with self.assertRaises(mod.RequestError):
            mod.provision(api, 16, mod.parse_request(request()))
        self.assertFalse(any(m == "DELETE" or p.endswith("/comments") for m,p,_ in api.calls))

    def test_request_does_not_control_api_path(self):
        api = FakeApi()
        mod.provision(api, 16, mod.parse_request(request()))
        self.assertEqual(api.calls[0][:2], ("GET", "milestones?state=all&per_page=100&page=1"))


if __name__ == "__main__":
    unittest.main()

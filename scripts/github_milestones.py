#!/usr/bin/env python3
"""Owner-only issue-label -> GitHub milestone provisioning; standard library only.

This code runs only from the default-branch workflow with least-privilege issues:write.
Issue text is untrusted data, never shell/source code.

Every write is preceded by a live check of the owner's open ordinary source
issue, exact event body and trigger label. Milestone reuse requires the exact
title/description and an open state; case folding detects collisions only.
An API null description is equivalent to an empty requested description.

Recovery reads server-attributed Actions-bot receipts, bounded to 1,000 comments.
Receipts never authorize writes: live source intent and completed target state
must still match. A new request body is needed to intentionally repeat completed
work. The serialized workflow prevents its own concurrent issue-event runs.

GitHub offers no transaction across source reads, milestone creation, target
reads/updates, comment creation and label deletion. Changes AFTER the last read
can race a write; a concurrent external comment writer or delayed API visibility
can still race receipt deduplication. Failures may leave partial effects, never
rolled back automatically. Reruns reconcile those effects under current intent;
this is not atomic mutation or an exactly-once delivery guarantee.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

REPOSITORY = "martinkoenig/mantis-studio"
OWNER_ID = 13789807
TRIGGER_LABEL = "automation:create-milestone"
START = "<!-- MANTIS_MILESTONE_REQUEST_V1\n"
END = "\nEND_MANTIS_MILESTONE_REQUEST_V1 -->"
TITLE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._:/()+\u2013\u2014-]{0,79}$")
MAX_MILESTONES = 5
MAX_ISSUES = 15
MAX_COMMENT_PAGES = 10
# Server-reported identity: https://api.github.com/users/github-actions%5Bbot%5D
ACTIONS_BOT_ID = 41898282


class RequestError(ValueError):
    """Expected, redacted input or GitHub API failure."""


class ApiError(RequestError):
    def __init__(self, message: str, status: int | None = None):
        super().__init__(message)
        self.status = status


class IntentRevoked(RequestError):
    """Stop without further writes; earlier partial effects may remain."""


@dataclass(frozen=True)
class DesiredMilestone:
    title: str
    description: str
    issues: tuple[int, ...]


def validate_event(event: dict[str, Any]) -> tuple[int, str]:
    """Fail closed unless the open ordinary issue AND label actor are the owner."""
    if not isinstance(event, dict):
        raise RequestError("Invalid label event")
    repo = event.get("repository") or {}
    issue = event.get("issue") or {}
    sender = event.get("sender") or {}
    label = event.get("label") or {}
    if any(not isinstance(value, dict) for value in (repo, issue, sender, label)):
        raise RequestError("Invalid label event fields")
    author = issue.get("user") or {}
    if (event.get("action") != "labeled"
            or repo.get("full_name") != REPOSITORY
            or label.get("name") != TRIGGER_LABEL):
        raise RequestError("Not an authorized label event")
    if (not isinstance(author, dict) or type(sender.get("id")) is not int
            or type(author.get("id")) is not int
            or sender["id"] != OWNER_ID or author["id"] != OWNER_ID):
        raise RequestError("Only owner-authored and owner-labeled issues are authorized")
    if "pull_request" in issue or issue.get("state") != "open":
        raise RequestError("Source must be an open issue (not a PR)")
    number = issue.get("number")
    if type(number) is not int or number <= 0:
        raise RequestError("Invalid source issue")
    body = issue.get("body")
    if not isinstance(body, str):
        raise RequestError("Issue request body is missing")
    return number, body


def parse_request(body: str) -> list[DesiredMilestone]:
    if len(body) > 60_000 or body.count(START) != 1 or body.count(END) != 1:
        raise RequestError("Expected one bounded milestone request block")
    start = body.index(START) + len(START)
    end = body.index(END)
    if end < start:
        raise RequestError("Request delimiters are reversed")
    data = body[start:end]
    if len(data) > 8_000:
        raise RequestError("Request block is too long")
    try:
        payload = json.loads(data, object_pairs_hook=unique_object)
    except json.JSONDecodeError as exc:
        raise RequestError("Request block is not valid JSON") from exc
    if not isinstance(payload, dict) or set(payload) != {"milestones"}:
        raise RequestError("Expected only a milestones array")
    items = payload["milestones"]
    if not isinstance(items, list) or not 1 <= len(items) <= MAX_MILESTONES:
        raise RequestError("Expected 1-5 milestones")
    out: list[DesiredMilestone] = []
    seen_titles: set[str] = set()
    seen_issues: set[int] = set()
    for item in items:
        if not isinstance(item, dict) or set(item) != {"title", "description", "issue_numbers"}:
            raise RequestError("Invalid milestone fields")
        title, description, numbers = item["title"], item["description"], item["issue_numbers"]
        if not isinstance(title, str) or not TITLE_RE.fullmatch(title) or title != title.strip():
            raise RequestError("Invalid milestone title")
        title_key = title.casefold()
        if title_key in seen_titles:
            raise RequestError("Duplicate milestone title")
        seen_titles.add(title_key)
        if (not isinstance(description, str) or len(description) > 1500
                or any(ord(c) < 32 and c not in "\n\t" for c in description)):
            raise RequestError("Invalid milestone description")
        if not isinstance(numbers, list) or not 1 <= len(numbers) <= MAX_ISSUES:
            raise RequestError("Expected 1-15 target issues")
        if any(type(n) is not int or not 1 <= n <= 2_000_000 for n in numbers):
            raise RequestError("Invalid target issue ID")
        if len(numbers) != len(set(numbers)) or any(n in seen_issues for n in numbers):
            raise RequestError("Repeated issue assignment")
        seen_issues.update(numbers)
        out.append(DesiredMilestone(title, description, tuple(numbers)))
    return out


def unique_object(pairs: list[tuple[str, Any]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise RequestError("Duplicate JSON object key")
        result[key] = value
    return result


class GitHubApi:
    def __init__(self, token: str, transport: Callable | None = None):
        if not token:
            raise RequestError("Missing GitHub token")
        self.token = token
        self.transport = transport or urllib.request.urlopen

    def call(self, method: str, path: str, body: dict | None = None) -> Any:
        uri = f"https://api.github.com/repos/{REPOSITORY}/{path}"
        request = urllib.request.Request(
            uri,
            data=None if body is None else json.dumps(body).encode("utf-8"),
            method=method,
            headers={
                "Authorization": f"Bearer {self.token}",
                "Accept": "application/vnd.github+json",
                "X-GitHub-Api-Version": "2022-11-28",
                "User-Agent": "mantis-milestone-provisioner",
                "Content-Type": "application/json",
            },
        )
        try:
            with self.transport(request, timeout=20) as response:
                raw = response.read(1_000_000)
                if len(raw) >= 1_000_000:
                    raise RequestError("GitHub response exceeded size limit")
                try:
                    return json.loads(raw) if raw else None
                except (ValueError, UnicodeError):
                    raise RequestError("Invalid GitHub JSON response") from None
        except urllib.error.HTTPError as exc:
            # Do not print untrusted response bodies or authentication credentials.
            raise ApiError(
                f"GitHub API {method} {path.split('?', 1)[0]} failed: HTTP {exc.code}", exc.code
            ) from None
        except (urllib.error.URLError, TimeoutError, OSError):
            # A lost write response is ambiguous; reconcile on a subsequent run.
            raise ApiError("GitHub API transport failed; outcome may be unknown") from None


def list_milestones(api: GitHubApi) -> dict[str, dict]:
    found: dict[str, dict] = {}
    for page in range(1, 21):
        rows = api.call("GET", f"milestones?state=all&per_page=100&page={page}")
        if not isinstance(rows, list):
            raise RequestError("Invalid GitHub milestone listing")
        for row in rows:
            if not isinstance(row, dict) or not isinstance(row.get("title"), str):
                raise RequestError("Invalid milestone entry")
            key = row["title"].casefold()
            if key in found:
                raise RequestError("Ambiguous duplicate milestones")
            found[key] = row
        if len(rows) < 100:
            return found
    raise RequestError("Milestone pagination exceeded bounded limit")


def compatible_milestone(row: Any, item: DesiredMilestone) -> dict:
    if (not isinstance(row, dict) or type(row.get("number")) is not int
            or row["number"] <= 0 or row.get("state") != "open"
            or row.get("title") != item.title
            or "description" not in row
            or (row.get("description") if row.get("description") is not None else "") != item.description):
        raise RequestError("Milestone identity, state or description is incompatible")
    return row


def open_issue(row: Any, number: int) -> dict:
    if (not isinstance(row, dict) or type(row.get("number")) is not int
            or row["number"] != number or "pull_request" in row or row.get("state") != "open"):
        raise RequestError(f"Target #{number} is not an open ordinary issue")
    return row


def assigned_number(issue: dict) -> int | None:
    present = issue.get("milestone")
    if present is None:
        return None
    if (not isinstance(present, dict) or type(present.get("number")) is not int
            or present["number"] <= 0):
        raise RequestError("Invalid issue milestone assignment")
    return present["number"]


def source_labels(issue: dict) -> set[str]:
    labels = issue.get("labels")
    if not isinstance(labels, list) or any(
            not isinstance(label, dict) or not isinstance(label.get("name"), str) for label in labels):
        raise RequestError("Invalid source labels response")
    return {label["name"] for label in labels}


def require_live_source(api: GitHubApi, source: int, body: str) -> None:
    issue = api.call("GET", f"issues/{source}")
    if (not isinstance(issue, dict) or type(issue.get("number")) is not int
            or issue["number"] != source or "pull_request" in issue
            or issue.get("state") != "open" or not isinstance(issue.get("user"), dict)
            or type(issue["user"].get("id")) is not int
            or issue["user"].get("id") != OWNER_ID or issue.get("body") != body):
        raise IntentRevoked("Source intent changed or was revoked")
    if TRIGGER_LABEL not in source_labels(issue):
        raise IntentRevoked("Source trigger label was removed")


def preflight(api: GitHubApi, desired: list[DesiredMilestone], milestones: dict[str, dict]) -> None:
    """Preflight all requests BEFORE writing anything."""
    for item in desired:
        existing = milestones.get(item.title.casefold())
        if existing is not None:
            compatible_milestone(existing, item)
        for n in item.issues:
            issue = open_issue(api.call("GET", f"issues/{n}"), n)
            present = assigned_number(issue)
            if present is not None and (existing is None or present != existing["number"]):
                raise RequestError(f"Target #{n} has a different milestone")


def receipt_marker(source: int, body: str) -> str:
    identity = json.dumps([REPOSITORY, source, body], ensure_ascii=True, separators=(",", ":"))
    digest = hashlib.sha256(identity.encode("utf-8")).hexdigest()
    return f"<!-- MANTIS_MILESTONE_COMPLETED_V1 sha256:{digest} -->"


def find_receipt(api: GitHubApi, source: int, marker: str) -> str | None:
    """Only a server-attributed bot receipt can dedupe; text grants no authority."""
    found = None
    for page in range(1, MAX_COMMENT_PAGES + 1):
        rows = api.call("GET", f"issues/{source}/comments?per_page=100&page={page}")
        if not isinstance(rows, list) or len(rows) > 100:
            raise RequestError("Invalid GitHub comments response")
        for row in rows:
            if not isinstance(row, dict):
                raise RequestError("Invalid GitHub comment entry")
            author = row.get("user")
            text = row.get("body")
            if (isinstance(author, dict) and type(author.get("id")) is int
                    and author["id"] == ACTIONS_BOT_ID
                    and author.get("type") == "Bot" and isinstance(text, str)
                    and text.startswith(marker + "\n")):
                if found is not None:
                    raise RequestError("Ambiguous duplicate completion receipts")
                found = text
        if len(rows) < 100:
            return found
    raise RequestError("Completion receipt lookup exceeded bounded limit")


def live_milestone(api: GitHubApi, item: DesiredMilestone, number: int) -> dict:
    row = compatible_milestone(api.call("GET", f"milestones/{number}"), item)
    if row["number"] != number:
        raise RequestError("Milestone identity changed")
    return row


def verify_completed(api: GitHubApi, desired: list[DesiredMilestone], milestones: dict[str, dict]) -> list[str]:
    results = []
    for item in desired:
        existing = milestones.get(item.title.casefold())
        if existing is None:
            raise RequestError("Completed request milestone is missing")
        row = live_milestone(api, item, existing["number"])
        for n in item.issues:
            issue = open_issue(api.call("GET", f"issues/{n}"), n)
            if assigned_number(issue) != row["number"]:
                raise RequestError("Completed request target state does not match")
        results.append(f"- {item.title}: https://github.com/{REPOSITORY}/milestone/{row['number']}")
    return results


def clear_trigger(api: GitHubApi, source: int, body: str) -> None:
    require_live_source(api, source, body)
    try:
        api.call("DELETE", f"issues/{source}/labels/{urllib.parse.quote(TRIGGER_LABEL, safe='')}")
    except ApiError as exc:
        if exc.status != 404:
            raise
        # Accept only a confirmed already-absent label, not every DELETE failure.
        latest = api.call("GET", f"issues/{source}")
        if (not isinstance(latest, dict) or type(latest.get("number")) is not int
                or latest["number"] != source or TRIGGER_LABEL in source_labels(latest)):
            raise


def _provision(api: GitHubApi, source: int, body: str, desired: list[DesiredMilestone]) -> list[str]:
    require_live_source(api, source, body)
    milestones = list_milestones(api)
    preflight(api, desired, milestones)
    marker = receipt_marker(source, body)
    receipt = find_receipt(api, source, marker)
    if receipt is not None:
        results = verify_completed(api, desired, milestones)
        expected = marker + "\nOwner-authorized milestone request completed:\n" + "\n".join(results)
        if receipt != expected:
            raise RequestError("Completion receipt does not match verified state")
        clear_trigger(api, source, body)
        return results

    # Resolve every milestone before assigning any issue, including collision recovery.
    for item in desired:
        milestone = milestones.get(item.title.casefold())
        if milestone is None:
            require_live_source(api, source, body)
            try:
                milestone = api.call("POST", "milestones", {
                    "title": item.title, "description": item.description, "state": "open"
                })
            except ApiError as exc:
                if exc.status not in (None, 422):
                    raise
                # Collision or lost response: never change a differently scoped milestone.
                milestone = list_milestones(api).get(item.title.casefold())
                if milestone is None:
                    raise
            milestones[item.title.casefold()] = compatible_milestone(milestone, item)

    for item in desired:
        milestone = milestones[item.title.casefold()]
        for n in item.issues:
            live_milestone(api, item, milestone["number"])
            latest = open_issue(api.call("GET", f"issues/{n}"), n)
            present = assigned_number(latest)
            if present is not None and present != milestone["number"]:
                raise RequestError(f"Target #{n} changed milestone concurrently")
            if present is None:
                require_live_source(api, source, body)
                api.call("PATCH", f"issues/{n}", {"milestone": milestone["number"]})

    results = verify_completed(api, desired, milestones)
    expected = marker + "\nOwner-authorized milestone request completed:\n" + "\n".join(results)
    receipt = find_receipt(api, source, marker)
    if receipt is not None and receipt != expected:
        raise RequestError("Completion receipt does not match verified state")
    if receipt is None:
        require_live_source(api, source, body)
        api.call("POST", f"issues/{source}/comments", {"body": expected})
    clear_trigger(api, source, body)
    return results


def provision(api: GitHubApi, event: dict[str, Any]) -> list[str]:
    source, body = validate_event(event)
    desired = parse_request(body)
    try:
        return _provision(api, source, body, desired)
    except IntentRevoked:
        return []


def main() -> None:
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    # Reject invalid snapshots before constructing even the authenticated API client.
    validate_event(event)
    api = GitHubApi(os.environ["GH_TOKEN"])
    results = provision(api, event)
    if not results:
        print("Source intent revoked or changed; stopped without further writes; partial effects may remain")
    for line in results:
        print(line)


if __name__ == "__main__":
    try:
        main()
    except (RequestError, KeyError, TypeError, OSError, ValueError) as exc:
        print(f"Milestone request rejected or failed: {exc}", file=sys.stderr)
        sys.exit(1)

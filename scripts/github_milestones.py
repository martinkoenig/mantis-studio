#!/usr/bin/env python3
"""Owner-only issue-label -> GitHub milestone provisioning; standard library only.

This code runs only from the default-branch workflow with least-privilege issues:write.
Issue text is untrusted data, never shell/source code.
"""
from __future__ import annotations

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


class RequestError(ValueError):
    """Expected, redacted input or GitHub API failure."""


@dataclass(frozen=True)
class DesiredMilestone:
    title: str
    description: str
    issues: tuple[int, ...]


def validate_event(event: dict[str, Any]) -> tuple[int, str]:
    """Fail closed unless the open ordinary issue AND label actor are the owner."""
    repo = event.get("repository") or {}
    issue = event.get("issue") or {}
    sender = event.get("sender") or {}
    label = event.get("label") or {}
    author = issue.get("user") or {}
    if (event.get("action") != "labeled"
            or repo.get("full_name") != REPOSITORY
            or label.get("name") != TRIGGER_LABEL):
        raise RequestError("Not an authorized label event")
    if sender.get("id") != OWNER_ID or author.get("id") != OWNER_ID:
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
    data = body.split(START, 1)[1].split(END, 1)[0]
    if len(data) > 8_000:
        raise RequestError("Request block is too long")
    try:
        payload = json.loads(data)
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
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as exc:
            # Do not print untrusted response bodies or authentication credentials.
            raise RequestError(
                f"GitHub API {method} {path.split('?', 1)[0]} failed: HTTP {exc.code}"
            ) from None


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


def preflight(api: GitHubApi, desired: list[DesiredMilestone], milestones: dict[str, dict]) -> dict[int, dict]:
    """Preflight all requests BEFORE writing anything."""
    states = {}
    for item in desired:
        existing = milestones.get(item.title.casefold())
        if existing and existing.get("state") != "open":
            raise RequestError("Matching milestone already closed")
        for n in item.issues:
            issue = api.call("GET", f"issues/{n}")
            if not isinstance(issue, dict) or "pull_request" in issue or issue.get("state") != "open":
                raise RequestError(f"Target #{n} is not an open issue")
            present = issue.get("milestone")
            if present and (not existing or present.get("number") != existing.get("number")):
                raise RequestError(f"Target #{n} has a different milestone")
            states[n] = issue
    return states


def provision(api: GitHubApi, source: int, desired: list[DesiredMilestone]) -> list[str]:
    milestones = list_milestones(api)
    states = preflight(api, desired, milestones)
    results = []
    for item in desired:
        milestone = milestones.get(item.title.casefold())
        if milestone is None:
            try:
                milestone = api.call("POST", "milestones", {
                    "title": item.title, "description": item.description, "state": "open"
                })
            except RequestError:
                # Only accept a previously created EXACT matching open title on collision.
                milestone = list_milestones(api).get(item.title.casefold())
                if not milestone or milestone.get("state") != "open":
                    raise
            if not isinstance(milestone, dict) or type(milestone.get("number")) is not int:
                raise RequestError("Unexpected created milestone response")
            milestones[item.title.casefold()] = milestone
        for n in item.issues:
            if (states[n].get("milestone") or {}).get("number") == milestone["number"]:
                continue
            latest = api.call("GET", f"issues/{n}")
            if not isinstance(latest, dict):
                raise RequestError("Unexpected target issue response")
            present = latest.get("milestone")
            if present and present.get("number") != milestone["number"]:
                raise RequestError(f"Target #{n} changed milestone concurrently")
            if not present:
                api.call("PATCH", f"issues/{n}", {"milestone": milestone["number"]})
        results.append(f"- {item.title}: {milestone.get('html_url', '(link unavailable)')}")
    api.call("POST", f"issues/{source}/comments", {
        "body": "Owner-authorized milestone request completed:\n" + "\n".join(results)
    })
    api.call("DELETE", f"issues/{source}/labels/{urllib.parse.quote(TRIGGER_LABEL, safe='')}")
    return results


def main() -> None:
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    source, body = validate_event(event)
    desired = parse_request(body)
    api = GitHubApi(os.environ["GH_TOKEN"])
    for line in provision(api, source, desired):
        print(line)


if __name__ == "__main__":
    try:
        main()
    except (RequestError, KeyError, TypeError, OSError, ValueError) as exc:
        print(f"Milestone request rejected or failed: {exc}", file=sys.stderr)
        sys.exit(1)

"""Parse analyzer output and classify findings against the committed baselines and the policy file."""

from __future__ import annotations

import fnmatch
import json
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from .common import BASELINE_DIR, POLICY_FILE, ROOT, git_state

TIDY_RX = re.compile(r"^(/[^:\n]+):(\d+):(\d+): (warning|error): (.*) \[([\w\-,.]+?)(?:,-warnings-as-errors)?\]$")
CPPCHECK_RX = re.compile(r"^([^\s:][^:]*):(\d+):(\d+): (\w+): (.*) \[(\w+)\]$")
# Output that only reflects the cppcheck configuration, not the code.
CPPCHECK_NOISE = {"missingIncludeSystem", "checkersReport", "toomanyconfigs"}


@dataclass(frozen=True)
class Finding:
    file: str  # repository-relative
    line: int
    column: int
    severity: str
    check: str
    message: str

    @property
    def key(self) -> str:
        # Line-agnostic so unrelated edits above a finding do not read as "resolved + new".
        return f"{self.file}|{self.check}|{self.severity}|{self.message}"

    def where(self) -> str:
        return f"{self.file}:{self.line}:{self.column}"


def relativize(path: str) -> str | None:
    candidate = Path(path)
    try:
        relative = candidate.resolve().relative_to(ROOT)
    except ValueError:
        return None
    parts = relative.parts
    if parts[0] in ("build", ".project-manager", ".claude", ".hermes", "graft"):
        return None
    return relative.as_posix()


def parse_tidy(text: str) -> list[Finding]:
    seen: set[Finding] = set()
    for raw in text.splitlines():
        match = TIDY_RX.match(raw.rstrip())
        if not match:
            continue
        path, line, col, severity, message, check = match.groups()
        relative = relativize(path)
        if relative is None:
            continue
        seen.add(Finding(relative, int(line), int(col), severity, check, message))
    return sorted(seen, key=lambda f: (f.file, f.line, f.column, f.check))


def parse_cppcheck(text: str) -> list[Finding]:
    seen: set[Finding] = set()
    for raw in text.splitlines():
        match = CPPCHECK_RX.match(raw.rstrip())
        if not match:
            continue
        path, line, col, severity, message, check = match.groups()
        if check in CPPCHECK_NOISE:
            continue
        relative = relativize(path if path.startswith("/") else str(ROOT / path))
        if relative is None:
            continue
        seen.add(Finding(relative, int(line), int(col), severity, check, message))
    return sorted(seen, key=lambda f: (f.file, f.line, f.column, f.check))


def load_policy() -> dict:
    return json.loads(POLICY_FILE.read_text()) if POLICY_FILE.exists() else {"deferred": [], "approved": []}


def load_baseline(name: str) -> dict | None:
    path = BASELINE_DIR / f"{name}.json"
    return json.loads(path.read_text()) if path.exists() else None


def write_baseline(name: str, tool_version: str, findings: list[Finding]) -> Path:
    counts = Counter(f.key for f in findings)
    payload = {
        "schema": 1,
        "tool": name,
        "tool_version": tool_version,
        "recorded_at": git_state(),
        "total": len(findings),
        "counts": dict(sorted(counts.items())),
    }
    path = BASELINE_DIR / f"{name}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=1) + "\n")
    return path


def matches_entry(finding: Finding, entry: dict) -> bool:
    if entry.get("check") and entry["check"] not in finding.check.split(","):
        return False
    paths = entry.get("paths")
    return not paths or any(fnmatch.fnmatch(finding.file, pattern) for pattern in paths)


def classify(findings: list[Finding], baseline: dict | None, policy: dict) -> dict:
    """Split findings into existing / new / resolved, and mark deferred and approved ones.

    Deferred and approved findings stay in the counts (nothing is hidden) but never count as new.
    """
    current = Counter(f.key for f in findings)
    base = Counter(baseline["counts"]) if baseline else Counter()
    by_key: dict[str, list[Finding]] = {}
    for finding in findings:
        by_key.setdefault(finding.key, []).append(finding)

    new, existing, resolved = [], [], []
    for key, items in by_key.items():
        allowed = base.get(key, 0) if baseline else len(items)
        # Findings beyond the baselined count are new; report the ones latest in the file.
        existing.extend(items[:allowed])
        new.extend(items[allowed:])
    for key, count in base.items():
        if current.get(key, 0) < count:
            resolved.append({"key": key, "count": count - current.get(key, 0)})

    def tagged(entries: list[dict], bucket: list[Finding]) -> list[Finding]:
        return [f for f in bucket if any(matches_entry(f, e) for e in entries)]

    deferred = tagged(policy.get("deferred", []), findings)
    approved = tagged(policy.get("approved", []), findings)
    exempt = set(deferred) | set(approved)
    return {
        "existing": existing,
        "new": [f for f in new if f not in exempt],
        "new_exempt": [f for f in new if f in exempt],
        "resolved": resolved,
        "deferred": deferred,
        "approved": approved,
        "has_baseline": baseline is not None,
    }


def summarize(result: dict, total: int) -> str:
    return (f"{total} findings: {len(result['existing'])} existing, {len(result['new'])} new, "
            f"{sum(r['count'] for r in result['resolved'])} resolved, {len(result['deferred'])} deferred, "
            f"{len(result['approved'])} approved")


def as_json(result: dict) -> dict:
    def finding_list(items: list[Finding]) -> list[dict]:
        return [{"at": f.where(), "check": f.check, "severity": f.severity, "message": f.message} for f in items]
    return {
        "existing_count": len(result["existing"]),
        "new": finding_list(result["new"]),
        "new_exempt": finding_list(result["new_exempt"]),
        "resolved": result["resolved"],
        "deferred_count": len(result["deferred"]),
        "approved_count": len(result["approved"]),
        "existing_errors": finding_list([f for f in result["existing"] if f.severity == "error"]),
        "baseline_present": result["has_baseline"],
    }


NOLINT_RX = re.compile(r"NOLINT(?:NEXTLINE|BEGIN)?(?:\(([^)]*)\))?(.*)")


def nolint_inventory() -> dict:
    """Approved-exception inventory: every NOLINT in src/tests and whether it carries a justification."""
    total, unjustified = 0, []
    for base in ("src", "tests"):
        for path in sorted((ROOT / base).rglob("*")):
            if path.suffix not in {".cpp", ".hpp"}:
                continue
            for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                match = NOLINT_RX.search(line)
                if not match or "NOLINTEND" in line:
                    continue
                total += 1
                if not match.group(2).strip().lstrip(":").strip():
                    unjustified.append(f"{path.relative_to(ROOT).as_posix()}:{number}")
    return {"total": total, "without_explanation": unjustified}

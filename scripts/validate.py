#!/usr/bin/env python3
"""validate.py - the three static checks this repo gates on.

Kept deliberately small (see CLAUDE.md, "Scope discipline": the prior repo's
validate.py grew past 10,000 lines and that is the specific failure this
script exists to avoid repeating). Three checks, nothing more:

    host-ids      No tracked file names this development machine or its
                  owner (home path, account name, hostname, CPU model,
                  kernel build, MAC/IP address, machine-id). Reuses the one
                  replacement table from scrub-host-ids.py so the two scripts
                  never drift apart on what counts as a leak.
    distribution  No tracked prose reports a mean/average as a latency,
                  outside a fenced ```model block (Little's/Amdahl's Law
                  modelling inputs are allowed there; see CLAUDE.md,
                  "Measurement discipline").
    harness-cap   The shared cross-chapter harness (examples/_lib/) stays
                  under a line cap, and so does this file.

Usage: python3 scripts/validate.py [--check host-ids|distribution|harness-cap]
Exit status: 0 if every requested check passes, 1 otherwise.
"""

from __future__ import annotations

import argparse
import importlib.util
import ipaddress
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HARNESS_DIR = REPO_ROOT / "examples" / "_lib"
HARNESS_LINE_CAP = 2000  # cumulative lines across examples/_lib/
VALIDATE_SELF_CAP = 400  # this file's own line count


def _tracked_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    return [REPO_ROOT / line for line in result.stdout.splitlines() if line]


def _read_text(path: Path) -> str | None:
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if b"\x00" in data:
        return None  # binary file, nothing to scan
    return data.decode("utf-8", errors="ignore")


def _load_scrub_module():
    spec = importlib.util.spec_from_file_location(
        "scrub_host_ids", REPO_ROOT / "scripts" / "scrub-host-ids.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


# RFC 5737 documentation ranges: reserved for examples, identical in every
# doc that uses them, so they name no machine.
_DOCUMENTATION_NETS = (
    ipaddress.ip_network("192.0.2.0/24"),
    ipaddress.ip_network("198.51.100.0/24"),
    ipaddress.ip_network("203.0.113.0/24"),
)


def _is_identifying_ipv4(candidate: str) -> bool:
    """Gate-only exemption: loopback/unspecified/broadcast/doc addresses are
    the same on every machine and name no host. scrub-host-ids.py's
    --filter path stays broad on purpose (it scrubs captured logs); this
    narrower check applies only to validate.py's authored-file gate."""
    try:
        addr = ipaddress.ip_address(candidate)
    except ValueError:
        return True  # not a classifiable address: fail safe, keep flagging
    if addr.is_loopback or addr.is_unspecified:
        return False
    if candidate == "255.255.255.255":
        return False
    if any(addr in net for net in _DOCUMENTATION_NETS):
        return False
    return True


def check_host_ids() -> tuple[bool, list[str]]:
    scrub = _load_scrub_module()
    table = scrub.build_table()
    problems: list[str] = []
    self_path = Path(__file__).resolve()

    for path in _tracked_files():
        if path.resolve() == self_path:
            continue  # this file's own docstring names the check, not a leak
        text = _read_text(path)
        if text is None:
            continue
        rel = path.relative_to(REPO_ROOT)
        for description, is_regex, pattern, _replacement in table:
            if description == "IPv4 address":
                hits = [
                    m.group(0)
                    for m in re.finditer(pattern, text)
                    if _is_identifying_ipv4(m.group(0))
                ]
                if hits:
                    problems.append(f"{rel}: contains {description} ({hits[0]})")
                continue
            found = re.search(pattern, text) if is_regex else (pattern in text)
            if found:
                problems.append(f"{rel}: contains {description}")

    return (not problems, problems)


_MODEL_FENCE = re.compile(r"^\s*```model\s*$")
_FENCE = re.compile(r"^\s*```")
_MEAN_LATENCY = re.compile(
    r"\b(mean|average|avg)\b[^.\n]{0,40}\blatency\b|\blatency\b[^.\n]{0,40}\b(mean|average|avg)\b",
    re.IGNORECASE,
)
# A line only counts as "reporting" a latency if it also carries a numeric
# value with a time unit; this keeps the check from flagging prose that
# merely talks about the mean/average rule (like this file's own "Do not"
# list) instead of citing a measured figure.
_LATENCY_VALUE = re.compile(r"\d+(\.\d+)?\s?(ns|us|µs|ms)\b", re.IGNORECASE)


def check_distribution() -> tuple[bool, list[str]]:
    problems: list[str] = []
    for path in _tracked_files():
        if path.suffix != ".md":
            continue
        text = _read_text(path)
        if text is None:
            continue
        rel = path.relative_to(REPO_ROOT)
        in_model_block = False
        for lineno, line in enumerate(text.splitlines(), start=1):
            if _MODEL_FENCE.match(line):
                in_model_block = True
                continue
            if in_model_block and _FENCE.match(line):
                in_model_block = False
                continue
            if in_model_block:
                continue
            if _MEAN_LATENCY.search(line) and _LATENCY_VALUE.search(line):
                problems.append(f"{rel}:{lineno}: reports a mean/average as a latency")
    return (not problems, problems)


def _line_count(path: Path) -> int:
    text = _read_text(path)
    return 0 if text is None else len(text.splitlines())


def check_harness_cap() -> tuple[bool, list[str]]:
    problems: list[str] = []

    if HARNESS_DIR.is_dir():
        total = sum(_line_count(p) for p in HARNESS_DIR.rglob("*") if p.is_file())
        if total > HARNESS_LINE_CAP:
            problems.append(
                f"examples/_lib/: {total} lines exceeds the {HARNESS_LINE_CAP}-line harness cap"
            )

    self_lines = _line_count(Path(__file__))
    if self_lines > VALIDATE_SELF_CAP:
        problems.append(
            f"scripts/validate.py: {self_lines} lines exceeds its own "
            f"{VALIDATE_SELF_CAP}-line cap"
        )

    return (not problems, problems)


CHECKS = {
    "host-ids": check_host_ids,
    "distribution": check_distribution,
    "harness-cap": check_harness_cap,
}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check",
        choices=sorted(CHECKS),
        action="append",
        help="run only this check (repeatable); default: run all checks",
    )
    args = parser.parse_args(argv)
    names = args.check or sorted(CHECKS)

    all_ok = True
    for name in names:
        ok, problems = CHECKS[name]()
        all_ok = all_ok and ok
        status = "PASS" if ok else "FAIL"
        print(f"[{status}] {name}")
        for problem in problems:
            print(f"    {problem}")

    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

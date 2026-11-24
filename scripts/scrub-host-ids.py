#!/usr/bin/env python3
"""scrub-host-ids.py - the one table of host-identifier replacements.

This repo is public (see CLAUDE.md, "Public-repo host-id scrub rule"): no
tracked file may name the development machine or its owner. This script is
the single place that table of replacements lives. It never hardcodes the
account name, hostname, or machine-id; it reads them from the running
environment and only ever writes the generic placeholder.

Two modes:

    --filter        Read stdin, apply every replacement, write stdout.
                     Pipe a captured log through this before committing it.
                     Reads untrusted input, so run it isolated:
                         python3 -I scripts/scrub-host-ids.py --filter

    --print-rules   Print `git filter-repo --replace-text` rules (one
                     `old==>new` pair per line, `regex:` prefix for the
                     pattern-based rules) for a history rewrite. This
                     output contains the real account name and hostname -
                     never commit it; pipe it straight into
                     `git filter-repo --replace-text -`.

Nothing here touches the filesystem beyond read-only lookups (hostname,
username, /etc/machine-id, /proc/cpuinfo, kernel release) needed to build
the table.
"""

from __future__ import annotations

import argparse
import getpass
import re
import socket
import sys
from pathlib import Path

Rule = tuple[str, bool, str, str]  # (description, is_regex, pattern, replacement)


def _read_text_file(path: str) -> str | None:
    try:
        return Path(path).read_text(encoding="utf-8", errors="ignore").strip()
    except OSError:
        return None


def _cpu_model() -> str | None:
    cpuinfo = _read_text_file("/proc/cpuinfo")
    if not cpuinfo:
        return None
    for line in cpuinfo.splitlines():
        if line.lower().startswith("model name"):
            _, _, value = line.partition(":")
            value = value.strip()
            return value or None
    return None


def build_table() -> list[Rule]:
    """Build the one replacement table, read fresh from the environment."""
    home = str(Path.home())
    account = getpass.getuser()
    hostname_short = socket.gethostname()
    hostname_fqdn = socket.getfqdn()
    machine_id = _read_text_file("/etc/machine-id")
    cpu_model = _cpu_model()
    kernel_release = None
    try:
        kernel_release = __import__("platform").uname().release
    except Exception:  # pragma: no cover - platform.uname() practically never fails
        kernel_release = None

    table: list[Rule] = []

    if home and home != "/":
        table.append(("home directory", False, home, "~"))
    # Hostname rules run before the account-name rule: a hostname often
    # embeds the account name as a prefix (e.g. "account-laptop.example"),
    # and matching the longer, more specific string first keeps the output
    # a clean single placeholder instead of a partially-redacted hostname.
    if hostname_fqdn and hostname_fqdn != "localhost":
        table.append(("hostname (fqdn)", False, hostname_fqdn, "<host>"))
    if hostname_short and hostname_short not in ("localhost", hostname_fqdn):
        table.append(("hostname (short)", True, rf"\b{re.escape(hostname_short)}\b", "<host>"))
    if account:
        # Whole-word match only, so the replacement never eats unrelated
        # substrings that happen to contain the account name.
        table.append(("account name", True, rf"\b{re.escape(account)}\b", "<account>"))
    if machine_id:
        table.append(("machine-id", False, machine_id, "<machine-id>"))
    if cpu_model:
        table.append(("cpu model", False, cpu_model, "<cpu-model>"))
    if kernel_release:
        table.append(("kernel build", False, kernel_release, "<kernel-build>"))

    # Pattern-based rules that do not depend on reading this host: MAC
    # addresses and IPv4 addresses. Deliberately broad - this filter runs on
    # captured logs, not on authored prose, so a false positive there is
    # cheap and a missed leak is not.
    table.append(("MAC address", True, r"\b[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}\b", "<mac>"))
    table.append(("IPv4 address", True, r"\b\d{1,3}(\.\d{1,3}){3}\b", "<ip>"))

    return table


def apply_table(text: str, table: list[Rule]) -> str:
    for _description, is_regex, pattern, replacement in table:
        if is_regex:
            text = re.sub(pattern, replacement, text)
        else:
            text = text.replace(pattern, replacement)
    return text


def print_rules(table: list[Rule]) -> None:
    for _description, is_regex, pattern, replacement in table:
        prefix = "regex:" if is_regex else ""
        print(f"{prefix}{pattern}==>{replacement}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--filter", action="store_true", help="scrub stdin, write stdout")
    mode.add_argument(
        "--print-rules",
        action="store_true",
        help="print git filter-repo --replace-text rules (holds the account name; never commit the output)",
    )
    args = parser.parse_args(argv)

    if args.filter and not sys.flags.isolated:
        print(
            "scrub-host-ids.py: --filter reads untrusted input; run as "
            "`python3 -I scripts/scrub-host-ids.py --filter`",
            file=sys.stderr,
        )

    table = build_table()

    if args.print_rules:
        print_rules(table)
        return 0

    text = sys.stdin.read()
    sys.stdout.write(apply_table(text, table))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

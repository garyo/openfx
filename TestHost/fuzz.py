#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# ///
# Copyright OpenFX and contributors to the OpenFX project.
# SPDX-License-Identifier: BSD-3-Clause
"""Fuzz OpenFX plugins with ofxtesthost.

Runs the host in --randomize mode over a range of seeds, one process per run
so a plugin crash is just a finding, classifies what went wrong, shrinks each
distinct failure to the smallest command line that still reproduces it, and
prints a report.

Usage:
    uv run TestHost/fuzz.py HOST PLUGIN-PATH [--runs N] [--seed S] [--plugin ID]...

Findings, most to least severe:
    crash        the host died on a signal while the plugin handled an action
    error        an action failed (render, describe, create instance, ...)
    warning      the host flagged plugin misbehaviour: an unreleased image, a
                 property written with the wrong type or index, non-finite output
    timeout      the run exceeded --timeout seconds
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass, field


@dataclass
class Finding:
    kind: str  # crash, error, warning, timeout
    signature: str  # what makes two findings "the same"
    seed: int
    repro: list[str]  # host arguments reproducing it
    detail: str  # the line that triggered it
    reproduced: str = ""  # "3/3" style repeatability of the minimal command line


@dataclass
class Result:
    returncode: int | None
    stdout: str
    stderr: str
    timed_out: bool = False
    repro: list[str] = field(default_factory=list)


NOISE = re.compile(r"^\d\d:\d\d:\d\d|\[trace\]")  # plugins' own logging
VALUE_IN_MESSAGE = re.compile(r"\d+")


HOST_ENV: dict[str, str] = {}  # extra environment for the host process (--env)


def run_host(host: str, args: list[str], timeout: float) -> Result:
    try:
        p = subprocess.run([host, *args], capture_output=True, text=True, timeout=timeout, errors="replace",
                           env={**os.environ, **HOST_ENV})
    except subprocess.TimeoutExpired as e:
        return Result(None, str(e.stdout or ""), str(e.stderr or ""), timed_out=True)
    result = Result(p.returncode, p.stdout, p.stderr)
    for line in p.stdout.splitlines():
        if line.startswith("repro: ofxtesthost "):
            result.repro = shlex.split(line[len("repro: ofxtesthost "):])
    return result


def classify(r: Result) -> tuple[str, str, str] | None:
    """(kind, signature, detail) for the most severe problem in a run, or None."""
    lines = [ln for ln in (r.stdout + "\n" + r.stderr).splitlines() if not NOISE.search(ln)]
    if r.timed_out:
        return "timeout", "timeout", "run exceeded the time limit"
    for ln in lines:
        if ln.startswith("FATAL:"):
            return "crash", ln.strip(), ln.strip()
    if r.returncode is not None and r.returncode < 0:
        return "crash", f"signal {-r.returncode}", f"killed by signal {-r.returncode}"
    for ln in lines:
        if ln.startswith("ERROR:"):
            sig = VALUE_IN_MESSAGE.sub("N", ln.strip())
            return "error", sig, ln.strip()
    for ln in lines:
        if ln.startswith("  ! "):
            sig = VALUE_IN_MESSAGE.sub("N", ln.strip())
            return "warning", sig, ln.strip()
    if r.returncode:
        return "error", f"exit {r.returncode}", f"exit status {r.returncode}"
    return None


def option_names(args: list[str]) -> frozenset[str]:
    """The options a command line uses, with --param/--clip names, ignoring values."""
    names = set()
    for i, a in enumerate(args):
        if a in ("--param", "--clip") and i + 1 < len(args):
            names.add(f"{a} {args[i + 1].split('=')[0]}")
        elif a.startswith("--"):
            names.add(a)
    return frozenset(names)


def shrink(host: str, args: list[str], want: str, timeout: float) -> list[str]:
    """Drop options one group at a time while the failure signature persists."""
    groups: list[list[str]] = []
    i = 0
    while i < len(args):
        a = args[i]
        if a.startswith("--") and a not in ("--ramp", "--list", "--describe", "--check-tiles"):
            groups.append(args[i:i + 2])
            i += 2
        else:
            groups.append([a])
            i += 1
    essential = {"--plugin", "--context"}  # keep the effect selection itself
    changed = True
    while changed:
        changed = False
        for k, g in enumerate(groups):
            if not g[0].startswith("--") or g[0] in essential:
                continue
            trial = [x for j, grp in enumerate(groups) if j != k for x in grp]
            c = classify(run_host(host, trial, timeout))
            if c and c[1] == want:
                groups.pop(k)
                changed = True
                break
    return [x for g in groups for x in g]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", help="path to the ofxtesthost binary")
    ap.add_argument("plugins", help="bundle, .ofx binary, or directory of bundles")
    ap.add_argument("--runs", type=int, default=50, help="seeds per plugin (default 50)")
    ap.add_argument("--seed", type=int, default=1, help="first seed (default 1)")
    ap.add_argument("--plugin", action="append", default=[], help="only this plugin identifier (repeatable)")
    ap.add_argument("--timeout", type=float, default=60, help="seconds per run (default 60)")
    ap.add_argument("--no-shrink", action="store_true", help="report the full random command line")
    ap.add_argument("--warnings", action="store_true", help="also report warnings, not just crashes and errors")
    ap.add_argument("--shrink-per-signature", type=int, default=4,
                    help="occurrences of one failure signature to shrink, to tell apart causes sharing it (default 4)")
    ap.add_argument("--repeat", type=int, default=3,
                    help="re-run each minimal repro this many times to spot intermittent failures")
    ap.add_argument("--env", action="append", default=[], metavar="KEY=VALUE",
                    help="set an environment variable for the host process (repeatable); e.g. DYLD_INSERT_LIBRARIES=... "
                         "to preload a sanitizer runtime for an instrumented plugin, which the shell cannot pass "
                         "through the Python interpreter on macOS")
    o = ap.parse_args()
    for kv in o.env:
        key, _, value = kv.partition("=")
        HOST_ENV[key] = value

    listing = run_host(o.host, [o.plugins, "--list"], o.timeout)
    ids = [ln.split()[0] for ln in listing.stdout.splitlines() if ln and not NOISE.search(ln) and " v" in ln]
    if o.plugin:
        ids = [i for i in ids if i in o.plugin]
    if not ids:
        print("no plugins found", file=sys.stderr)
        for ln in (listing.stdout + listing.stderr).splitlines():
            if ln.strip() and not NOISE.search(ln):
                print(f"  host: {ln}", file=sys.stderr)
        return 2

    findings: dict[str, list[Finding]] = defaultdict(list)  # plugin -> findings
    counts: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    baseline_failures: dict[str, str] = {}
    for pid in ids:
        # A plugin that fails with no randomisation at all is not a fuzz finding.
        base = classify(run_host(o.host, [o.plugins, "--plugin", pid], o.timeout))
        if base and base[0] != "warning":
            baseline_failures[pid] = base[2]
            print(f"  {pid}: fails without randomisation, skipping: {base[2]}", file=sys.stderr)
            continue
        shrunk: dict[str, int] = defaultdict(int)  # signature -> occurrences shrunk so far
        minimal: set[tuple[str, frozenset[str]]] = set()  # (signature, options) already reported
        for seed in range(o.seed, o.seed + o.runs):
            args = [o.plugins, "--plugin", pid, "--randomize", str(seed)]
            r = run_host(o.host, args, o.timeout)
            c = classify(r)
            if not c or (c[0] == "warning" and not o.warnings):
                counts[pid]["ok" if not c else "warning"] += 1
                continue
            kind, sig, detail = c
            counts[pid][kind] += 1
            if shrunk[sig] >= o.shrink_per_signature:
                continue
            shrunk[sig] += 1
            repro = r.repro or args
            if not o.no_shrink and r.repro:
                repro = shrink(o.host, r.repro, sig, o.timeout)
            key = (sig, option_names(repro))
            if key in minimal:
                continue
            minimal.add(key)
            hits = sum(1 for _ in range(o.repeat) if (cc := classify(run_host(o.host, repro, o.timeout))) and cc[1] == sig)
            findings[pid].append(Finding(kind, sig, seed, repro, detail, f"{hits}/{o.repeat}"))
            print(f"  {pid}: {kind} at seed {seed} (reproduces {hits}/{o.repeat}): {detail}", file=sys.stderr)

    print("\n=== fuzz report ===")
    for pid in ids:
        if pid in baseline_failures:
            print(f"\n{pid}: not fuzzed, fails without randomisation: {baseline_failures[pid]}")
            continue
        c = counts[pid]
        summary = ", ".join(f"{n} {k}" for k, n in sorted(c.items(), key=lambda kv: kv[0]))
        print(f"\n{pid}: {o.runs} runs: {summary or 'nothing run'}")
        for f in findings[pid]:
            flaky = "" if f.reproduced.startswith(f.reproduced.split("/")[1]) else f"  (intermittent: reproduces {f.reproduced})"
            print(f"  [{f.kind}] {f.detail}{flaky}")
            print(f"      seed {f.seed}: ofxtesthost {shlex.join(f.repro)}")
    total = sum(len(v) for v in findings.values())
    print(f"\n{total} distinct finding{'s' if total != 1 else ''} across {len(ids)} plugin{'s' if len(ids) != 1 else ''}")
    return 1 if any(f.kind in ("crash", "error", "timeout") for v in findings.values() for f in v) else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Run the remaining non-mutating Can checks."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence


REPOSITORY = Path(__file__).resolve().parents[1]
CHECK_NAMES = ("python", "dependency")


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="append",
        choices=CHECK_NAMES,
        help="run only this check; may be repeated",
    )
    return parser.parse_args(argv)


def commands() -> Dict[str, List[str]]:
    return {
        "python": [
            sys.executable,
            "-B",
            "-m",
            "unittest",
            "discover",
            "-s",
            "Tests",
            "-p",
            "test_*.py",
        ],
        "dependency": [sys.executable, "-B", "scripts/dependency_check.py"],
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    selected = args.check or list(CHECK_NAMES)
    failures = []
    for name in selected:
        command = commands()[name]
        print(f"== {name} ==", flush=True)
        print("+ " + subprocess.list2cmdline(command), flush=True)
        try:
            result = subprocess.run(
                command,
                cwd=REPOSITORY,
                check=False,
            )
        except OSError as error:
            print(f"run_quality_checks: cannot run {name}: {error}", file=sys.stderr)
            failures.append(name)
            continue
        if result.returncode != 0:
            failures.append(name)

    if failures:
        print(
            "run_quality_checks: failed: " + ", ".join(failures),
            file=sys.stderr,
        )
        return 1
    print(f"run_quality_checks: {len(selected)} check(s) passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

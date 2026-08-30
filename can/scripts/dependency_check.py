#!/usr/bin/env python3
"""Reject configured reverse includes, symbol references, and code patterns."""

from __future__ import annotations

import argparse
import fnmatch
import json
import posixpath
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple


TOOL_VERSION = "1.5"
INCLUDE_DIRECTIVE_RE = re.compile(r"^\s*#\s*include\b")
C_SUFFIXES = {".c", ".h"}


def load_config(config_path: Path) -> Tuple[Path, Dict[str, Any]]:
    config_path = config_path.resolve()
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read config {config_path}: {error}") from error

    project_root = config.get("project_root", "..")
    if not isinstance(project_root, str) or not project_root:
        raise ValueError("project_root must be a non-empty path")
    return (config_path.parent / project_root).resolve(), config


def normalized_relative(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def matches_any(value: str, patterns: Iterable[str]) -> bool:
    normalized = value.replace("\\\\", "/")
    return any(fnmatch.fnmatchcase(normalized, pattern.replace("\\\\", "/"))
               for pattern in patterns)


def collect_files(root: Path,
                  config: Dict[str, Any],
                  requested_paths: Sequence[str]) -> List[Path]:
    explicit = bool(requested_paths)
    raw_paths = list(requested_paths) if explicit else list(
        config.get("source_roots", ["src"]))
    include_globs = config.get("include_globs", ["**/*.c", "**/*.h"])
    exclude_globs = config.get("exclude_globs", [])
    files: Dict[str, Path] = {}

    if not all(isinstance(pattern, str) for pattern in include_globs):
        raise ValueError("include_globs must contain strings")
    if not all(isinstance(pattern, str) for pattern in exclude_globs):
        raise ValueError("exclude_globs must contain strings")

    for raw_path in raw_paths:
        candidate = Path(raw_path)
        if not candidate.is_absolute():
            candidate = root / candidate
        candidate = candidate.resolve()
        if candidate.is_file():
            candidates = [candidate]
        elif candidate.is_dir():
            candidates = candidate.rglob("*")
        else:
            raise ValueError(f"path does not exist: {candidate}")

        for path in candidates:
            if not path.is_file() or path.suffix.lower() not in C_SUFFIXES:
                continue
            relative = normalized_relative(path, root)
            if matches_any(relative, exclude_globs):
                continue
            if not explicit and not matches_any(relative, include_globs):
                continue
            files[str(path).lower()] = path

    return sorted(files.values(),
                  key=lambda item: normalized_relative(item, root).lower())


def mask_comments_and_literals(text: str) -> str:
    result = list(text)
    index = 0
    state = "code"
    quote = ""

    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if current == "/" and following == "/":
                result[index:index + 2] = "  "
                index += 2
                state = "line_comment"
                continue
            if current == "/" and following == "*":
                result[index:index + 2] = "  "
                index += 2
                state = "block_comment"
                continue
            if current in {'"', "'"}:
                quote = current
                result[index] = " "
                state = "literal"
        elif state == "line_comment":
            if current == "\n":
                state = "code"
            else:
                result[index] = " "
        elif state == "block_comment":
            if current == "*" and following == "/":
                result[index:index + 2] = "  "
                index += 2
                state = "code"
                continue
            if current != "\n":
                result[index] = " "
        else:
            if current == "\\\\" and following:
                result[index] = " "
                if following != "\n":
                    result[index + 1] = " "
                index += 2
                continue
            if current == quote:
                result[index] = " "
                state = "code"
            elif current != "\n":
                result[index] = " "
        index += 1

    return "".join(result)


@dataclass(frozen=True)
class DependencyFinding:
    path: str
    line: int
    rule: str
    include: str
    message: str
    symbol: str = ""
    pattern: str = ""


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", help="files/directories; default: configured owned code")
    parser.add_argument(
        "--config",
        type=Path,
        default=Path(__file__).with_name("coding_quality_config.json"),
        help="quality configuration JSON",
    )
    parser.add_argument(
        "--json",
        nargs="?",
        const="-",
        metavar="FILE",
        help="emit JSON to FILE, or stdout when FILE is omitted/-",
    )
    return parser.parse_args(argv)


def include_candidates(source_relative: str, include: str) -> List[str]:
    normalized_include = posixpath.normpath(include.replace("\\", "/"))
    source_parent = Path(source_relative).parent.as_posix()
    resolved = posixpath.normpath(posixpath.join(source_parent, normalized_include))
    return [normalized_include, posixpath.basename(normalized_include), resolved]


def matches_include_any(candidate: str, patterns: Sequence[str]) -> bool:
    """Match include paths case-insensitively, as Windows compilers do."""
    return matches_any(candidate.casefold(), [pattern.casefold() for pattern in patterns])


def validate_project_include_roots(root: Path, config: Dict[str, Any]) -> List[Path]:
    configured_roots = config.get("project_include_roots", [])
    if not isinstance(configured_roots, list) or not all(
        isinstance(item, str) and item.strip() for item in configured_roots
    ):
        raise ValueError("project_include_roots must be a list of non-empty paths")

    include_roots: List[Path] = []
    for configured_root in configured_roots:
        include_root = root / configured_root
        if not include_root.is_dir():
            raise ValueError(
                "project_include_roots path is not a directory: "
                f"{configured_root}"
            )
        include_roots.append(include_root)
    return include_roots


def collect_project_include_aliases(root: Path, include_roots: Sequence[Path]) -> List[str]:
    """Return root-relative and include-root-relative aliases for project headers."""
    aliases = set()
    for include_root in include_roots:
        for path in include_root.rglob("*"):
            if not path.is_file():
                continue
            aliases.add(normalized_relative(path, root))
            aliases.add(path.relative_to(include_root).as_posix())
    return sorted(aliases)


def is_project_include(candidates: Sequence[str], project_include_aliases: Sequence[str]) -> bool:
    return any(
        matches_include_any(candidate, project_include_aliases)
        for candidate in candidates
    )


def logical_preprocessor_lines(
    text: str,
    masked: str,
) -> List[Tuple[int, str, str]]:
    """Return phase-2-spliced lines with their first physical line numbers."""
    raw_lines = text.splitlines()
    masked_lines = masked.splitlines()
    logical_lines: List[Tuple[int, str, str]] = []
    index = 0

    while index < len(raw_lines):
        line_number = index + 1
        raw_line = raw_lines[index]
        masked_line = masked_lines[index]
        while raw_line.endswith("\\") and index + 1 < len(raw_lines):
            raw_line = raw_line[:-1] + raw_lines[index + 1]
            masked_line = masked_line[:-1] + masked_lines[index + 1]
            index += 1
        logical_lines.append((line_number, raw_line, masked_line))
        index += 1

    return logical_lines


def parse_include_directive(
    raw_line: str,
    masked_line: str,
) -> Optional[Tuple[Optional[str], str]]:
    """Parse one preprocessor include, preserving unresolved macro arguments."""
    match = INCLUDE_DIRECTIVE_RE.match(masked_line)
    if match is None:
        return None

    index = match.end()
    while index < len(raw_line):
        if raw_line[index] in {'"', '<'}:
            break
        if not masked_line[index].isspace():
            break
        index += 1

    if index >= len(raw_line):
        return None, "<missing>"

    opener = raw_line[index]
    if opener not in {'"', '<'}:
        argument = masked_line[match.end():].strip()
        return None, argument or "<missing>"

    closer = '"' if opener == '"' else '>'
    end = raw_line.find(closer, index + 1)
    if end < 0:
        return None, raw_line[index:].strip() or "<missing>"
    return raw_line[index + 1:end], ""


def validate_rules(
    root: Path,
    config: Dict[str, Any],
    owned_files: Sequence[Path],
    project_include_roots: Sequence[Path],
) -> List[Dict[str, Any]]:
    rules = config.get("dependency_rules", [])
    if not isinstance(rules, list):
        raise ValueError("dependency_rules must be a list")
    owned_paths = [normalized_relative(path, root) for path in owned_files]
    names = set()
    for index, rule in enumerate(rules):
        if not isinstance(rule, dict):
            raise ValueError(f"dependency_rules[{index}] must be an object")
        for required in ("name", "from"):
            if not rule.get(required):
                raise ValueError(f"dependency_rules[{index}] lacks '{required}'")
        if rule["name"] in names:
            raise ValueError(f"duplicate dependency rule name: {rule['name']}")
        names.add(rule["name"])
        if not rule.get("reason"):
            raise ValueError(f"dependency rule '{rule['name']}' lacks a reason")
        if not isinstance(rule["from"], list) or not rule["from"] or not all(
            isinstance(item, str) and item.strip() for item in rule["from"]
        ):
            raise ValueError(f"dependency rule '{rule['name']}' has an invalid from list")
        for source_pattern in rule["from"]:
            if not any(matches_any(path, [source_pattern]) for path in owned_paths):
                raise ValueError(
                    f"dependency rule '{rule['name']}' from pattern matches no owned file: {source_pattern}"
                )
        includes = rule.get("forbid_includes", [])
        if "forbid_symbols" in rule:
            raise ValueError(
                f"dependency rule '{rule['name']}' uses obsolete forbid_symbols; "
                "use forbidden_symbols"
            )
        symbols = rule.get("forbidden_symbols", [])
        patterns = rule.get("forbidden_patterns", [])
        allowed_includes_present = "allowed_project_includes" in rule
        allowed_includes = rule.get("allowed_project_includes", [])
        if allowed_includes_present and not project_include_roots:
            raise ValueError(
                f"dependency rule '{rule['name']}' uses allowed_project_includes "
                "without project_include_roots"
            )
        if not includes and not symbols and not patterns and not allowed_includes_present:
            raise ValueError(f"dependency rule '{rule['name']}' has no forbidden dependency")
        if not isinstance(includes, list) or not all(
            isinstance(item, str) and item.strip() for item in includes
        ):
            raise ValueError(f"dependency rule '{rule['name']}' has invalid forbid_includes")
        if not isinstance(symbols, list) or not all(
            isinstance(item, str) and item.strip() for item in symbols
        ):
            raise ValueError(f"dependency rule '{rule['name']}' has invalid forbidden_symbols")
        if not isinstance(allowed_includes, list) or not all(
            isinstance(item, str) and item.strip() for item in allowed_includes
        ):
            raise ValueError(
                f"dependency rule '{rule['name']}' has invalid allowed_project_includes"
            )
        if not isinstance(patterns, list):
            raise ValueError(f"dependency rule '{rule['name']}' has invalid forbidden_patterns")
        for pattern_index, pattern in enumerate(patterns):
            expression = pattern if isinstance(pattern, str) else pattern.get("regex") if isinstance(pattern, dict) else None
            if not expression:
                raise ValueError(
                    f"dependency rule '{rule['name']}' forbidden_patterns[{pattern_index}] lacks regex"
                )
            try:
                compiled = re.compile(expression)
            except re.error as error:
                raise ValueError(
                    f"dependency rule '{rule['name']}' has invalid regex '{expression}': {error}"
                ) from error
            if compiled.match("") is not None:
                raise ValueError(
                    f"dependency rule '{rule['name']}' has a forbidden pattern that matches empty text"
                )
    coverage = config.get("dependency_rule_coverage", [])
    if not isinstance(coverage, list) or not all(
        isinstance(item, str) and item.strip() for item in coverage
    ):
        raise ValueError("dependency_rule_coverage must be a list of globs")
    for coverage_pattern in coverage:
        if not any(matches_any(path, [coverage_pattern]) for path in owned_paths):
            raise ValueError(
                "dependency_rule_coverage pattern matches no owned file: "
                f"{coverage_pattern}"
            )

    for owned_path in owned_paths:
        if not matches_any(owned_path, coverage):
            continue
        if not any(matches_any(owned_path, rule["from"]) for rule in rules):
            raise ValueError(
                "dependency rules do not classify covered owned file: "
                f"{owned_path}"
            )

    return rules


def scan_dependencies(
    root: Path,
    config: Dict[str, Any],
    files: Sequence[Path],
    owned_files: Sequence[Path],
) -> List[DependencyFinding]:
    project_include_roots = validate_project_include_roots(root, config)
    project_include_aliases = collect_project_include_aliases(
        root,
        project_include_roots,
    )
    rules = validate_rules(root, config, owned_files, project_include_roots)
    findings: List[DependencyFinding] = []
    for path in files:
        relative = normalized_relative(path, root)
        applicable = [rule for rule in rules if matches_any(relative, rule["from"])]
        if not applicable:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            findings.append(DependencyFinding(relative, 1, "file-readable", "", str(error)))
            continue
        masked = mask_comments_and_literals(text)
        for line_number, raw_line, masked_line in logical_preprocessor_lines(text, masked):
            parsed = parse_include_directive(raw_line, masked_line)
            if parsed is None:
                continue
            include, unresolved_argument = parsed
            for rule in applicable:
                forbidden = rule.get("forbid_includes", [])
                allowed_includes_present = "allowed_project_includes" in rule
                allowed_includes = rule.get("allowed_project_includes", [])
                if not forbidden and not allowed_includes_present:
                    continue
                if include is None:
                    findings.append(
                        DependencyFinding(
                            relative,
                            line_number,
                            rule["name"],
                            unresolved_argument,
                            f"{rule['reason']}; cannot resolve #include argument",
                        )
                    )
                    continue
                candidates = include_candidates(relative, include)
                if forbidden and any(
                    matches_include_any(candidate, forbidden) for candidate in candidates
                ):
                    findings.append(
                        DependencyFinding(
                            relative,
                            line_number,
                            rule["name"],
                            include,
                            rule["reason"],
                        )
                    )
                if (
                    allowed_includes_present
                    and is_project_include(candidates, project_include_aliases)
                    and not any(
                        matches_include_any(candidate, allowed_includes)
                        for candidate in candidates
                    )
                ):
                    findings.append(
                        DependencyFinding(
                            relative,
                            line_number,
                            rule["name"],
                            include,
                            f"{rule['reason']}; project include is not permitted "
                            "by this module's closed include contract",
                        )
                    )
        for symbol_match in re.finditer(r"\b[A-Za-z_]\w*\b", masked):
            symbol = symbol_match.group(0)
            line_number = masked.count("\n", 0, symbol_match.start()) + 1
            for rule in applicable:
                forbidden_symbols = rule.get("forbidden_symbols", [])
                if forbidden_symbols and matches_any(symbol, forbidden_symbols):
                    findings.append(
                        DependencyFinding(
                            relative,
                            line_number,
                            rule["name"],
                            "",
                            rule["reason"],
                            symbol,
                        )
                    )
        for rule in applicable:
            for pattern_entry in rule.get("forbidden_patterns", []):
                if isinstance(pattern_entry, str):
                    expression = pattern_entry
                    description = expression
                else:
                    expression = pattern_entry["regex"]
                    description = pattern_entry.get("description", expression)
                for pattern_match in re.finditer(expression, masked, flags=re.MULTILINE):
                    line_number = masked.count("\n", 0, pattern_match.start()) + 1
                    findings.append(
                        DependencyFinding(
                            relative,
                            line_number,
                            rule["name"],
                            "",
                            f"{rule['reason']} ({description})",
                            "",
                            expression,
                        )
                    )
    return sorted(findings, key=lambda item: (item.path.lower(), item.line, item.rule))


def build_report(root: Path, files: Sequence[Path], findings: Sequence[DependencyFinding]) -> Dict[str, Any]:
    by_rule: Dict[str, int] = {}
    for finding in findings:
        by_rule[finding.rule] = by_rule.get(finding.rule, 0) + 1
    return {
        "tool": "dependency_check",
        "version": TOOL_VERSION,
        "root": str(root),
        "files_scanned": len(files),
        "findings": [asdict(finding) for finding in findings],
        "summary": {"total": len(findings), "by_rule": by_rule},
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        root, config = load_config(args.config)
        files = collect_files(root, config, args.paths)
        owned_files = collect_files(root, config, [])
        findings = scan_dependencies(root, config, files, owned_files)
    except ValueError as error:
        print(f"dependency_check: {error}", file=sys.stderr)
        return 2

    report = build_report(root, files, findings)
    json_text = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.json == "-":
        sys.stdout.write(json_text)
    else:
        for finding in findings:
            if finding.include:
                dependency = f"include '{finding.include}'"
            elif finding.symbol:
                dependency = f"symbol '{finding.symbol}'"
            else:
                dependency = f"pattern '{finding.pattern}'"
            print(f"{finding.path}:{finding.line}: {finding.rule}: {dependency} is forbidden; {finding.message}")
        print(f"dependency_check: scanned {len(files)} file(s), found {len(findings)} reverse dependency issue(s)")
        if args.json:
            try:
                Path(args.json).write_text(json_text, encoding="utf-8", newline="\n")
            except OSError as error:
                print(f"dependency_check: cannot write JSON: {error}", file=sys.stderr)
                return 2
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())

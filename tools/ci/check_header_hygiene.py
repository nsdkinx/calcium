#!/usr/bin/env python3
"""Enforce Calcium's public-header invariants (P5, P6, and the level DAG).

This script is the mechanical enforcement of the vertical-integration promise.
Without it, third-party types leak into public headers within weeks and the
promise silently degrades into an aspiration.

Four checks:
  1. NO_EXTERNAL_INCLUDES  Public headers include only calcium/ or the C++ stdlib.
  2. NO_FOREIGN_IDENTIFIERS Public headers never name Skia/SDL/HarfBuzz/ICU/Twell.
  3. LEVEL_DAG             Module dependencies point strictly downward, acyclic.
  4. SELF_CONTAINED        Every public header parses standalone.

Usage:
    python tools/ci/check_header_hygiene.py [--repo-root DIR] [--verbose]

Exit code 0 = clean, 1 = violations found.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# Level DAG (docs/02-architecture.md §1)
#
# A module may depend ONLY on modules in this map's transitive closure below it.
# Adding an edge here is an architectural decision, not a convenience.
# ---------------------------------------------------------------------------
LEVEL_DEPENDENCIES: dict[str, set[str]] = {
    "core":          set(),
    "geometry":      {"core"},
    "graphics":      {"core", "geometry"},
    "text":          {"core", "geometry", "graphics"},
    "gpu":           {"core", "geometry"},
    "platform":      {"core", "geometry"},
    "animation":     {"core", "geometry"},
    "layer":         {"core", "geometry", "graphics", "animation"},
    "layout":        {"core", "geometry"},
    "accessibility": {"core", "geometry"},
    "view":          {"core", "geometry", "graphics", "animation", "layer",
                      "layout", "platform", "accessibility", "text"},
    "widget":        {"core", "geometry", "graphics", "animation", "layer",
                      "layout", "platform", "accessibility", "text", "view"},
    "compose":       {"core", "geometry", "graphics", "animation", "layer",
                      "layout", "platform", "accessibility", "text", "view",
                      "widget"},
}

# Identifier prefixes that must never appear in a public header.
# Each entry is (compiled pattern, human-readable origin).
FOREIGN_IDENTIFIER_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"\bSk[A-Z]\w*"),          "Skia"),
    (re.compile(r"\bsk_sp\b"),             "Skia"),
    (re.compile(r"\bGr[A-Z]\w*"),          "Skia/Ganesh"),
    (re.compile(r"\bSDL_\w+"),             "SDL"),
    (re.compile(r"\bhb_\w+"),              "HarfBuzz"),
    (re.compile(r"\bicu::\w+"),            "ICU"),
    (re.compile(r"\bU_[A-Z]\w*"),          "ICU"),
    (re.compile(r"\bUChar\b|\bUText\b"),   "ICU"),
    (re.compile(r"\btwell_\w+"),           "Twell"),
    (re.compile(r"\bTWELL_\w+"),           "Twell"),
    (re.compile(r"\bFT_\w+"),              "FreeType"),
    (re.compile(r"\bVk[A-Z]\w*"),          "Vulkan"),
    (re.compile(r"\bID3D1[12]\w*"),        "Direct3D"),
    (re.compile(r"\bMTL[A-Z]\w*"),         "Metal"),
    (re.compile(r"\bNS[A-Z]\w*"),          "AppKit/Foundation"),
    (re.compile(r"\bUI[A-Z]\w*Ref\b"),     "UIKit"),
    (re.compile(r"\bCG[A-Z]\w*"),          "CoreGraphics"),
    (re.compile(r"\bCA[A-Z]\w*Ref\b"),     "CoreAnimation"),
    (re.compile(r"\bjobject\b|\bJNIEnv\b"), "JNI"),
    (re.compile(r"\bHWND\b|\bHDC\b|\bLPCWSTR\b"), "Win32"),
]

# The C++ standard library headers a public Calcium header may include.
# Deliberately curated rather than permissive: <iostream> in a public header is
# a 300 KB static-init tax on every consumer, and <regex> is worse.
ALLOWED_STDLIB_HEADERS: frozenset[str] = frozenset({
    # C compatibility
    "cstddef", "cstdint", "cstring", "cmath", "climits", "cfloat", "cassert",
    "cstdio", "cstdlib", "cinttypes", "cstdarg",
    # Language support / traits
    "type_traits", "concepts", "limits", "compare", "version", "bit",
    "source_location", "typeinfo", "initializer_list", "utility",
    # Containers and views
    "array", "vector", "span", "string", "string_view", "optional", "variant",
    "tuple", "map", "unordered_map", "set", "unordered_set", "deque",
    # Algorithms / iteration
    "algorithm", "iterator", "numeric", "functional", "ranges",
    # Memory
    "memory", "new", "scoped_allocator",
    # Concurrency (atomics are needed by the intent queue's public types)
    "atomic", "mutex", "thread", "chrono", "condition_variable",
    # Formatting / errors
    "format", "system_error", "exception", "stdexcept",
})

INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')

# Strip comments and string literals before scanning for foreign identifiers, so
# prose like "// Skia implements this" is not a violation. This matters: the
# specs legitimately discuss backends by name.
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//[^\n]*")
STRING_LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')
CHAR_LITERAL = re.compile(r"'(?:[^'\\\n]|\\.)*'")


@dataclass
class Violation:
    path: Path
    line: int
    check: str
    message: str

    def render(self, repo_root: Path) -> str:
        try:
            rel = self.path.relative_to(repo_root)
        except ValueError:
            rel = self.path
        location = f"{rel.as_posix()}:{self.line}" if self.line else rel.as_posix()
        return f"  {location}: [{self.check}] {self.message}"


@dataclass
class HygieneReport:
    violations: list[Violation] = field(default_factory=list)
    headers_checked: int = 0

    @property
    def is_clean(self) -> bool:
        return not self.violations

    def add(self, path: Path, line: int, check: str, message: str) -> None:
        self.violations.append(Violation(path, line, check, message))


def strip_code_noise(source: str) -> str:
    """Blank out comments and literals, preserving line numbering."""
    def blank_preserving_newlines(match: re.Match[str]) -> str:
        return re.sub(r"[^\n]", " ", match.group(0))

    source = BLOCK_COMMENT.sub(blank_preserving_newlines, source)
    source = LINE_COMMENT.sub(blank_preserving_newlines, source)
    source = STRING_LITERAL.sub(blank_preserving_newlines, source)
    source = CHAR_LITERAL.sub(blank_preserving_newlines, source)
    return source


def module_of_public_header(path: Path, include_root: Path) -> str | None:
    """Return the module name for include/calcium/<module>/foo.hpp."""
    try:
        relative = path.relative_to(include_root / "calcium")
    except ValueError:
        return None
    return relative.parts[0] if len(relative.parts) > 1 else None


def check_includes(
    path: Path, source: str, include_root: Path, report: HygieneReport
) -> None:
    """Checks 1 and 3: external includes and the level DAG."""
    module = module_of_public_header(path, include_root)
    allowed_modules = LEVEL_DEPENDENCIES.get(module, set()) if module else None

    for line_number, line in enumerate(source.splitlines(), start=1):
        match = INCLUDE_PATTERN.match(line)
        if not match:
            continue
        bracket, target = match.group(1), match.group(2)

        if target.startswith("calcium/"):
            parts = Path(target).parts
            if len(parts) < 3:
                continue  # umbrella header, e.g. calcium/calcium.hpp
            dependency = parts[1]
            if module is None or allowed_modules is None:
                continue
            if dependency == module:
                continue
            if dependency not in LEVEL_DEPENDENCIES:
                report.add(path, line_number, "LEVEL_DAG",
                           f"unknown module '{dependency}' in include '{target}'")
            elif dependency not in allowed_modules:
                report.add(
                    path, line_number, "LEVEL_DAG",
                    f"'{module}' may not depend on '{dependency}' "
                    f"(would invert or widen the level DAG)")
            continue

        if bracket == "<" and target in ALLOWED_STDLIB_HEADERS:
            continue

        if bracket == "<" and "/" not in target and not target.endswith((".h", ".hpp")):
            report.add(
                path, line_number, "NO_EXTERNAL_INCLUDES",
                f"stdlib header <{target}> is not on the public allowlist; "
                f"add it to ALLOWED_STDLIB_HEADERS if genuinely needed")
            continue

        report.add(
            path, line_number, "NO_EXTERNAL_INCLUDES",
            f"public header includes non-Calcium, non-stdlib header "
            f"'{target}' (P5: dependencies live behind backend interfaces)")


def check_foreign_identifiers(path: Path, source: str, report: HygieneReport) -> None:
    """Check 2: no third-party type or macro names in public headers."""
    cleaned = strip_code_noise(source)
    for line_number, line in enumerate(cleaned.splitlines(), start=1):
        for pattern, origin in FOREIGN_IDENTIFIER_PATTERNS:
            found = pattern.search(line)
            if found:
                report.add(
                    path, line_number, "NO_FOREIGN_IDENTIFIERS",
                    f"'{found.group(0)}' is a {origin} identifier; public API "
                    f"must expose only Calcium types (P5)")
                break


def check_include_guard(path: Path, source: str, report: HygieneReport) -> None:
    """Check 4 (cheap form): every public header is self-guarded."""
    if "#pragma once" in source:
        return
    if re.search(r"^\s*#\s*ifndef\s+CALCIUM_\w+", source, re.MULTILINE):
        return
    report.add(path, 1, "SELF_CONTAINED",
               "missing '#pragma once' (or a CALCIUM_* include guard)")


def detect_dag_cycles(report: HygieneReport, repo_root: Path) -> None:
    """Check 3b: the declared dependency map itself must be acyclic."""
    visiting: set[str] = set()
    visited: set[str] = set()

    def walk(module: str, trail: list[str]) -> None:
        if module in visiting:
            cycle = " -> ".join(trail + [module])
            report.add(repo_root / "tools/ci/check_header_hygiene.py", 0,
                       "LEVEL_DAG", f"dependency cycle in LEVEL_DEPENDENCIES: {cycle}")
            return
        if module in visited:
            return
        visiting.add(module)
        for dependency in sorted(LEVEL_DEPENDENCIES.get(module, set())):
            walk(dependency, trail + [module])
        visiting.discard(module)
        visited.add(module)

    for module in sorted(LEVEL_DEPENDENCIES):
        walk(module, [])


def run(repo_root: Path, verbose: bool) -> HygieneReport:
    report = HygieneReport()
    include_root = repo_root / "include"

    if not include_root.is_dir():
        report.add(repo_root, 0, "SETUP",
                   f"include directory not found at {include_root}")
        return report

    detect_dag_cycles(report, repo_root)

    headers = sorted(include_root.rglob("*.hpp")) + sorted(include_root.rglob("*.h"))
    for header in headers:
        report.headers_checked += 1
        source = header.read_text(encoding="utf-8", errors="replace")
        if verbose:
            print(f"  checking {header.relative_to(repo_root).as_posix()}")
        check_includes(header, source, include_root, report)
        check_foreign_identifiers(header, source, report)
        check_include_guard(header, source, report)

    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Calcium public-header hygiene gate")
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    print(f"Calcium header hygiene gate  (root: {repo_root})")

    report = run(repo_root, args.verbose)

    print(f"Checked {report.headers_checked} public header(s).")
    if report.is_clean:
        print("PASS: public API surface is free of third-party leakage.")
        return 0

    print(f"\nFAIL: {len(report.violations)} violation(s):\n")
    for violation in report.violations:
        print(violation.render(repo_root))
    print("\nSee docs/01-principles.md (P5) and docs/03-project-structure.md §3.1.")
    return 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""conformance_coverage.py - the command conformance corpus coverage gate.

The command conformance corpus (``tests/conformance/commands/*.json``) is replayed
through six transports (direct registry, REST generic, REST resource routes, the
``cmd`` CLI, ``serve-stdio`` and the TUI ``InProcessClient``). Replaying it proves
*behavioural parity*; it does **not** prove *completeness*. A command could be
added to the live registry, or a catalog error code introduced, with no corpus
case exercising it, and every transport would still agree - on nothing. This tool
closes that gap: it fails when the corpus does not cover the live registry.

What it proves (each finding names the exact missing item - command, code, route
or waiver):

1. **Command coverage** - every registry command has at least one passing-shaped
   success case and, unless the descriptor takes no arguments and so has no
   documented failure (``command.list``, ``system.info``, ``db.tables``), at
   least one failure case.
2. **Error-code coverage** - every catalog error code appears as an
   ``expect.error.code`` (or a nested job-outcome ``error.code``) somewhere in the
   corpus, **or** is waived in ``tools/conformance_waivers.json`` with an owner, a
   reason and the path of a test that covers it instead (the path must exist).
3. **Route coverage** - every REST resource route in catalog section 5 has a
   corpus step whose ``rest_resource`` block uses that method + path pattern. The
   generic ``POST /api/v1/commands`` endpoint and the gated ``/api/v1/jobs*`` rows
   are the command-API surface, exercised through the generic endpoint rather than
   a synthesized resource route; when no ``rest_resource`` step carries them they
   are reported as a *note* (not a hard failure) provided their command is covered.
4. **Mutating coverage** - every mutating command has an ``authz_matrix_<cmd>``
   case (the generated authorization matrix; ``conformance_gen_authz.py --check``
   guards its freshness in its own CTest test).
5. **Dry-run coverage** - every descriptor with ``dry_run: true`` has a step that
   sends ``options.dry_run``; at least one representative ``unsupported`` case
   exists for the descriptors that do not support it.
6. **Orphans** - a corpus case whose top-level ``command`` is not in the live
   registry (and whose name is not an ``unknown_command_*`` fixture) is a stale
   case after a rename and fails.

With ``--report FILE`` it also emits a markdown *commands x transports* matrix
built from the per-transport summary JSONs every runner writes to
``$URLSHORTENER_CONFORMANCE_OUT`` (the C++ ``RunReport`` summary
``<transport>_summary.json`` and the Python ``_SummaryCollector`` summary
``conformance_summary_<transport>.json``), and surfaces the count of counted
skip-notes (e.g. ``lsof``/``netstat`` unavailable) those summaries carry.

Design constraints (mirrors the other conformance tooling):

* Python standard library only; runs under ``python3 -I``.
* The catalog error-code set is parsed through the **shared** parser in
  ``tools/conformance_lint.py`` so the gate and the linter cannot disagree.
* The live registry is read from ``url_shortener cmd command.list`` (via
  ``SIMPLE_HTTP_BIN`` / a ``cmake-build*/`` fallback), or injected with
  ``--registry-json FILE`` for an in-process / hermetic run (the wrapper test uses
  this to build synthetic negative scenarios without the binary).
* No wall-clock assertions; the tool is pure data analysis.

Usage::

    python tools/conformance_coverage.py \\
        --binary cmake-build/url_shortener \\
        --catalog docs/commands/README.md \\
        --corpus tests/conformance/commands \\
        --report coverage_matrix.md
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

#: Repository root: this tool lives in ``tools/`` directly under the root.
_REPO_ROOT = Path(__file__).resolve().parent.parent

# The catalog error-code parser is shared with the linter (spec: "shared parser
# with tools/conformance_lint.py"); import it from the sibling tools directory so
# the gate and the linter never diverge on what a valid error code is.
if str(_REPO_ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT / "tools"))

import conformance_lint as cl  # noqa: E402

#: The six transports a corpus case can be carried on, in report column order.
TRANSPORTS: Tuple[str, ...] = (
    "direct",
    "rest_generic",
    "rest_resource",
    "cli_cmd",
    "stdio",
    "tui",
)

#: Path prefixes of the command-API / generic surface in catalog section 5. A
#: route under one of these is exercised through the generic ``POST
#: /api/v1/commands`` endpoint rather than a synthesized resource route, so a
#: missing ``rest_resource`` step for it is a reported note, not a hard finding.
_GENERIC_ROUTE_PREFIXES: Tuple[str, ...] = ("/api/v1/commands", "/api/v1/jobs")

#: Case-name prefix that marks a deliberately-unknown-command fixture; such a
#: case's ``command`` is intentionally absent from the registry (rule 6 exempt).
_UNKNOWN_COMMAND_PREFIX = "unknown_command_"

#: Substrings in a summary skip reason that mark a counted tool-unavailable
#: skip-note (the ``lsof``/``netstat`` fallbacks of conformance_32 / e2e_19 and
#: any corpus skip phrased "unavailable"); surfaced as a reported number.
_SKIP_NOTE_TOKENS: Tuple[str, ...] = ("lsof", "netstat", "unavailable")


# ---------------------------------------------------------------------------
# Finding
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Finding:
    """A single coverage violation.

    :ivar category: Short machine tag of the failing check (``command``,
        ``error_code``, ``route``, ``mutating``, ``dry_run``, ``orphan``,
        ``waiver``).
    :ivar message: One-line human description naming the exact missing item.
    """

    category: str
    message: str

    def format(self) -> str:
        """Render as ``[category] message`` for the gate's failure output.

        :return: The formatted single-line diagnostic.
        """
        return f"[{self.category}] {self.message}"


# ---------------------------------------------------------------------------
# Registry (live binary or injected JSON)
# ---------------------------------------------------------------------------


def registry_commands(binary: str) -> List[Dict[str, Any]]:
    """Return the live command descriptors from ``<binary> cmd command.list``.

    The child runs ``cmd command.list --json - --raw`` (empty args on stdin) so
    stdout is exactly the ``result`` object ``{"commands": [ ... ]}``; it is never
    run through a shell and is bounded by a timeout.

    :param binary: Path to the ``url_shortener`` executable.
    :return: The descriptor list (one dict per registered command).
    :raises RuntimeError: If the child fails, times out, or prints no list.
    """
    argv = [binary, "cmd", "command.list", "--json", "-", "--raw"]
    try:
        proc = subprocess.run(
            argv,
            input=b"{}",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError("command.list timed out") from exc
    if proc.returncode != 0:
        raise RuntimeError(
            f"command.list exited {proc.returncode}: "
            f"{proc.stderr.decode('utf-8', 'replace')!r}"
        )
    return _commands_from_text(proc.stdout.decode("utf-8", "replace"))


def registry_commands_from_json(path: Path) -> List[Dict[str, Any]]:
    """Return command descriptors from an injected JSON file (``--registry-json``).

    The file is either the raw ``command.list`` ``result`` object
    (``{"commands": [...]}``) or a bare array of descriptors; both shapes are
    accepted so a test can hand-write a minimal fake registry.

    :param path: Path to the registry JSON file.
    :return: The descriptor list.
    :raises RuntimeError: If the file is missing, not JSON, or carries no list.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(f"cannot read --registry-json {path}: {exc}") from exc
    return _commands_from_text(text)


def _commands_from_text(text: str) -> List[Dict[str, Any]]:
    """Parse a ``command.list`` payload (object or array) into a descriptor list.

    :param text: The JSON text to parse.
    :return: The descriptor list.
    :raises RuntimeError: If the text is not JSON or carries no command list.
    """
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"command.list output is not JSON: {exc}") from exc
    commands = doc.get("commands") if isinstance(doc, dict) else doc
    if not isinstance(commands, list) or not commands:
        raise RuntimeError("command.list returned no commands")
    descriptors = [c for c in commands if isinstance(c, dict) and c.get("name")]
    if not descriptors:
        raise RuntimeError("command.list returned no named descriptors")
    return descriptors


# ---------------------------------------------------------------------------
# Catalog (error codes via the shared parser; section-5 routes)
# ---------------------------------------------------------------------------


def catalog_error_codes(catalog: Path) -> Set[str]:
    """Return the catalog error-code set using the linter's shared parser.

    :param catalog: Path to ``docs/commands/README.md``.
    :return: The non-empty set of valid ``error.code`` values.
    :raises RuntimeError: If no codes were parsed (propagated from the linter).
    """
    return cl.parse_error_codes(catalog)


def _backticked(cell: str) -> List[str]:
    """Return every ``` `token` ``` from a Markdown table cell, in order."""
    return re.findall(r"`([^`]+)`", cell)


#: A hand-written ``#### `command.name` - ...`` section-3 heading.
_COMMAND_HEADING_RE = re.compile(r"^####\s+`([a-z][a-z0-9_]*\.[a-z][a-z0-9_]*)`")

#: A hand-written ``Errors: `code`, `code`, ...`` line (section 3.1-3.3 prose).
_ERRORS_LINE_RE = re.compile(r"\bErrors:\s*(`[^`]+`(?:[,/ ]+`[^`]+`)*)")


def parse_documented_errors(catalog: Path) -> Dict[str, Set[str]]:
    """Parse the catalog's hand-written ``Errors:`` lines, keyed by command.

    Section 3's command subsections (``#### `link.create` ...``) may carry an
    ``Errors: `code`, `code`, ...`` line enumerating the error codes that command
    documents. This associates every such line with its nearest preceding command
    heading. A command absent from the result (``command.list``, ``system.info``,
    ``link.preview``, ``job.list``, ...) documents **no** error and so is exempt
    from the mandatory per-command failure case (spec check 1: "unless the
    descriptor has no documented error").

    :param catalog: Path to ``docs/commands/README.md``.
    :return: A ``command -> {error code}`` map holding only commands with a
        documented error (may be empty if the section format changes).
    """
    documented: Dict[str, Set[str]] = {}
    current: Optional[str] = None
    for line in catalog.read_text(encoding="utf-8").splitlines():
        heading = _COMMAND_HEADING_RE.match(line.strip())
        if heading:
            current = heading.group(1)
            continue
        if current is None:
            continue
        match = _ERRORS_LINE_RE.search(line)
        if match:
            codes = set(_backticked(match.group(1)))
            if codes:
                documented.setdefault(current, set()).update(codes)
    return documented


def catalog_routes(catalog: Path) -> List[Tuple[str, str]]:
    """Parse the ``(method, path)`` routes from catalog section 5.

    Section 5 is ``## 5. Static route -> command table``. Each data row's first
    column backtick-lists one or more routes. A full route token is
    ``METHOD /path``; a bare word token (``disable`` / ``restore``) is an
    alternative *final segment* of the preceding full route (the
    ``.../enable`` / ``disable`` / ``restore`` shorthand), expanded here so each
    alternative becomes its own route.

    :param catalog: Path to ``docs/commands/README.md``.
    :return: The list of ``(method, path-pattern)`` routes, order preserved;
        ``{slug}`` / ``{id}`` segments are left as literal pattern placeholders.
    :raises RuntimeError: If section 5 or its table cannot be found.
    """
    lines = catalog.read_text(encoding="utf-8").splitlines()
    in_section = False
    seen_separator = False
    routes: List[Tuple[str, str]] = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("## "):
            in_section = stripped.startswith("## 5.")
            seen_separator = False
            continue
        if not in_section:
            continue
        cells = cl._table_cells(line)
        if cells is None:
            if seen_separator and routes:
                break  # prose after the table ends the scan
            continue
        if cl._is_separator_row(cells):
            seen_separator = True
            continue
        if not seen_separator:
            continue  # header row
        if not cells:
            continue
        routes.extend(_row_routes(_backticked(cells[0])))
    if not routes:
        raise RuntimeError(
            f"no routes parsed from section 5 of {catalog}; the '## 5. Static "
            "route' table is missing or changed format"
        )
    return routes


def _row_routes(tokens: List[str]) -> List[Tuple[str, str]]:
    """Expand one section-5 row's backticked tokens into ``(method, path)`` pairs.

    :param tokens: The backtick tokens of the row's route column, in order.
    :return: One ``(method, path)`` pair per full or alternative-segment route.
    """
    out: List[Tuple[str, str]] = []
    last_method: Optional[str] = None
    last_path: Optional[str] = None
    for token in tokens:
        match = re.match(r"^([A-Z]+)\s+(/\S+)$", token.strip())
        if match:
            last_method, last_path = match.group(1), match.group(2)
            out.append((last_method, last_path))
        elif (
            last_method is not None
            and last_path is not None
            and "/" not in token
            and " " not in token
        ):
            # Bare alternative final segment of the preceding full route.
            base = last_path.rsplit("/", 1)[0]
            out.append((last_method, f"{base}/{token.strip()}"))
    return out


def _route_regex(path_pattern: str) -> "re.Pattern[str]":
    """Compile a section-5 path pattern into a full-match regex.

    Each ``{slug}`` / ``{id}`` segment matches one non-empty, slash-free segment;
    every other segment is matched literally.

    :param path_pattern: A section-5 path with ``{...}`` placeholders.
    :return: A compiled regex that full-matches an equivalent concrete path.
    """
    parts = []
    for segment in path_pattern.split("/"):
        if segment.startswith("{") and segment.endswith("}"):
            parts.append(r"[^/]+")
        else:
            parts.append(re.escape(segment))
    return re.compile("^" + "/".join(parts) + "$")


# ---------------------------------------------------------------------------
# Corpus index
# ---------------------------------------------------------------------------


@dataclass
class CorpusIndex:
    """Static view of the corpus used by :func:`check` and the matrix report.

    :ivar case_commands: Top-level ``command`` of every case, by case name.
    :ivar case_names_by_command: Case names grouped by top-level ``command``.
    :ivar success_commands: Commands with a step expecting success (``ok: true``
        or a job ``expect_accept``).
    :ivar failure_commands: Commands with a step expecting failure (``ok: false``).
    :ivar codes_seen: Error codes seen anywhere under an ``expect`` /
        ``expect_accept`` block.
    :ivar rest_routes_seen: ``(method, concrete-path)`` pairs of every
        ``rest_resource`` step.
    :ivar dry_run_commands: Commands with a step that sends ``options.dry_run``.
    :ivar unsupported_present: Whether any step expects the ``unsupported`` code.
    :ivar case_names: Every case name in the corpus (for the ``authz_matrix_*``
        and orphan checks).
    :ivar unknown_command_cases: Case names with a step expecting the
        ``unknown_command`` code; these deliberately target a command absent from
        the registry and are exempt from the orphan check.
    """

    case_commands: Dict[str, Optional[str]] = field(default_factory=dict)
    case_names_by_command: Dict[str, List[str]] = field(default_factory=dict)
    success_commands: Set[str] = field(default_factory=set)
    failure_commands: Set[str] = field(default_factory=set)
    codes_seen: Set[str] = field(default_factory=set)
    rest_routes_seen: Set[Tuple[str, str]] = field(default_factory=set)
    dry_run_commands: Set[str] = field(default_factory=set)
    unsupported_present: bool = False
    case_names: Set[str] = field(default_factory=set)
    unknown_command_cases: Set[str] = field(default_factory=set)


def _iter_error_codes(node: Any) -> List[str]:
    """Return every ``error.code`` string nested anywhere inside *node*.

    Walks iteratively (explicit stack) so deep input cannot overflow the call
    stack; collects the ``code`` of every ``{"error": {"code": <str>}}`` object,
    which covers both a top-level ``expect.error.code`` and a nested job-outcome
    ``expect.result.error.code``.

    :param node: Any JSON value from an ``expect`` / ``expect_accept`` block.
    :return: Every nested ``error.code`` string, in traversal order.
    """
    out: List[str] = []
    stack: List[Any] = [node]
    while stack:
        current = stack.pop()
        if isinstance(current, dict):
            error = current.get("error")
            if isinstance(error, dict) and isinstance(error.get("code"), str):
                out.append(error["code"])
            stack.extend(current.values())
        elif isinstance(current, list):
            stack.extend(current)
    return out


def corpus_index(corpus_dir: Path) -> CorpusIndex:
    """Build a :class:`CorpusIndex` from every ``*.json`` file in *corpus_dir*.

    :param corpus_dir: Directory of corpus files (data only; already linted).
    :return: The populated index.
    :raises RuntimeError: If a corpus file is not valid JSON.
    """
    index = CorpusIndex()
    for path in sorted(corpus_dir.glob("*.json")):
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"{path}: invalid JSON: {exc}") from exc
        for case in doc.get("cases", []):
            if isinstance(case, dict):
                _index_case(index, case)
    return index


def _index_case(index: CorpusIndex, case: Dict[str, Any]) -> None:
    """Fold one corpus *case* into *index*."""
    name = case.get("name")
    if isinstance(name, str):
        index.case_names.add(name)
    top_command = case.get("command")
    index.case_commands[name] = top_command if isinstance(top_command, str) else None
    if isinstance(top_command, str) and isinstance(name, str):
        index.case_names_by_command.setdefault(top_command, []).append(name)

    for step in case.get("steps", []):
        if not isinstance(step, dict):
            continue
        request = step.get("request")
        command = (
            request.get("command")
            if isinstance(request, dict) and isinstance(request.get("command"), str)
            else None
        )
        options = request.get("options") if isinstance(request, dict) else None
        if isinstance(options, dict) and "dry_run" in options and command:
            index.dry_run_commands.add(command)

        expect = step.get("expect")
        accept = step.get("expect_accept")
        if command:
            if isinstance(accept, dict) and accept.get("ok") is True:
                index.success_commands.add(command)
            if isinstance(expect, dict):
                if expect.get("ok") is True:
                    index.success_commands.add(command)
                elif expect.get("ok") is False:
                    index.failure_commands.add(command)

        for block in (expect, accept):
            if block is not None:
                for code in _iter_error_codes(block):
                    index.codes_seen.add(code)
                    if code == "unsupported":
                        index.unsupported_present = True
                    if code == "unknown_command" and isinstance(name, str):
                        index.unknown_command_cases.add(name)

        rest = step.get("rest_resource")
        if isinstance(rest, dict):
            method = rest.get("method")
            rpath = rest.get("path")
            if isinstance(method, str) and isinstance(rpath, str):
                index.rest_routes_seen.add((method.upper(), rpath))


# ---------------------------------------------------------------------------
# Waivers
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Waiver:
    """One reviewed error-code waiver.

    :ivar code: The waived error code.
    :ivar owner: Accountable owner of the waiver.
    :ivar reason: Why the code cannot appear in the corpus.
    :ivar evidence: Repo-relative path of the test that covers the code instead.
    """

    code: str
    owner: str
    reason: str
    evidence: str


def load_waivers(path: Optional[Path]) -> Dict[str, Waiver]:
    """Load the error-code waivers file.

    The file is ``{"policy": <str>, "waivers": {code: {owner, reason,
    evidence}}}``; a bare ``{code: "evidence/path"}`` map is also accepted (the
    minimal spec shape). Keys beginning with ``_`` and the ``policy`` key are
    metadata and ignored.

    :param path: Path to the waivers JSON, or ``None`` for no waivers.
    :return: A ``code -> Waiver`` map.
    :raises RuntimeError: If the file is present but not valid JSON or is shaped
        wrongly.
    """
    if path is None:
        return {}
    if not path.is_file():
        raise RuntimeError(f"waivers file {path} does not exist")
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(doc, dict):
        raise RuntimeError(f"{path}: waivers file must be a JSON object")
    raw = doc.get("waivers") if "waivers" in doc else doc
    if not isinstance(raw, dict):
        raise RuntimeError(f"{path}: 'waivers' must be an object")
    waivers: Dict[str, Waiver] = {}
    for code, value in raw.items():
        if code == "policy" or code.startswith("_"):
            continue
        if isinstance(value, str):
            waivers[code] = Waiver(code, owner="", reason="", evidence=value)
        elif isinstance(value, dict):
            waivers[code] = Waiver(
                code,
                owner=str(value.get("owner", "")),
                reason=str(value.get("reason", "")),
                evidence=str(value.get("evidence", value.get("test", ""))),
            )
        else:
            raise RuntimeError(
                f"{path}: waiver {code!r} must be a string path or an object"
            )
    return waivers


# ---------------------------------------------------------------------------
# The coverage check
# ---------------------------------------------------------------------------


@dataclass
class CheckResult:
    """Outcome of :func:`check`: hard findings plus reported notes.

    :ivar findings: Hard coverage violations (any makes the gate fail).
    :ivar route_notes: Generic/gated routes not exercised as resource routes but
        whose command is otherwise covered (reported, non-fatal).
    :ivar waived_codes: Error codes satisfied by an accepted waiver.
    """

    findings: List[Finding] = field(default_factory=list)
    route_notes: List[str] = field(default_factory=list)
    waived_codes: List[str] = field(default_factory=list)


def check(
    index: CorpusIndex,
    commands: List[Dict[str, Any]],
    codes: Set[str],
    routes: List[Tuple[str, str]],
    waivers: Dict[str, Waiver],
    documented_errors: Optional[Dict[str, Set[str]]] = None,
    repo_root: Path = _REPO_ROOT,
) -> CheckResult:
    """Run every coverage check and return the findings, notes and waived codes.

    :param index: The static corpus index.
    :param commands: The live registry descriptors.
    :param codes: The catalog error-code set.
    :param routes: The section-5 ``(method, path)`` routes.
    :param waivers: The accepted error-code waivers (``code -> Waiver``).
    :param documented_errors: ``command -> {error code}`` from the catalog's
        ``Errors:`` lines; a command absent here documents no error and is exempt
        from the mandatory per-command failure case. ``None`` means "treat every
        command as documenting an error" (strict; used by a synthetic test).
    :param repo_root: Root used to resolve a waiver's evidence path for existence.
    :return: A :class:`CheckResult`.
    """
    result = CheckResult()
    registry_names = {c["name"] for c in commands if isinstance(c.get("name"), str)}

    _check_commands(index, commands, documented_errors, result)
    _check_error_codes(index, codes, waivers, repo_root, result)
    _check_routes(index, routes, registry_names, result)
    _check_mutating(index, commands, result)
    _check_dry_run(index, commands, result)
    _check_orphans(index, registry_names, result)
    return result


def _check_commands(
    index: CorpusIndex,
    commands: List[Dict[str, Any]],
    documented_errors: Optional[Dict[str, Set[str]]],
    result: CheckResult,
) -> None:
    """Check 1: every command has a success case, and a failure case when the
    catalog documents an error for it (``command.list`` / ``system.info`` /
    ``link.preview`` document none and are exempt)."""
    for descriptor in commands:
        name = descriptor.get("name")
        if not isinstance(name, str):
            continue
        if name not in index.success_commands:
            result.findings.append(
                Finding(
                    "command",
                    f"command '{name}' has no corpus case expecting success "
                    "(ok:true or a job accept)",
                )
            )
        has_documented_error = (
            documented_errors is None or name in documented_errors
        )
        if has_documented_error and name not in index.failure_commands:
            result.findings.append(
                Finding(
                    "command",
                    f"command '{name}' documents an error in the catalog but has "
                    "no corpus case expecting a failure (ok:false)",
                )
            )


def _check_error_codes(
    index: CorpusIndex,
    codes: Set[str],
    waivers: Dict[str, Waiver],
    repo_root: Path,
    result: CheckResult,
) -> None:
    """Check 2: every catalog code appears in the corpus or is validly waived."""
    for code in sorted(codes):
        if code in index.codes_seen:
            continue
        waiver = waivers.get(code)
        if waiver is None:
            result.findings.append(
                Finding(
                    "error_code",
                    f"error code '{code}' is in the catalog table but no corpus "
                    "case expects it and it is not waived",
                )
            )
            continue
        if not waiver.evidence:
            result.findings.append(
                Finding(
                    "waiver",
                    f"waiver for '{code}' names no evidence test file",
                )
            )
            continue
        if not (repo_root / waiver.evidence).is_file():
            result.findings.append(
                Finding(
                    "waiver",
                    f"waiver for '{code}' names a missing evidence file "
                    f"'{waiver.evidence}'",
                )
            )
            continue
        if not waiver.owner or not waiver.reason:
            result.findings.append(
                Finding(
                    "waiver",
                    f"waiver for '{code}' must carry both an owner and a reason",
                )
            )
            continue
        result.waived_codes.append(code)

    # A waiver for a code that the corpus actually covers (or that is not a
    # catalog code) is stale and must be removed.
    for code in sorted(waivers):
        if code not in codes:
            result.findings.append(
                Finding(
                    "waiver",
                    f"waiver for '{code}' is not a catalog error code (stale)",
                )
            )
        elif code in index.codes_seen:
            result.findings.append(
                Finding(
                    "waiver",
                    f"waiver for '{code}' is unnecessary; the corpus covers it",
                )
            )


def _check_routes(
    index: CorpusIndex,
    routes: List[Tuple[str, str]],
    registry_names: Set[str],
    result: CheckResult,
) -> None:
    """Check 3: every section-5 resource route is exercised by a rest_resource
    step; generic/gated rows covered only through the generic endpoint become
    notes."""
    seen = index.rest_routes_seen
    for method, path_pattern in routes:
        regex = _route_regex(path_pattern)
        covered = any(
            m == method and regex.match(p) for (m, p) in seen
        )
        if covered:
            continue
        if any(path_pattern.startswith(prefix) for prefix in _GENERIC_ROUTE_PREFIXES):
            result.route_notes.append(
                f"{method} {path_pattern} is served by the generic command API "
                "and has no rest_resource corpus step (exercised generically)"
            )
            continue
        result.findings.append(
            Finding(
                "route",
                f"resource route {method} {path_pattern} (catalog section 5) has "
                "no corpus rest_resource step",
            )
        )


def _check_mutating(
    index: CorpusIndex, commands: List[Dict[str, Any]], result: CheckResult
) -> None:
    """Check 4: every mutating command has an ``authz_matrix_<cmd>`` case."""
    for descriptor in commands:
        name = descriptor.get("name")
        if not isinstance(name, str) or not descriptor.get("mutating"):
            continue
        case_name = "authz_matrix_" + name.replace(".", "_")
        if case_name not in index.case_names:
            result.findings.append(
                Finding(
                    "mutating",
                    f"mutating command '{name}' has no '{case_name}' case in the "
                    "generated authorization matrix",
                )
            )


def _check_dry_run(
    index: CorpusIndex, commands: List[Dict[str, Any]], result: CheckResult
) -> None:
    """Check 5: every ``dry_run`` descriptor has an ``options.dry_run`` step, and
    at least one representative ``unsupported`` case exists."""
    for descriptor in commands:
        name = descriptor.get("name")
        if not isinstance(name, str) or not descriptor.get("dry_run"):
            continue
        if name not in index.dry_run_commands:
            result.findings.append(
                Finding(
                    "dry_run",
                    f"descriptor '{name}' declares dry_run:true but no corpus "
                    "step sends options.dry_run for it",
                )
            )
    if not index.unsupported_present:
        result.findings.append(
            Finding(
                "dry_run",
                "no corpus case exercises the 'unsupported' code (expected a "
                "representative dry_run/async-unsupported case)",
            )
        )


def _check_orphans(
    index: CorpusIndex, registry_names: Set[str], result: CheckResult
) -> None:
    """Check 6: a case whose top-level command is not in the registry (and is not
    an ``unknown_command_*`` fixture) is a stale orphan."""
    for name, command in sorted(index.case_commands.items(), key=lambda kv: kv[0] or ""):
        if command is None or command in registry_names:
            continue
        if isinstance(name, str) and (
            name.startswith(_UNKNOWN_COMMAND_PREFIX)
            or name in index.unknown_command_cases
        ):
            continue
        result.findings.append(
            Finding(
                "orphan",
                f"case '{name}' targets command '{command}' which is not in the "
                "live registry (stale after a rename?)",
            )
        )


# ---------------------------------------------------------------------------
# Per-transport summaries + matrix report
# ---------------------------------------------------------------------------


def _load_summaries(summaries_dir: Path) -> Dict[str, Dict[str, Any]]:
    """Load every per-transport summary JSON found under *summaries_dir*.

    Reads both the C++ ``RunReport`` summary (``<transport>_summary.json``,
    ``{transport, ran, skipped:[{case,reason}]}``) and the Python
    ``_SummaryCollector`` summary (``conformance_summary_<transport>.json``,
    ``{transport, cases:[{name,command,suite,status,reason}]}``).

    :param summaries_dir: Directory the runners wrote their summaries to.
    :return: ``transport -> summary dict`` for every summary found (unreadable or
        malformed files are skipped).
    """
    summaries: Dict[str, Dict[str, Any]] = {}
    if not summaries_dir.is_dir():
        return summaries
    for path in sorted(summaries_dir.glob("*summary*.json")):
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        transport = doc.get("transport") if isinstance(doc, dict) else None
        if isinstance(transport, str):
            summaries[transport] = doc
    return summaries


def _transport_case_status(summary: Dict[str, Any]) -> Dict[str, str]:
    """Return a ``case-name -> status`` map for one transport summary.

    Normalizes both summary shapes: the Python summary carries an explicit
    per-case ``status`` (``pass`` / ``fail`` / ``skip``); the C++ summary carries
    only a ``skipped`` list, so every named skipped case is ``skip`` and the rest
    are left unknown here (the caller infers ``ran`` from the corpus case set).

    :param summary: One per-transport summary dict.
    :return: A ``case-name -> status`` map (only the cases the summary names).
    """
    status: Dict[str, str] = {}
    for case in summary.get("cases", []) or []:
        if isinstance(case, dict) and isinstance(case.get("name"), str):
            status[case["name"]] = str(case.get("status", "pass"))
    for skip in summary.get("skipped", []) or []:
        if isinstance(skip, dict) and isinstance(skip.get("case"), str):
            status[skip["case"]] = "skip"
    return status


def count_skip_notes(summaries: Dict[str, Dict[str, Any]]) -> Dict[str, int]:
    """Count counted tool-unavailable skip-notes across every summary.

    Scans every skip reason (both summary shapes) for the tokens in
    :data:`_SKIP_NOTE_TOKENS` (``lsof`` / ``netstat`` / ``unavailable``), which
    the carry-forward from subtask 21 (conformance_32 / e2e_19) and any corpus
    "unavailable" skip use, so reduced coverage is a surfaced number.

    :param summaries: The loaded per-transport summaries.
    :return: ``transport -> count`` of skip-notes matching a token (zero omitted).
    """
    counts: Dict[str, int] = {}
    for transport, summary in summaries.items():
        reasons: List[str] = []
        for case in summary.get("cases", []) or []:
            if isinstance(case, dict) and case.get("status") == "skip":
                reasons.append(str(case.get("reason") or ""))
        for skip in summary.get("skipped", []) or []:
            if isinstance(skip, dict):
                reasons.append(str(skip.get("reason") or ""))
        n = sum(
            1
            for reason in reasons
            if any(token in reason.lower() for token in _SKIP_NOTE_TOKENS)
        )
        if n:
            counts[transport] = n
    return counts


def build_matrix(
    index: CorpusIndex,
    commands: List[Dict[str, Any]],
    summaries: Dict[str, Dict[str, Any]],
) -> str:
    """Render the markdown *commands x transports* coverage matrix.

    Each cell shows ``ran/skip`` counts for that command's cases on that
    transport, computed from the loaded summaries. A transport with no summary
    renders ``-`` so a missing runner is visible rather than silently blank.

    :param index: The static corpus index (maps commands to their case names).
    :param commands: The live registry descriptors (row order).
    :param summaries: The loaded per-transport summaries.
    :return: A markdown document string.
    """
    statuses = {t: _transport_case_status(s) for t, s in summaries.items()}
    lines: List[str] = []
    lines.append("# Conformance coverage matrix")
    lines.append("")
    lines.append(
        "Cells are `ran/skip` counts of each command's corpus cases on each "
        "transport, from the per-transport run summaries."
    )
    lines.append("")
    header = "| Command | " + " | ".join(TRANSPORTS) + " |"
    sep = "|" + "---|" * (len(TRANSPORTS) + 1)
    lines.append(header)
    lines.append(sep)

    for descriptor in commands:
        name = descriptor.get("name")
        if not isinstance(name, str):
            continue
        case_names = index.case_names_by_command.get(name, [])
        cells = []
        for transport in TRANSPORTS:
            if transport not in summaries:
                cells.append("-")
                continue
            status = statuses[transport]
            skipped = sum(1 for cn in case_names if status.get(cn) == "skip")
            ran = len(case_names) - skipped
            cells.append(f"{ran}/{skipped}")
        lines.append(f"| `{name}` | " + " | ".join(cells) + " |")

    notes = count_skip_notes(summaries)
    lines.append("")
    if notes:
        rendered = ", ".join(f"{t}: {n}" for t, n in sorted(notes.items()))
        lines.append(f"**Counted skip-notes (reduced coverage):** {rendered}")
    else:
        lines.append("**Counted skip-notes (reduced coverage):** none observed")
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _find_binary_fallback() -> Optional[str]:
    """Return a ``url_shortener`` binary via ``SIMPLE_HTTP_BIN`` or cmake-build."""
    env = os.environ.get("SIMPLE_HTTP_BIN")
    if env and Path(env).is_file():
        return str(Path(env).resolve())
    candidates: List[Path] = []
    for build_dir in sorted(_REPO_ROOT.glob("cmake-build*")):
        for binary_name in ("url_shortener", "url_shortener.exe"):
            candidate = build_dir / binary_name
            if candidate.is_file():
                candidates.append(candidate)
    if not candidates:
        return None
    candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return str(candidates[0].resolve())


def _parse_args(argv: List[str]) -> argparse.Namespace:
    """Parse the gate's command-line arguments."""
    parser = argparse.ArgumentParser(
        prog="conformance_coverage.py",
        description="Command conformance corpus coverage gate.",
    )
    parser.add_argument("--binary", default=None, help="url_shortener executable")
    parser.add_argument(
        "--registry-json",
        default=None,
        help="read the registry from this JSON file instead of the binary",
    )
    parser.add_argument(
        "--catalog",
        default=str(_REPO_ROOT / "docs" / "commands" / "README.md"),
        help="command catalog markdown",
    )
    parser.add_argument(
        "--corpus",
        default=str(_REPO_ROOT / "tests" / "conformance" / "commands"),
        help="corpus directory",
    )
    parser.add_argument(
        "--waivers",
        default=str(_REPO_ROOT / "tools" / "conformance_waivers.json"),
        help="error-code waivers JSON",
    )
    parser.add_argument(
        "--report", default=None, help="write the markdown matrix here (optional)"
    )
    parser.add_argument(
        "--summaries",
        default=os.environ.get("URLSHORTENER_CONFORMANCE_OUT"),
        help="directory of per-transport run summaries (for --report)",
    )
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    """CLI entry point.

    Resolves the registry (``--registry-json`` or the binary), parses the catalog
    error codes and section-5 routes, indexes the corpus, runs every check and
    prints the findings. Optionally writes the ``--report`` matrix.

    :param argv: Argument strings without the program name.
    :return: ``0`` when the corpus fully covers the registry (any remaining gaps
        are waived or reported notes); ``1`` on any hard finding; ``2`` on a usage
        / environment error.
    """
    args = _parse_args(argv)
    try:
        if args.registry_json:
            commands = registry_commands_from_json(Path(args.registry_json))
        else:
            binary = args.binary or _find_binary_fallback()
            if not binary:
                print(
                    "conformance_coverage: no --binary / --registry-json and no "
                    "cmake-build binary found",
                    file=sys.stderr,
                )
                return 2
            commands = registry_commands(binary)
        codes = catalog_error_codes(Path(args.catalog))
        routes = catalog_routes(Path(args.catalog))
        documented_errors = parse_documented_errors(Path(args.catalog))
        index = corpus_index(Path(args.corpus))
        waivers = load_waivers(Path(args.waivers) if args.waivers else None)
    except RuntimeError as exc:
        print(f"conformance_coverage: {exc}", file=sys.stderr)
        return 2

    result = check(index, commands, codes, routes, waivers, documented_errors)

    if args.report:
        summaries_dir = Path(args.summaries) if args.summaries else None
        summaries = _load_summaries(summaries_dir) if summaries_dir else {}
        Path(args.report).write_text(
            build_matrix(index, commands, summaries), encoding="utf-8"
        )

    for code in result.waived_codes:
        waiver = waivers[code]
        print(
            f"waived error code '{code}': {waiver.reason} "
            f"(owner {waiver.owner}; evidence {waiver.evidence})"
        )
    for note in result.route_notes:
        print(f"note: {note}")

    if result.findings:
        print("")
        print("conformance coverage gate FAILED:")
        for finding in sorted(result.findings, key=lambda f: (f.category, f.message)):
            print(f"  {finding.format()}")
        return 1

    print(
        f"conformance coverage gate OK: {len(commands)} commands, {len(codes)} "
        f"error codes ({len(result.waived_codes)} waived), "
        f"{len(routes)} routes checked"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""conformance_lint.py - schema linter for the command conformance corpus.

The conformance corpus (``tests/conformance/commands/*.json``) is *data*, not
code: every later Task 07.0 subtask (adapter shims, corpus authoring, the
coverage gate) replays the same JSON cases through a different transport. This
linter fixes the exact on-disk schema (version 1) and rejects a malformed
corpus before any shim ever reads it, so each later subtask can trust one
unambiguous shape.

The authoritative, human-readable schema lives next to the corpus in
``tests/conformance/commands/README.md``; this module is the executable mirror
of that document. The thirteen numbered lint rules below map one-to-one onto the
rules documented there and onto the negative fixtures under
``tests/conformance/py/lint_negative/``.

Design constraints (spec 07.0/01):

* Python standard library only - no ``jsonschema`` or any third-party package.
* The linter never imports or runs the product binary.
* Error codes are parsed live from the command catalog
  (``docs/commands/README.md`` section 1) so that a code added by Task 01.0
  is picked up automatically; the list is never hard-coded here.

Public surface:

* ``main(argv) -> int`` - CLI entry point; exit 0 clean, 1 on any finding,
  prints ``file:case:step: RULE-n message`` lines to stdout.
* ``lint_paths(paths, catalog_path=None) -> list[Finding]`` - importable core
  used by the wrapper unittest.
"""

from __future__ import annotations

import glob
import json
import re
import sys
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Constants: the schema vocabulary (mirrors tests/conformance/commands/README.md)
# ---------------------------------------------------------------------------

#: Only schema version this linter understands. Additive changes keep this
#: value; any incompatible change must bump it (see "Constraints" in the spec).
CORPUS_VERSION = 1

#: Allowed keys at the corpus (top-level object) level.
CORPUS_KEYS = {
    "corpus_version",
    "suite",
    "description",
    "allow_unknown_codes",
    "cases",
}

#: Corpus keys that must be present.
CORPUS_REQUIRED = {"corpus_version", "suite", "cases"}

#: Allowed keys on a Case object.
CASE_KEYS = {
    "name",
    "doc",
    "command",
    "caller",
    "backend",
    "files",
    "transports",
    "steps",
}

#: Case keys that must be present.
CASE_REQUIRED = {"name", "doc", "command", "steps"}

#: Allowed keys on a Step object.
STEP_KEYS = {
    "label",
    "caller",
    "request",
    "raw_request",
    "expect",
    "save",
    "await_job",
    "expect_accept",
    "rest_resource",
    "http_status",
    "cli_exit",
}

#: Allowed keys on a request envelope (``args``/``options`` are freeform).
REQUEST_KEYS = {"v", "id", "command", "args", "options"}

#: Allowed keys on an Expect / expect_accept object (``result``/``job`` are
#: command-specific match objects and are not key-checked here).
EXPECT_KEYS = {"ok", "result", "job", "error", "match"}

#: Allowed keys on an ``error`` match object (``message`` is never compared).
ERROR_KEYS = {"code"}

#: Allowed keys on a ``transports`` object.
TRANSPORTS_KEYS = {"only", "skip"}

#: Allowed keys on an ``await_job`` object.
AWAIT_JOB_KEYS = {"var", "timeout_ms"}

#: Allowed keys on a ``rest_resource`` object.
REST_RESOURCE_KEYS = {"method", "path", "query", "body", "status"}

#: Valid ``match`` modes.
MATCH_MODES = {"exact", "subset"}

#: Valid ``caller`` roles.
CALLER_ROLES = {"admin", "user"}

#: Valid ``backend`` identifiers.
BACKENDS = {"sqlite", "inmemory"}

#: Every known transport id (used by the transports and backend rules).
TRANSPORT_IDS = {
    "direct",
    "rest_generic",
    "rest_resource",
    "cli_cmd",
    "stdio",
    "tui",
}

#: Out-of-process runners that cannot share an in-process ``inmemory`` store;
#: a case with ``backend: "inmemory"`` must restrict ``transports.only`` to
#: exclude these (rule 10). ``direct`` and ``tui`` run in the test process and
#: are the only transports that can observe an in-memory backend.
OUT_OF_PROCESS_TRANSPORTS = {"cli_cmd", "stdio", "rest_generic", "rest_resource"}

#: Expect keys whose value must be the ``<ts>`` matcher, never a literal
#: timestamp (rule 6).
TIMESTAMP_KEYS = {
    "created_at",
    "updated_at",
    "deleted_at",
    "started_at",
    "finished_at",
}

#: Case-name grammar: lower_snake_case, must start with a letter (rule 2).
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")

#: A dotted ``<group>.<verb>`` command name (rule 5).
COMMAND_RE = re.compile(r"^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$")

#: A matcher token such as ``<id>`` or ``<ts>`` (defined fully in subtask 02).
MATCHER_TOKEN_RE = re.compile(r"^<[^>]+>$")

#: A ``${var}`` substitution reference.
VAR_REF_RE = re.compile(r"\$\{([^}]+)\}")

#: A standalone ``${var}`` value (whole string is a single reference).
VAR_ONLY_RE = re.compile(r"^\$\{[A-Za-z_][A-Za-z0-9_]*\}$")

#: A confined file ``path`` arg: ``${tmp}/`` followed by a single component
#: (checked for safety with :func:`_bad_file_name_reason`). Rule 14.
TMP_PATH_RE = re.compile(r"^\$\{tmp\}/(.+)$")


# ---------------------------------------------------------------------------
# Finding
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Finding:
    """A single lint violation.

    :ivar rule: The 1-based rule number (1..14) that was violated; matches the
        numbered rule in ``tests/conformance/commands/README.md`` and the
        ``lint_negative`` fixture ``rule_NN_*.json``.
    :ivar file: Path (as supplied) of the offending corpus file.
    :ivar case: Case ``name`` the finding belongs to, or ``None`` for a
        corpus-level finding (e.g. invalid JSON, unknown top-level key).
    :ivar step: Human-locatable step identifier (``"step N"`` or its label),
        or ``None`` when the finding is not tied to a single step.
    :ivar msg: One-line human description of what is wrong.
    """

    rule: int
    file: str
    case: Optional[str]
    step: Optional[str]
    msg: str

    def format(self) -> str:
        """Render as ``file:case:step: RULE-n message`` for CLI output.

        :return: The formatted single-line diagnostic; absent ``case``/``step``
            segments are rendered as ``-``.
        """
        case = self.case if self.case is not None else "-"
        step = self.step if self.step is not None else "-"
        return f"{self.file}:{case}:{step}: RULE-{self.rule} {self.msg}"


# ---------------------------------------------------------------------------
# Catalog parsing (error codes + mutating command set)
# ---------------------------------------------------------------------------


def _default_catalog_path() -> Path:
    """Return the default command-catalog path relative to the repo root.

    ``tools/`` sits directly under the repository root, so the catalog is two
    levels up from this file. Resolving from ``__file__`` (not the process cwd)
    lets the linter run from any working directory, including the CTest build
    directory and PowerShell.

    :return: Absolute path to ``docs/commands/README.md``.
    """
    return Path(__file__).resolve().parent.parent / "docs" / "commands" / "README.md"


def _table_cells(line: str) -> Optional[List[str]]:
    """Split one Markdown table row into stripped inner cells.

    :param line: A single text line.
    :return: The inner cell strings for a ``| ... |`` row, or ``None`` when the
        line is not a table row. The leading/trailing empty cells produced by
        the bounding pipes are dropped.
    """
    stripped = line.strip()
    if not stripped.startswith("|"):
        return None
    parts = [cell.strip() for cell in stripped.split("|")]
    # Drop the empty strings created by the leading and trailing pipe.
    if parts and parts[0] == "":
        parts = parts[1:]
    if parts and parts[-1] == "":
        parts = parts[:-1]
    return parts


def _is_separator_row(cells: List[str]) -> bool:
    """Return ``True`` for a Markdown header separator row (``---`` cells)."""
    return all(set(cell) <= {"-", ":"} and cell != "" for cell in cells)


def _backticked(cell: str) -> List[str]:
    """Extract every ``` `token` ``` from a table cell.

    Handles a cell that lists several codes, e.g. ``` `invalid_slug` /
    `reserved_slug` ```.

    :param cell: A single Markdown table cell's text.
    :return: The backtick-delimited tokens, in order.
    """
    return re.findall(r"`([^`]+)`", cell)


def parse_error_codes(catalog_path: Path) -> Set[str]:
    """Parse the stable error-code set from the catalog's ``### Error codes``
    table (section 1 of ``docs/commands/README.md``).

    The parser reads the first column (which may hold more than one backticked
    code) of every data row of the table that immediately follows the
    ``### Error codes`` heading. It intentionally breaks loudly (empty set) if
    that heading or table disappears, so rule 4's test fails rather than
    silently accepting any code.

    :param catalog_path: Path to the command catalog Markdown file.
    :return: The non-empty set of valid ``error.code`` values.
    :raises FileNotFoundError: If ``catalog_path`` does not exist.
    :raises RuntimeError: If no codes were parsed (the ``### Error codes`` table
        moved or changed format); failing loudly keeps rule 4 from silently
        accepting every code.
    """
    lines = catalog_path.read_text(encoding="utf-8").splitlines()
    codes: Set[str] = set()
    in_section = False
    seen_header = False
    for line in lines:
        if line.strip().startswith("### "):
            in_section = line.strip() == "### Error codes"
            seen_header = False
            continue
        if not in_section:
            continue
        cells = _table_cells(line)
        if cells is None:
            # A blank line or prose after the table ends the scan.
            if seen_header:
                break
            continue
        if _is_separator_row(cells):
            seen_header = True
            continue
        if not seen_header:
            # This is the header row (``| Code | Meaning | ... |``).
            continue
        if cells:
            codes.update(_backticked(cells[0]))
    if not codes:
        raise RuntimeError(
            f"no error codes parsed from {catalog_path}; the '### Error codes' "
            "table is missing or its format changed (rule 4 would fail open)"
        )
    return codes


def parse_mutating_commands(catalog_path: Path) -> Set[str]:
    """Parse the set of mutating command names from the catalog's generated
    summary table (the ``| Command | Perm | Kind | Mut | ... |`` block in
    section 3.7).

    A command is mutating when its ``Mut`` column reads ``yes``. Used by rule
    12 (a mutating command run by a ``user`` caller must be denied).

    :param catalog_path: Path to the command catalog Markdown file.
    :return: The non-empty set of command names whose ``Mut`` column is ``yes``.
    :raises RuntimeError: If no mutating command was found (the generated
        summary table moved or changed format); failing loudly keeps rule 12
        from silently passing every ``user`` mutation.
    """
    lines = catalog_path.read_text(encoding="utf-8").splitlines()
    mutating: Set[str] = set()
    command_idx: Optional[int] = None
    mut_idx: Optional[int] = None
    for line in lines:
        cells = _table_cells(line)
        if cells is None:
            if command_idx is not None:
                # Table ended.
                command_idx = None
                mut_idx = None
            continue
        header = [c.lower() for c in cells]
        if "command" in header and "mut" in header:
            command_idx = header.index("command")
            mut_idx = header.index("mut")
            continue
        if command_idx is None or mut_idx is None:
            continue
        if _is_separator_row(cells):
            continue
        if len(cells) <= max(command_idx, mut_idx):
            continue
        names = _backticked(cells[command_idx])
        if not names:
            continue
        if cells[mut_idx].strip().lower() == "yes":
            mutating.update(names)
    if not mutating:
        raise RuntimeError(
            f"no mutating commands parsed from {catalog_path}; the generated "
            "'| Command | Perm | Kind | Mut | ... |' table is missing or its "
            "format changed (rule 12 would fail open)"
        )
    return mutating


# ---------------------------------------------------------------------------
# Per-rule helpers
# ---------------------------------------------------------------------------


def _is_matcher(value: str) -> bool:
    """Return ``True`` if *value* is a ``<...>`` matcher token."""
    return bool(MATCHER_TOKEN_RE.match(value))


def _is_var(value: str) -> bool:
    """Return ``True`` if *value* is a whole-string ``${var}`` reference."""
    return bool(VAR_ONLY_RE.match(value))


def _collect_var_refs(node: Any) -> Set[str]:
    """Return every ``${var}`` name referenced anywhere inside *node*.

    The walk is iterative (explicit stack) so arbitrarily deep input cannot
    overflow the Python call stack.

    :param node: Any JSON value (dict/list/str/scalar).
    :return: The set of referenced variable names (without ``${}``).
    """
    refs: Set[str] = set()
    stack: List[Any] = [node]
    while stack:
        current = stack.pop()
        if isinstance(current, str):
            refs.update(VAR_REF_RE.findall(current))
        elif isinstance(current, dict):
            for key, value in current.items():
                # ``${var}`` never appears in a key, but scan them defensively.
                refs.update(VAR_REF_RE.findall(key))
                stack.append(value)
        elif isinstance(current, list):
            stack.extend(current)
    return refs


def _iter_non_nfc_strings(node: Any) -> List[str]:
    """Return every string inside *node* that is not already Unicode NFC.

    Used by rule 13. The walk is iterative so arbitrarily deep input cannot
    overflow the call stack.

    :param node: Any JSON value (dict/list/str/scalar).
    :return: Each non-NFC string value found (object keys are checked too).
    """
    offenders: List[str] = []
    stack: List[Any] = [node]
    while stack:
        current = stack.pop()
        if isinstance(current, str):
            if unicodedata.normalize("NFC", current) != current:
                offenders.append(current)
        elif isinstance(current, dict):
            for key, value in current.items():
                if unicodedata.normalize("NFC", key) != key:
                    offenders.append(key)
                stack.append(value)
        elif isinstance(current, list):
            stack.extend(current)
    return offenders


def _bad_file_name_reason(key: str) -> Optional[str]:
    """Return why *key* is an unsafe ``files`` name, or ``None`` if it is safe.

    A ``files`` key is materialized as a bare file name inside the per-case temp
    dir, so it must not be empty, contain a path separator, be a directory alias
    (``.`` / ``..``), or start with ``~`` (home-dir expansion / tilde trap).

    :param key: The ``files`` map key to validate.
    :return: A short human reason string, or ``None`` when the key is safe.
    """
    if key == "":
        return "empty name"
    if "/" in key or "\\" in key:
        return "contains a path separator"
    if key in (".", ".."):
        return "is a directory alias"
    if key.startswith("~"):
        return "starts with '~'"
    return None


def _step_id(index: int, step: Any) -> str:
    """Return a human-locatable id for a step (``label`` if present)."""
    if isinstance(step, dict) and isinstance(step.get("label"), str):
        return f"step {index + 1} ({step['label']})"
    return f"step {index + 1}"


def _iter_path_arg_values(args: Any) -> List[str]:
    """Return every string value under a key named ``path`` within request args.

    Walks *args* iteratively (no recursion, so a hostile depth cannot overflow
    the stack) and collects the value of every ``path`` key it finds. Used by
    rule 14; the ``rest_resource.path`` HTTP route is deliberately **not** visited
    because this is only ever called with a request's ``args`` object.

    :param args: The request ``args`` value (any JSON node), or ``None``.
    :return: Every ``path`` string value, in traversal order.
    """
    out: List[str] = []
    stack: List[Any] = [args]
    while stack:
        current = stack.pop()
        if isinstance(current, dict):
            for key, value in current.items():
                if key == "path" and isinstance(value, str):
                    out.append(value)
                stack.append(value)
        elif isinstance(current, list):
            stack.extend(current)
    return out


def _step_path_arg_values(step: Any) -> List[str]:
    """Return the ``path`` arg string values of a step's request, if any.

    :param step: A step object (any value; non-dicts yield an empty list).
    :return: Every ``path`` arg value under ``step.request.args``.
    """
    if not isinstance(step, dict):
        return []
    request = step.get("request")
    if not isinstance(request, dict):
        return []
    return _iter_path_arg_values(request.get("args"))


# ---------------------------------------------------------------------------
# Core linter
# ---------------------------------------------------------------------------


class _Linter:
    """Stateful linter over one set of corpus files.

    The instance accumulates :class:`Finding` objects and the global set of
    case names (rule 2 is cross-file), then exposes them through
    :func:`lint_paths`.

    :ivar error_codes: Valid ``error.code`` values parsed from the catalog.
    :ivar mutating: Mutating command names parsed from the catalog.
    """

    def __init__(self, error_codes: Set[str], mutating: Set[str]) -> None:
        self.error_codes = error_codes
        self.mutating = mutating
        self.findings: List[Finding] = []
        # name -> file that first defined it (for cross-file uniqueness).
        self._names: Dict[str, str] = {}

    # -- emission -----------------------------------------------------------

    def _emit(
        self,
        rule: int,
        file: str,
        case: Optional[str],
        step: Optional[str],
        msg: str,
    ) -> None:
        self.findings.append(Finding(rule, file, case, step, msg))

    # -- file-level ---------------------------------------------------------

    def lint_file(self, path: Path) -> None:
        """Lint a single corpus file, appending any findings.

        :param path: Path to a ``*.json`` corpus file.
        """
        file = str(path)
        try:
            raw = path.read_text(encoding="utf-8")
        except OSError as exc:  # pragma: no cover - IO failure is environmental
            self._emit(1, file, None, None, f"cannot read file: {exc}")
            return
        try:
            doc = json.loads(raw)
        except json.JSONDecodeError as exc:
            self._emit(1, file, None, None, f"invalid JSON: {exc}")
            return
        except RecursionError:
            # Pathologically nested JSON overflows the decoder; treat as a
            # structural (rule 1) failure rather than letting it crash the run.
            self._emit(1, file, None, None, "JSON nesting too deep to parse")
            return
        except ValueError as exc:  # pragma: no cover - defensive
            # Any other json value error (e.g. a surrogate decode issue) is a
            # structural problem, not a linter crash.
            self._emit(1, file, None, None, f"invalid JSON: {exc}")
            return

        if not isinstance(doc, dict):
            self._emit(1, file, None, None, "top-level value must be an object")
            return

        # Rule 1: unknown / missing top-level keys, version.
        allow_unknown_codes = doc.get("allow_unknown_codes", False)
        for key in doc:
            if key not in CORPUS_KEYS:
                self._emit(1, file, None, None, f"unknown corpus key '{key}'")
        for key in CORPUS_REQUIRED:
            if key not in doc:
                self._emit(1, file, None, None, f"missing corpus key '{key}'")
        if doc.get("corpus_version") != CORPUS_VERSION:
            self._emit(
                1,
                file,
                None,
                None,
                f"corpus_version must be {CORPUS_VERSION}",
            )
        if "allow_unknown_codes" in doc and not isinstance(
            allow_unknown_codes, bool
        ):
            self._emit(1, file, None, None, "allow_unknown_codes must be bool")
            allow_unknown_codes = False

        cases = doc.get("cases")
        if not isinstance(cases, list):
            self._emit(1, file, None, None, "'cases' must be an array")
            return

        for case in cases:
            if not isinstance(case, dict):
                self._emit(1, file, None, None, "case must be an object")
                continue
            self._lint_case(file, case, bool(allow_unknown_codes))

    # -- case-level ---------------------------------------------------------

    def _lint_case(
        self, file: str, case: Dict[str, Any], allow_unknown_codes: bool
    ) -> None:
        name = case.get("name")
        name_str = name if isinstance(name, str) else None

        # Rule 1: unknown / missing case keys.
        for key in case:
            if key not in CASE_KEYS:
                self._emit(1, file, name_str, None, f"unknown case key '{key}'")
        for key in CASE_REQUIRED:
            if key not in case:
                self._emit(1, file, name_str, None, f"missing case key '{key}'")

        # Rule 2: name regex + global uniqueness.
        if name_str is None:
            self._emit(2, file, None, None, "case 'name' must be a string")
        else:
            if not NAME_RE.match(name_str):
                self._emit(
                    2, file, name_str, None, "name must match ^[a-z][a-z0-9_]*$"
                )
            if name_str in self._names:
                self._emit(
                    2,
                    file,
                    name_str,
                    None,
                    f"duplicate case name (also in {self._names[name_str]})",
                )
            else:
                self._names[name_str] = file

        # Rule 1: caller enum (role has no dedicated rule -> structural).
        caller = case.get("caller", "admin")
        if caller not in CALLER_ROLES:
            self._emit(
                1, file, name_str, None, f"case caller must be admin|user, got {caller!r}"
            )
            caller = "admin"

        # Rule 10: backend enum + inmemory restriction.
        self._lint_backend(file, name_str, case)

        # Rule 9: transports.
        self._lint_transports(file, name_str, case)

        # Rule 11: files keys must be safe bare file names.
        files = case.get("files")
        if isinstance(files, dict):
            for key in files:
                reason = _bad_file_name_reason(key)
                if reason is not None:
                    self._emit(
                        11,
                        file,
                        name_str,
                        None,
                        f"files key '{key}' is not a safe bare file name: {reason}",
                    )

        steps = case.get("steps")
        if not isinstance(steps, list) or not steps:
            self._emit(1, file, name_str, None, "'steps' must be a non-empty array")
            return

        # Rule 14 (case-level): a case that uses a file `path` arg must scope its
        # transports, so a "path works" expectation is never measured against a
        # transport that refuses `path` by default (REST is default-deny
        # `unsupported`). The per-value shape is checked per step in _lint_step.
        if any(_step_path_arg_values(step) for step in steps):
            transports = case.get("transports")
            scoped = isinstance(transports, dict) and (
                "only" in transports or "skip" in transports
            )
            if not scoped:
                self._emit(
                    14,
                    file,
                    name_str,
                    None,
                    "a case with a 'path' arg must scope transports "
                    "(transports.only or transports.skip) so a 'path works' "
                    "expectation is never measured against a transport whose "
                    "default is to refuse 'path' with 'unsupported' (REST)",
                )

        defined_vars: Set[str] = set()
        for index, step in enumerate(steps):
            if not isinstance(step, dict):
                self._emit(1, file, name_str, _step_id(index, step), "step must be an object")
                continue
            self._lint_step(
                file, name_str, index, step, caller, allow_unknown_codes, defined_vars
            )

    def _lint_backend(
        self, file: str, name: Optional[str], case: Dict[str, Any]
    ) -> None:
        backend = case.get("backend", "sqlite")
        if backend not in BACKENDS:
            self._emit(
                10, file, name, None, f"backend must be sqlite|inmemory, got {backend!r}"
            )
            return
        if backend != "inmemory":
            return
        transports = case.get("transports")
        only = None
        if isinstance(transports, dict):
            only = transports.get("only")
        if not isinstance(only, list):
            self._emit(
                10,
                file,
                name,
                None,
                "backend 'inmemory' requires transports.only restricted to "
                "in-process transports (direct, tui)",
            )
            return
        bad = [t for t in only if t in OUT_OF_PROCESS_TRANSPORTS]
        if bad:
            self._emit(
                10,
                file,
                name,
                None,
                "backend 'inmemory' is incompatible with out-of-process "
                f"transports in transports.only: {sorted(bad)}",
            )

    def _lint_transports(
        self, file: str, name: Optional[str], case: Dict[str, Any]
    ) -> None:
        transports = case.get("transports")
        if transports is None:
            return
        if not isinstance(transports, dict):
            self._emit(9, file, name, None, "transports must be an object")
            return
        for key in transports:
            if key not in TRANSPORTS_KEYS:
                self._emit(9, file, name, None, f"unknown transports key '{key}'")
        has_only = "only" in transports
        has_skip = "skip" in transports
        if has_only and has_skip:
            self._emit(
                9, file, name, None, "transports.only and transports.skip are mutually exclusive"
            )
        if has_only:
            only = transports.get("only")
            if not isinstance(only, list):
                self._emit(9, file, name, None, "transports.only must be an array")
            else:
                for tid in only:
                    if tid not in TRANSPORT_IDS:
                        self._emit(9, file, name, None, f"unknown transport id '{tid}' in only")
        if has_skip:
            skip = transports.get("skip")
            if not isinstance(skip, dict):
                self._emit(9, file, name, None, "transports.skip must be an object")
            else:
                for tid, reason in skip.items():
                    if tid not in TRANSPORT_IDS:
                        self._emit(9, file, name, None, f"unknown transport id '{tid}' in skip")
                    if not isinstance(reason, str) or reason.strip() == "":
                        self._emit(
                            9, file, name, None, f"transports.skip['{tid}'] reason must be non-empty"
                        )

    # -- step-level ---------------------------------------------------------

    def _lint_step(
        self,
        file: str,
        name: Optional[str],
        index: int,
        step: Dict[str, Any],
        case_caller: str,
        allow_unknown_codes: bool,
        defined_vars: Set[str],
    ) -> None:
        sid = _step_id(index, step)

        # Rule 1: unknown step keys.
        for key in step:
            if key not in STEP_KEYS:
                self._emit(1, file, name, sid, f"unknown step key '{key}'")

        # Rule 1: caller enum.
        step_caller = step.get("caller", case_caller)
        if step_caller not in CALLER_ROLES:
            self._emit(1, file, name, sid, f"step caller must be admin|user, got {step_caller!r}")
            step_caller = case_caller

        has_request = "request" in step
        has_raw = "raw_request" in step

        # Rule 3: exactly one of request / raw_request.
        if has_request == has_raw:
            self._emit(
                3, file, name, sid, "a step must have exactly one of 'request' or 'raw_request'"
            )

        request = step.get("request")
        command: Optional[str] = None
        if has_request:
            if not isinstance(request, dict):
                self._emit(1, file, name, sid, "'request' must be an object")
            else:
                for key in request:
                    if key not in REQUEST_KEYS:
                        self._emit(1, file, name, sid, f"unknown request key '{key}'")
                command = request.get("command") if isinstance(request.get("command"), str) else None
                # Rule 5: dotted command (unless raw_request or unknown_command_*).
                name_exempt = bool(name) and name.startswith("unknown_command_")
                if command is not None and not COMMAND_RE.match(command) and not name_exempt:
                    self._emit(
                        5, file, name, sid, f"request.command '{command}' is not a dotted <group>.<verb>"
                    )

        # Rule 14 (value shape): every file `path` arg must be exactly
        # `${tmp}/<safe bare name>` - no literal absolute path, no `..`, and a
        # single component under the per-case temp dir. A `path` arg is free
        # text the C++/Python runners confine only by replay convention, so this
        # keeps every transport's replay inside the temp dir (no shim can rely on
        # a `--path-root`: `cmd` has none).
        for path_value in _step_path_arg_values(step):
            match = TMP_PATH_RE.match(path_value)
            if match is None:
                self._emit(
                    14,
                    file,
                    name,
                    sid,
                    f"path arg '{path_value}' must be '${{tmp}}/<name>' "
                    "(no literal absolute path, no '..', one component)",
                )
                continue
            reason = _bad_file_name_reason(match.group(1))
            if reason is not None:
                self._emit(
                    14,
                    file,
                    name,
                    sid,
                    f"path arg '{path_value}' component after '${{tmp}}/' is "
                    f"not a safe bare name: {reason}",
                )

        # Rule 8: await_job <-> expect_accept coupling.
        has_await = "await_job" in step
        has_accept = "expect_accept" in step
        if has_await and not has_accept:
            self._emit(8, file, name, sid, "await_job requires expect_accept")
        if has_accept and not has_await:
            self._emit(8, file, name, sid, "expect_accept is only allowed with await_job")

        # Rule 1: await_job / rest_resource structure.
        if has_await:
            await_job = step.get("await_job")
            if not isinstance(await_job, dict):
                self._emit(1, file, name, sid, "'await_job' must be an object")
            else:
                for key in await_job:
                    if key not in AWAIT_JOB_KEYS:
                        self._emit(1, file, name, sid, f"unknown await_job key '{key}'")
        if "rest_resource" in step:
            rr = step.get("rest_resource")
            if not isinstance(rr, dict):
                self._emit(1, file, name, sid, "'rest_resource' must be an object")
            else:
                for key in rr:
                    if key not in REST_RESOURCE_KEYS:
                        self._emit(1, file, name, sid, f"unknown rest_resource key '{key}'")

        # Rule 4/6: expect and expect_accept objects.
        expect = step.get("expect")
        if "expect" not in step:
            self._emit(4, file, name, sid, "step is missing required 'expect'")
        else:
            self._lint_expect(file, name, sid, expect, allow_unknown_codes, "expect")
        if has_accept:
            self._lint_expect(
                file, name, sid, step.get("expect_accept"), allow_unknown_codes, "expect_accept"
            )

        # Rule 7: ${var} used before it is saved.
        save = step.get("save")
        save_keys: Set[str] = set()
        if isinstance(save, dict):
            save_keys = set(save.keys())
        available = defined_vars | save_keys
        refs = set()
        for key in ("request", "raw_request", "expect", "expect_accept", "rest_resource"):
            if key in step:
                refs |= _collect_var_refs(step[key])
        if has_await and isinstance(step.get("await_job"), dict):
            var = step["await_job"].get("var")
            if isinstance(var, str):
                refs.add(var)
        for ref in sorted(refs):
            if ref == "tmp":
                continue
            if ref not in available:
                self._emit(
                    7, file, name, sid, f"${{{ref}}} is used before any step saves it"
                )
        defined_vars |= save_keys

        # Rule 12: mutating command run as user must be denied.
        if command is not None and command in self.mutating and step_caller == "user":
            if not self._expect_denied(expect):
                self._emit(
                    12,
                    file,
                    name,
                    sid,
                    f"mutating command '{command}' as caller 'user' must expect "
                    "error.code 'not_authorized'",
                )

        # Rule 13: every matched string must already be Unicode NFC. The C++
        # runner's NFC only composes the Latin-1 range, so a corpus that relied
        # on composing a wider codepoint would diverge from the Python matcher;
        # requiring pre-normalized strings keeps the two verdicts identical.
        for key in ("request", "raw_request", "expect", "expect_accept"):
            if key in step:
                for offender in _iter_non_nfc_strings(step[key]):
                    self._emit(
                        13,
                        file,
                        name,
                        sid,
                        f"string {offender!r} in '{key}' is not Unicode "
                        "NFC-normalized",
                    )

    def _expect_denied(self, expect: Any) -> bool:
        """Return ``True`` iff *expect* asserts ``not_authorized``."""
        if not isinstance(expect, dict):
            return False
        if expect.get("ok") is not False:
            return False
        error = expect.get("error")
        return isinstance(error, dict) and error.get("code") == "not_authorized"

    def _lint_expect(
        self,
        file: str,
        name: Optional[str],
        sid: str,
        expect: Any,
        allow_unknown_codes: bool,
        where: str,
    ) -> None:
        if not isinstance(expect, dict):
            self._emit(4, file, name, sid, f"'{where}' must be an object")
            return

        # Rule 1: unknown expect keys.
        for key in expect:
            if key not in EXPECT_KEYS:
                self._emit(1, file, name, sid, f"unknown {where} key '{key}'")

        # Rule 4: ok present and boolean.
        ok = expect.get("ok")
        if not isinstance(ok, bool):
            self._emit(4, file, name, sid, f"{where}.ok must be a boolean")

        # Rule 4: match mode.
        if "match" in expect and expect.get("match") not in MATCH_MODES:
            self._emit(4, file, name, sid, f"{where}.match must be exact|subset")

        # Rule 4: exactly one of result / job / error, consistent with ok.
        present = [k for k in ("result", "job", "error") if k in expect]
        if len(present) != 1:
            self._emit(
                4, file, name, sid, f"{where} must contain exactly one of result/job/error"
            )
        else:
            outcome = present[0]
            if ok is True and outcome == "error":
                self._emit(4, file, name, sid, f"{where}.ok=true cannot carry an 'error'")
            if ok is False and outcome != "error":
                self._emit(4, file, name, sid, f"{where}.ok=false must carry an 'error'")

        # Rule 4: error shape + code membership.
        error = expect.get("error")
        if isinstance(error, dict):
            for key in error:
                if key not in ERROR_KEYS:
                    self._emit(4, file, name, sid, f"{where}.error key '{key}' is not allowed")
            code = error.get("code")
            if not isinstance(code, str):
                self._emit(4, file, name, sid, f"{where}.error.code must be a string")
            elif code not in self.error_codes and not allow_unknown_codes:
                self._emit(
                    4, file, name, sid, f"{where}.error.code '{code}' is not in the catalog error table"
                )

        # Rule 6: timestamp / id literals anywhere inside expect.
        self._lint_literals(file, name, sid, expect, where)

    def _lint_literals(
        self, file: str, name: Optional[str], sid: str, node: Any, where: str
    ) -> None:
        # Iterative walk (explicit stack) so deep input cannot overflow the
        # Python call stack and crash the linter.
        stack: List[Any] = [node]
        while stack:
            current = stack.pop()
            if isinstance(current, dict):
                for key, value in current.items():
                    if key in TIMESTAMP_KEYS and not (
                        isinstance(value, str) and value == "<ts>"
                    ):
                        self._emit(
                            6, file, name, sid, f"{where}.{key} must be the <ts> matcher, not a literal"
                        )
                    if key == "id" and isinstance(value, str):
                        if not (_is_matcher(value) or _is_var(value)):
                            self._emit(
                                6,
                                file,
                                name,
                                sid,
                                f"{where} 'id' value '{value}' must be a matcher token or ${{var}}",
                            )
                    stack.append(value)
            elif isinstance(current, list):
                stack.extend(current)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------


def lint_paths(
    paths: List[Path], catalog_path: Optional[Path] = None
) -> List[Finding]:
    """Lint a set of corpus files and return every finding.

    Case-name uniqueness (rule 2) is enforced across the whole *paths* set, so
    pass the entire corpus in one call to catch cross-file collisions.

    :param paths: Corpus ``*.json`` files to lint, in the order they should be
        processed (the order only affects which file "owns" a duplicate name).
    :param catalog_path: Command-catalog Markdown file to parse error codes and
        mutating commands from; defaults to ``docs/commands/README.md``
        resolved relative to this tool.
    :return: All findings, sorted by file, rule, case and step.
    """
    catalog = catalog_path or _default_catalog_path()
    error_codes = parse_error_codes(catalog)
    mutating = parse_mutating_commands(catalog)
    linter = _Linter(error_codes, mutating)
    for path in paths:
        linter.lint_file(path)
    return sorted(
        linter.findings,
        key=lambda f: (f.file, f.rule, f.case or "", f.step or ""),
    )


def _expand_args(argv: List[str]) -> List[Path]:
    """Expand CLI arguments into a concrete, de-duplicated file list.

    Each argument may be a directory (globbed for ``*.json``), an existing
    file, or a shell/PowerShell glob pattern that the invoking shell did not
    expand. The result is sorted so output is deterministic.

    :param argv: Raw argument strings (without the program name).
    :return: Sorted, de-duplicated list of ``*.json`` paths.
    """
    collected: List[Path] = []
    seen: Set[str] = set()

    def add(p: Path) -> None:
        key = str(p)
        if key not in seen:
            seen.add(key)
            collected.append(p)

    for arg in argv:
        path = Path(arg)
        if path.is_dir():
            for match in sorted(path.glob("*.json")):
                add(match)
        elif path.exists():
            add(path)
        else:
            for match in sorted(glob.glob(arg)):
                add(Path(match))
    return sorted(collected, key=str)


def main(argv: List[str]) -> int:
    """CLI entry point.

    :param argv: Argument strings (without the program name): corpus files,
        directories, or glob patterns.
    :return: ``0`` when no findings, ``1`` on any finding or usage error.
    """
    if not argv:
        print("usage: conformance_lint.py <corpus.json | dir | glob> ...", file=sys.stderr)
        return 1
    paths = _expand_args(argv)
    if not paths:
        print("conformance_lint: no corpus files matched the arguments", file=sys.stderr)
        return 1
    findings = lint_paths(paths)
    for finding in findings:
        print(finding.format())
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

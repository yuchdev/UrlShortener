#!/usr/bin/env python3
"""conformance_match.py - the one normalizer + matcher for the command corpus.

A conformance case's ``expect`` block is compared against a *live* response
envelope by two independent implementations: this Python harness and the C++
runner of Task 07.0 subtask 03. To stop the two matchers from drifting, the
rules are pinned as data in
``tests/conformance/normalizer_vectors.json`` and both implementations must pass
every vector in it unchanged. This module is the Python side of that contract.

The authoritative, human-readable description of every rule and token lives next
to the corpus schema in ``tests/conformance/commands/README.md`` under
"Matchers and normalization"; this module is its executable mirror. The vector
file - not this code and not the prose - is the normative source: a rule change
edits the vectors first.

Design constraints (spec 07.0/02):

* Python standard library only; no third-party package.
* The regex dialect accepted by ``<regex:...>`` is restricted to constructs
  valid in both Python ``re`` and C++ ``std::regex`` ECMAScript (no lookbehind,
  no named groups, no possessive quantifiers) so the C++ runner matches
  identically; :func:`forbidden_regex_reason` is the lint that enforces it.
* Tokens are whole-string values and never match object keys, only values.

Public surface:

* :func:`substitute` - ``${name}`` expansion over requests and ``expect`` blocks.
* :func:`normalize_actual` - envelope normalization rules N1/N4/N5.
* :func:`match` - structural match of ``expect`` against an actual value.
* :func:`jsonpath_get` - the restricted ``$.a.b[0].c`` accessor used by ``save``.
* :func:`forbidden_regex_reason` - regex-dialect lint for ``<regex:...>``.
"""

from __future__ import annotations

import copy
import re
import unicodedata
from typing import Any, Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Token and grammar constants
# ---------------------------------------------------------------------------

#: Character class a ``<id>`` value must be drawn from (link ids, ``j-<n>`` job
#: ids, ``srv-<n>`` server ids). Mirrors the catalog id grammar.
_ID_RE = re.compile(r"[A-Za-z0-9_.:-]+")

#: Maximum length of an ``<id>`` value.
_ID_MAX_LEN = 64

#: RFC 3339 UTC timestamp ``YYYY-MM-DDThh:mm:ss[.fff]Z`` (ASCII, UTC only). The
#: fractional part is optional; the literal ``T`` and trailing ``Z`` are
#: required so non-UTC offsets and lowercase separators are rejected.
_TS_RE = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?Z")

#: The slug alphabet for ``<slug:N>``: base62, matching ``generateSlug`` in
#: ``src/core/utils.cpp``.
_SLUG_CHAR_RE = re.compile(r"[0-9A-Za-z]+")

#: A ``${name}`` substitution reference (name is an identifier).
_SUBST_RE = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")

#: One ``$.name`` or ``[index]`` segment of a restricted JSONPath. Anything the
#: expression cannot consume (wildcards, filters, ``..``) is rejected.
_JSONPATH_SEGMENT_RE = re.compile(r"\.([A-Za-z_][A-Za-z0-9_]*)|\[(\d+)\]")

#: Regex constructs that are valid in Python ``re`` but not in ``std::regex``
#: ECMAScript (or vice versa), each paired with a human reason. Used by
#: :func:`forbidden_regex_reason`.
_FORBIDDEN_REGEX = [
    (re.compile(r"\(\?<[=!]"), "lookbehind"),
    (re.compile(r"\(\?P[<=]"), "Python-only named group / backreference"),
    (re.compile(r"\(\?<[A-Za-z_]"), "named group"),
    (re.compile(r"\(\?'"), "named group"),
    (re.compile(r"(?:[*+?]|\})\+"), "possessive quantifier"),
]


# ---------------------------------------------------------------------------
# Variable substitution
# ---------------------------------------------------------------------------


def substitute(obj: Any, variables: Dict[str, Any]) -> Any:
    """Expand every ``${name}`` reference inside *obj* using *variables*.

    Substitution is applied to every string inside a request or an ``expect``
    block before the block is used. ``${tmp}`` expands to the per-case temp
    directory with ``/`` separators on every OS (backslashes in the ``tmp``
    value are rewritten). Objects and arrays are rebuilt recursively; non-string
    scalars pass through unchanged. The input is never mutated.

    :param obj: Any JSON value (dict / list / str / scalar / ``None``).
    :param variables: Mapping of variable name to value; a value is stringified
        with ``str`` when inlined into a larger string.
    :return: A new value with every ``${name}`` expanded.
    :raises ValueError: If a referenced variable name is not present in
        *variables* (an unsaved-variable reference is an error).
    """
    if isinstance(obj, dict):
        return {key: substitute(value, variables) for key, value in obj.items()}
    if isinstance(obj, list):
        return [substitute(value, variables) for value in obj]
    if isinstance(obj, str):
        return _substitute_str(obj, variables)
    return obj


def _substitute_str(text: str, variables: Dict[str, Any]) -> str:
    """Expand ``${name}`` references in a single string.

    :param text: The string to expand.
    :param variables: Variable mapping (see :func:`substitute`).
    :return: The expanded string.
    :raises ValueError: On a reference to an unknown variable.
    """

    def replace(match: "re.Match[str]") -> str:
        name = match.group(1)
        if name not in variables:
            raise ValueError(f"unknown variable '{name}' in substitution")
        value = variables[name]
        if name == "tmp" and isinstance(value, str):
            value = value.replace("\\", "/")
        return str(value)

    return _SUBST_RE.sub(replace, text)


# ---------------------------------------------------------------------------
# Normalization (N1, N4, N5)
# ---------------------------------------------------------------------------


def normalize_actual(
    envelope: Dict[str, Any],
    request: Optional[Dict[str, Any]],
    carries_id: bool,
) -> Tuple[Dict[str, Any], List[str]]:
    """Apply the envelope normalization rules N1, N4 and N5 to *envelope*.

    The remaining rules are realized by the matcher: N2 (key order) and N3
    (numeric / boolean value comparison) by structural comparison, N6 (NFC
    string comparison) by :func:`match`. The input envelope is never mutated; a
    deep copy is returned.

    * **N1** - ``error.message`` and ``error.details`` are deleted.
    * **N4** - if the request carried an ``id`` and the transport ``carries_id``,
      the actual ``id`` must equal it; otherwise it must match ``^srv-[0-9]+$``.
      A mismatch is reported as a finding. Either way the value is then replaced
      by the sentinel ``"<id>"`` so ``expect`` need not restate it.
    * **N5** - any top-level key other than ``v``, ``id``, ``ok``, ``result``,
      ``job`` or ``error`` is reported as a finding, regardless of match mode.

    :param envelope: The actual response envelope.
    :param request: The request envelope that produced *envelope*, or ``None``
        when the case replayed raw text; only its ``id`` is read.
    :param carries_id: Whether the transport echoes the request ``id`` (capability
        ``carries_id``); when ``False`` the server is expected to mint an
        ``srv-<n>`` id regardless of any request id.
    :return: ``(normalized_envelope, findings)`` where *findings* is the list of
        human-readable N4/N5 violations (empty when the envelope is well-formed);
        a non-empty list means the envelope fails to match.
    """
    env = copy.deepcopy(envelope)
    findings: List[str] = []

    # N1: drop the human-only error fields.
    error = env.get("error")
    if isinstance(error, dict):
        error.pop("message", None)
        error.pop("details", None)

    # N5: no extra top-level keys.
    allowed = {"v", "id", "ok", "result", "job", "error"}
    for key in env:
        if key not in allowed:
            findings.append(
                f"$.{key}: unexpected top-level envelope key (N5)"
            )

    # N4: id echo / generated-id check, then replace with the sentinel.
    if "id" in env:
        actual_id = env["id"]
        has_request_id = isinstance(request, dict) and "id" in request
        if has_request_id and carries_id:
            if actual_id != request["id"]:
                findings.append(
                    f"$.id: expected echoed request id {request['id']!r}, "
                    f"got {actual_id!r} (N4)"
                )
        elif not (
            isinstance(actual_id, str) and re.fullmatch(r"srv-[0-9]+", actual_id)
        ):
            findings.append(
                f"$.id: generated id {actual_id!r} does not match "
                "^srv-[0-9]+$ (N4)"
            )
        env["id"] = "<id>"

    return env, findings


# ---------------------------------------------------------------------------
# Matching
# ---------------------------------------------------------------------------


def match(
    expected: Any,
    actual: Any,
    mode: str,
    variables: Dict[str, Any],
    path: str = "$",
) -> List[str]:
    """Structurally match *expected* against *actual*.

    Returns the list of mismatch reasons; an empty list means the values match.
    Each reason is prefixed with the JSONPath-style location at which it was
    found. The comparison realizes rules N2 (object key order ignored), N3
    (numbers by value, booleans never equal numbers) and N6 (strings compared
    after NFC normalization, no trimming), plus the matcher tokens and the
    ``exact`` / ``subset`` modes.

    :param expected: The ``expect`` value (may contain matcher tokens).
    :param actual: The normalized actual value.
    :param mode: ``"exact"`` (extra object keys fail) or ``"subset"`` (extra
        object keys allowed; arrays still compared element-for-element).
    :param variables: Variable mapping consulted by the ``<var:name>`` token.
    :param path: JSONPath-style location of *expected* for diagnostics.
    :return: ``[]`` when the values match, else one ``"path: reason"`` string per
        mismatch.
    """
    if isinstance(expected, dict):
        return _match_object(expected, actual, mode, variables, path)
    if isinstance(expected, list):
        return _match_array(expected, actual, mode, variables, path)
    if isinstance(expected, str):
        return _match_string(expected, actual, mode, variables, path)
    if isinstance(expected, bool):
        if isinstance(actual, bool) and actual == expected:
            return []
        return [f"{path}: expected boolean {expected!r}, got {actual!r}"]
    if isinstance(expected, (int, float)):
        if isinstance(actual, bool) or not isinstance(actual, (int, float)):
            return [f"{path}: expected number {expected!r}, got {actual!r}"]
        if float(actual) == float(expected):
            return []
        return [f"{path}: expected number {expected!r}, got {actual!r}"]
    if expected is None:
        if actual is None:
            return []
        return [f"{path}: expected null, got {actual!r}"]
    return [f"{path}: unsupported expected type {type(expected).__name__}"]


def _match_object(
    expected: Dict[str, Any],
    actual: Any,
    mode: str,
    variables: Dict[str, Any],
    path: str,
) -> List[str]:
    """Match an expected object, honoring ``<absent>`` and the match mode."""
    if not isinstance(actual, dict):
        return [f"{path}: expected object, got {_json_type(actual)}"]
    reasons: List[str] = []
    expected_keys: set = set()
    absent_keys: set = set()
    for key, exp_val in expected.items():
        child = f"{path}.{key}"
        if isinstance(exp_val, str) and exp_val == "<absent>":
            absent_keys.add(key)
            if key in actual:
                reasons.append(f"{child}: expected key to be absent")
            continue
        expected_keys.add(key)
        if key not in actual:
            reasons.append(f"{child}: missing key")
            continue
        reasons += match(exp_val, actual[key], mode, variables, child)
    if mode == "exact":
        for key in actual:
            if key not in expected_keys and key not in absent_keys:
                reasons.append(f"{path}.{key}: unexpected key")
    return reasons


def _match_array(
    expected: List[Any],
    actual: Any,
    mode: str,
    variables: Dict[str, Any],
    path: str,
) -> List[str]:
    """Match an expected array (always element-for-element, even in subset)."""
    if not isinstance(actual, list):
        return [f"{path}: expected array, got {_json_type(actual)}"]
    if len(expected) != len(actual):
        return [
            f"{path}: array length {len(actual)} != expected {len(expected)}"
        ]
    reasons: List[str] = []
    for index, (exp_val, act_val) in enumerate(zip(expected, actual)):
        reasons += match(exp_val, act_val, mode, variables, f"{path}[{index}]")
    return reasons


def _match_string(
    expected: str,
    actual: Any,
    mode: str,
    variables: Dict[str, Any],
    path: str,
) -> List[str]:
    """Match an expected string, interpreting a recognized ``<...>`` token."""
    token = _parse_token(expected)
    if token is None:
        # Literal text, including an unrecognized ``<foo>`` token-looking string.
        if isinstance(actual, str) and _nfc(actual) == _nfc(expected):
            return []
        return [f"{path}: expected string {expected!r}, got {actual!r}"]
    # A value normalized to the token sentinel (e.g. N4 replaces id with
    # ``"<id>"``) matches the token trivially.
    if actual == expected:
        return []
    kind, arg = token
    reason = _match_token(kind, arg, actual, variables)
    if reason is None:
        return []
    return [f"{path}: {reason}"]


def _parse_token(value: str) -> Optional[Tuple[str, Optional[str]]]:
    """Classify *value* as a matcher token.

    :param value: A whole-string ``expect`` value.
    :return: ``(kind, argument)`` for a recognized token, else ``None`` (the
        value is literal text). ``argument`` is the part after ``:`` for
        parameterized tokens (``slug``, ``regex``, ``var``) and ``None``
        otherwise.
    """
    if value in ("<any>", "<id>", "<ts>", "<int>", "<uint>", "<absent>"):
        return value[1:-1], None
    for prefix, kind in (("<slug:", "slug"), ("<regex:", "regex"), ("<var:", "var")):
        if value.startswith(prefix) and value.endswith(">"):
            return kind, value[len(prefix):-1]
    return None


def _match_token(
    kind: str,
    arg: Optional[str],
    actual: Any,
    variables: Dict[str, Any],
) -> Optional[str]:
    """Return ``None`` if *actual* satisfies the token, else a mismatch reason.

    :param kind: Token kind (``any``/``id``/``ts``/``int``/``uint``/``slug``/
        ``regex``/``var``/``absent``).
    :param arg: Token argument for parameterized tokens, else ``None``.
    :param actual: The actual value to test.
    :param variables: Variable mapping for the ``var`` token.
    :return: ``None`` on success; a human-readable reason on failure.
    """
    if kind == "any":
        return None
    if kind == "id":
        if (
            isinstance(actual, str)
            and 1 <= len(actual) <= _ID_MAX_LEN
            and _ID_RE.fullmatch(actual)
        ):
            return None
        return f"expected <id>, got {actual!r}"
    if kind == "ts":
        if isinstance(actual, str) and _TS_RE.fullmatch(actual):
            return None
        return f"expected <ts> (RFC 3339 UTC), got {actual!r}"
    if kind == "int":
        return None if _as_int(actual) is not None else f"expected <int>, got {actual!r}"
    if kind == "uint":
        value = _as_int(actual)
        if value is not None and value >= 0:
            return None
        return f"expected <uint>, got {actual!r}"
    if kind == "slug":
        length = int(arg) if arg and arg.isdigit() else None
        if length is None:
            return f"malformed <slug:N> token argument {arg!r}"
        if (
            isinstance(actual, str)
            and len(actual) == length
            and _SLUG_CHAR_RE.fullmatch(actual)
        ):
            return None
        return f"expected <slug:{length}>, got {actual!r}"
    if kind == "regex":
        pattern = arg or ""
        bad = forbidden_regex_reason(pattern)
        if bad is not None:
            return f"<regex> pattern uses a disallowed construct: {bad}"
        if isinstance(actual, str) and re.fullmatch(pattern, actual):
            return None
        return f"expected <regex:{pattern}> full match, got {actual!r}"
    if kind == "var":
        name = arg or ""
        if name not in variables:
            return f"unknown variable {name!r} referenced by <var>"
        if _deep_equal(variables[name], actual):
            return None
        return f"expected <var:{name}> == {variables[name]!r}, got {actual!r}"
    if kind == "absent":
        return "<absent> may only be used as an object field value"
    return f"unknown matcher token kind {kind!r}"


# ---------------------------------------------------------------------------
# Value helpers (N3, N6)
# ---------------------------------------------------------------------------


def _nfc(text: str) -> str:
    """Return the Unicode NFC normalization of *text* (rule N6; no trimming)."""
    return unicodedata.normalize("NFC", text)


def _as_int(value: Any) -> Optional[int]:
    """Return *value* as an integer if it is one by rule N3, else ``None``.

    A JSON boolean is never an integer; an integral JSON float (``3.0``) is.
    """
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return None


def _deep_equal(left: Any, right: Any) -> bool:
    """Structurally compare two concrete JSON values by the N3/N6 value rules.

    Used by the ``<var:name>`` token to compare a saved value against the actual.

    :param left: The saved (expected) value.
    :param right: The actual value.
    :return: ``True`` when the values are equal under N3 (numbers by value,
        booleans distinct from numbers) and N6 (strings compared after NFC).
    """
    if isinstance(left, dict) and isinstance(right, dict):
        if set(left) != set(right):
            return False
        return all(_deep_equal(left[key], right[key]) for key in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(
            _deep_equal(a, b) for a, b in zip(left, right)
        )
    if isinstance(left, (dict, list)) or isinstance(right, (dict, list)):
        return False
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return float(left) == float(right)
    if isinstance(left, str) and isinstance(right, str):
        return _nfc(left) == _nfc(right)
    if left is None or right is None:
        return left is None and right is None
    return left == right


def _json_type(value: Any) -> str:
    """Return a short JSON type name for *value* (for diagnostics)."""
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, (int, float)):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    if isinstance(value, dict):
        return "object"
    return type(value).__name__


# ---------------------------------------------------------------------------
# Restricted JSONPath (used by ``save``)
# ---------------------------------------------------------------------------


def jsonpath_get(doc: Any, path: str) -> Any:
    """Resolve a restricted JSONPath of the form ``$.a.b[0].c`` against *doc*.

    Only dotted object keys and ``[index]`` array subscripts are supported.
    Wildcards (``*``), recursive descent (``..``) and filter expressions
    (``[?...]``) are rejected so the accessor cannot silently do more than the
    C++ runner.

    :param doc: The document (nested dict / list) to read from.
    :param path: The path expression; must start with ``$``.
    :return: The value at *path*.
    :raises ValueError: If *path* does not start with ``$`` or contains a
        segment that is not a dotted key or ``[index]`` subscript.
    :raises KeyError: If a named key is missing (or the current node is not an
        object).
    :raises IndexError: If an array index is out of range (or the current node
        is not an array).
    """
    if not isinstance(path, str) or not path.startswith("$"):
        raise ValueError(f"jsonpath must start with '$': {path!r}")
    rest = path[1:]
    pos = 0
    current = doc
    while pos < len(rest):
        segment = _JSONPATH_SEGMENT_RE.match(rest, pos)
        if segment is None:
            raise ValueError(
                f"unsupported jsonpath segment at {rest[pos:]!r} "
                "(no wildcards, '..' or filters)"
            )
        if segment.group(1) is not None:
            key = segment.group(1)
            if not isinstance(current, dict) or key not in current:
                raise KeyError(key)
            current = current[key]
        else:
            index = int(segment.group(2))
            if not isinstance(current, list) or index >= len(current):
                raise IndexError(index)
            current = current[index]
        pos = segment.end()
    return current


# ---------------------------------------------------------------------------
# Regex-dialect lint
# ---------------------------------------------------------------------------


def forbidden_regex_reason(pattern: str) -> Optional[str]:
    """Return why *pattern* is outside the shared regex dialect, else ``None``.

    The ``<regex:...>`` token must use only constructs valid in both Python
    ``re`` and C++ ``std::regex`` ECMAScript, so the two matchers agree. This
    rejects lookbehind, named groups / named backreferences, and possessive
    quantifiers.

    :param pattern: The regex pattern (the text after ``<regex:``).
    :return: A short human reason when *pattern* uses a disallowed construct,
        else ``None``.
    """
    for compiled, reason in _FORBIDDEN_REGEX:
        if compiled.search(pattern):
            return reason
    return None

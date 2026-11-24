#!/usr/bin/env python3
"""conformance_gen_authz.py - generate the authorization matrix conformance corpus.

The command conformance corpus (``tests/conformance/commands/*.json``) proves that
every transport answers identically. One part of that is authorization: a
read-only ``user`` caller must be refused *every* non-read command with
``not_authorized``, and - per ADR 0002 and the registry's ``execute`` pipeline -
that refusal happens **before** any argument validation (threat model T-08). The
descriptor x role matrix that pins this down must not be hand-maintained: a new
mutating command added to the registry would silently escape a stale hand-written
list. This tool therefore derives the matrix from the **live** catalog.

It spawns ``url_shortener cmd command.list`` (the same registry dump every
transport serves), reads each descriptor, and emits one case per descriptor whose
base permission is not ``read``: the step runs that command with minimal,
type-valid arguments as ``user`` and expects ``not_authorized``. Because
authorization precedes validation, the dummy arguments never decide the outcome -
a failure means authorization, not a bad argument (the matrix's whole point).

The generated file ``42_authorization_matrix.json`` is **committed**. CI
(subtask 15's gate) re-runs this tool with ``--check`` and fails if regeneration
would change the committed bytes, so adding a mutating command without
regenerating is caught.

Design constraints (mirrors the other conformance tooling):

* Python standard library only; no third-party package.
* The tool never writes outside ``tests/conformance/commands/``.
* Output is deterministic: ``command.list`` returns commands in sorted order and
  the serializer is pinned (``json.dumps(indent=2, ensure_ascii=False)`` plus a
  trailing newline), so the same binary always yields the same bytes.

Usage::

    python tools/conformance_gen_authz.py            # (re)write the matrix file
    python tools/conformance_gen_authz.py --check     # exit 1 if it is stale
    python tools/conformance_gen_authz.py --print      # write generated text to stdout

The binary is located via ``SIMPLE_HTTP_BIN`` or a ``cmake-build*/`` fallback,
exactly like the conformance shims.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

#: Repository root: this tool lives in ``tools/`` directly under the root.
_REPO_ROOT = Path(__file__).resolve().parent.parent

#: The single file this tool owns; it never writes anywhere else.
_OUTPUT_PATH = (
    _REPO_ROOT / "tests" / "conformance" / "commands" / "42_authorization_matrix.json"
)

#: Suite id recorded in the generated corpus (its logical grouping in reports).
_SUITE = "authorization_matrix"

#: One-line human description of the generated suite.
_DESCRIPTION = (
    "Generated descriptor x role matrix: every command whose base permission is "
    "not 'read' is refused for a read-only user with not_authorized, before any "
    "argument validation (ADR 0002 order: authorize, then ArgSpec check). "
    "Regenerate with tools/conformance_gen_authz.py; CI checks it with --check."
)

#: The error code a denied mutating command must produce.
_DENIED_CODE = "not_authorized"


def find_binary() -> str:
    """Return the path to the ``url_shortener`` binary.

    Honours ``SIMPLE_HTTP_BIN`` (the variable the conformance shims use), then
    falls back to a ``url_shortener`` (or ``url_shortener.exe``) under any
    ``cmake-build*/`` directory at the repo root, preferring the newest.

    :return: Absolute path to the executable.
    :raises RuntimeError: If no binary can be located.
    """
    env = os.environ.get("SIMPLE_HTTP_BIN")
    if env:
        path = Path(env)
        if path.is_file():
            return str(path.resolve())
        raise RuntimeError(f"SIMPLE_HTTP_BIN={env!r} is not a file")
    candidates: List[Path] = []
    for build_dir in sorted(_REPO_ROOT.glob("cmake-build*")):
        for name in ("url_shortener", "url_shortener.exe"):
            candidate = build_dir / name
            if candidate.is_file():
                candidates.append(candidate)
    if not candidates:
        raise RuntimeError(
            "no url_shortener binary found; set SIMPLE_HTTP_BIN or build into "
            "cmake-build/"
        )
    # Prefer the most recently modified build.
    candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return str(candidates[0].resolve())


def load_descriptors(binary: str) -> List[Dict[str, Any]]:
    """Spawn ``cmd command.list`` on *binary* and return its descriptor list.

    The command is run with ``--json -`` (empty args on stdin) and ``--raw`` so
    stdout is exactly the ``result`` object ``{ "commands": [ ... ] }``. The child
    is never run through a shell and is bounded by a timeout.

    :param binary: Path to the ``url_shortener`` executable.
    :return: The ``commands`` array: one descriptor dict per registered command.
    :raises RuntimeError: If the child fails, times out, or prints no command
        list.
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
    text = proc.stdout.decode("utf-8", "replace")
    try:
        result = json.loads(text)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"command.list stdout is not JSON: {exc}") from exc
    commands = result.get("commands")
    if not isinstance(commands, list) or not commands:
        raise RuntimeError("command.list returned no commands")
    return commands


def _minimal_value(arg: Dict[str, Any]) -> Any:
    """Return a minimal, type-valid value for one required argument.

    Type defaults follow the spec: ``string`` -> ``"x"``, ``bool`` -> ``true``,
    ``int`` -> ``1``, ``enum`` -> its first declared value. A ``timestamp`` gets a
    valid RFC 3339 UTC instant; ``list`` / ``map`` / ``object`` get a minimal
    container. The value never decides the outcome (authorization precedes
    validation); it only has to be present and type-plausible.

    :param arg: One descriptor argument object (``name``, ``type``, ``enum?``).
    :return: A JSON value appropriate to the argument's declared type.
    """
    arg_type = arg.get("type")
    if arg_type == "enum":
        values = arg.get("enum")
        if isinstance(values, list) and values:
            return values[0]
        return "x"
    if arg_type == "bool":
        return True
    if arg_type == "int":
        return 1
    if arg_type == "timestamp":
        return "1970-01-01T00:00:00Z"
    if arg_type == "list":
        return ["x"]
    if arg_type == "map":
        return {"k": "x"}
    if arg_type == "object":
        return {}
    # string and any unknown/future scalar type.
    return "x"


def _minimal_args(descriptor: Dict[str, Any]) -> Dict[str, Any]:
    """Build the minimal args object from a descriptor's required arguments.

    :param descriptor: One ``command.list`` descriptor.
    :return: A dict holding one value per required argument, in declaration order.
    """
    args: Dict[str, Any] = {}
    for arg in descriptor.get("args", []):
        if isinstance(arg, dict) and arg.get("required"):
            name = arg.get("name")
            if isinstance(name, str):
                args[name] = _minimal_value(arg)
    return args


def build_case(descriptor: Dict[str, Any]) -> Dict[str, Any]:
    """Build one matrix case for a non-read *descriptor*.

    :param descriptor: A ``command.list`` descriptor whose ``perm`` is not
        ``read``.
    :return: A corpus case asserting a ``user`` caller is denied the command with
        ``not_authorized``.
    """
    command = descriptor["name"]
    return {
        "name": "authz_matrix_" + command.replace(".", "_"),
        "doc": (
            f"a read-only user is refused {command} with not_authorized before "
            "any argument validation (authorize-then-validate, ADR 0002)"
        ),
        "command": command,
        "caller": "user",
        "steps": [
            {
                "request": {
                    "v": 1,
                    "command": command,
                    "args": _minimal_args(descriptor),
                },
                "expect": {"ok": False, "error": {"code": _DENIED_CODE}},
            }
        ],
    }


def build_corpus(descriptors: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Build the full matrix corpus object from the live *descriptors*.

    Only descriptors whose base ``perm`` is not ``read`` produce a case; they are
    emitted in ascending command-name order (``command.list`` already sorts, and
    this re-sort makes the output independent of any future list order).

    :param descriptors: The ``command.list`` descriptor array.
    :return: The corpus object ready to serialize.
    """
    non_read = [
        d
        for d in descriptors
        if isinstance(d, dict)
        and isinstance(d.get("name"), str)
        and d.get("perm") != "read"
    ]
    non_read.sort(key=lambda d: d["name"])
    return {
        "corpus_version": 1,
        "suite": _SUITE,
        "description": _DESCRIPTION,
        "cases": [build_case(d) for d in non_read],
    }


def render(corpus: Dict[str, Any]) -> str:
    """Serialize *corpus* to the pinned, deterministic on-disk text.

    :param corpus: The corpus object from :func:`build_corpus`.
    :return: The JSON text (2-space indent, non-ASCII preserved) with a trailing
        newline, exactly as the committed file must contain.
    """
    return json.dumps(corpus, indent=2, ensure_ascii=False) + "\n"


def generate_text() -> str:
    """Locate the binary, read the live catalog and render the matrix text.

    :return: The deterministic matrix-corpus text.
    """
    return render(build_corpus(load_descriptors(find_binary())))


def main(argv: List[str]) -> int:
    """CLI entry point.

    With no flag the matrix file is (re)written. ``--check`` regenerates in memory
    and compares against the committed file, exiting 1 on any difference or a
    missing file. ``--print`` writes the generated text to stdout and touches no
    file.

    :param argv: Argument strings without the program name.
    :return: ``0`` on success; ``1`` when ``--check`` finds drift or a file is
        missing; ``2`` on a usage error.
    """
    mode = "write"
    if argv:
        if len(argv) != 1 or argv[0] not in ("--check", "--print"):
            print(
                "usage: conformance_gen_authz.py [--check | --print]",
                file=sys.stderr,
            )
            return 2
        mode = argv[0].lstrip("-")

    try:
        text = generate_text()
    except RuntimeError as exc:
        print(f"conformance_gen_authz: {exc}", file=sys.stderr)
        return 2

    if mode == "print":
        sys.stdout.write(text)
        return 0
    if mode == "check":
        if not _OUTPUT_PATH.is_file():
            print(
                f"conformance_gen_authz: {_OUTPUT_PATH} is missing; run "
                "tools/conformance_gen_authz.py to create it",
                file=sys.stderr,
            )
            return 1
        current = _OUTPUT_PATH.read_text(encoding="utf-8")
        if current != text:
            print(
                f"conformance_gen_authz: {_OUTPUT_PATH} is stale; regenerate with "
                "tools/conformance_gen_authz.py",
                file=sys.stderr,
            )
            return 1
        return 0

    _OUTPUT_PATH.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

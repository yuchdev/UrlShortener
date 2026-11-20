#!/usr/bin/env python3
"""14_rest_resource_corpus.py - replay the command corpus over the REST resource routes.

Runs every corpus case (``tests/conformance/commands/*.json``) through a real
``url_shortener`` HTTP server and, for every step that carries a ``rest_resource``
block, the equivalent **resource route** (``POST /api/v1/links``,
``GET /api/v1/links/{slug}``, ``PATCH``/``DELETE``/``/enable``/``/stats``, the
gated ``GET /api/v1/links``, ``GET /api/v1/commands`` and ``/api/v1/jobs*`` rows).
It is the ``rest_resource`` sibling of ``13_rest_generic_corpus.py`` (subtask 08)
and shares its server/client plumbing through :mod:`conformance_rest` (Task 07.0
subtask 09).

What makes this adapter unique (catalog section 2 "REST access control",
plan.md C3): a resource route does **not** return a command envelope - it returns
today's legacy link/error body with today's status code. This is the only adapter
that *synthesizes* an envelope test-side so the shared matcher can compare the
``result`` body and the error code against the corpus, **without** reformatting
any legacy body on the server (constraint: the wrapper is test-side only). The
synthesis rules (subtask 09):

  * 2xx -> ``{"v":1,"ok":true,"id":"srv-0","result": <legacy body>}``.
  * 4xx/5xx -> ``{"v":1,"ok":false,"id":"srv-0","error":{"code": <envelope code>}}``
    where the envelope code is read from the legacy body at ``error.code`` (the
    key the legacy handlers emit, ``makeApiErrorResponse`` in
    ``src/http/request_handlers.cpp``) and translated from the legacy REST
    vocabulary (the error-table ``invalid_request``/``repository``/
    ``internal_error`` and the per-field REST decoder codes ``invalid_enabled``/
    ``invalid_expires_at``/``invalid_tags``/... and the stats ``invalid_bucket``/
    ``invalid_timestamp``, which all mean the envelope's ``invalid_field``) to the
    envelope vocabulary through :data:`rest_resource_status.json`'s
    ``legacy_code_to_envelope`` map.

  The synthesized top-level ``id`` is the minimal N4-conformant server id
  sentinel (``srv-0``): a resource route carries no envelope id, but the shared
  matcher's expected envelope always contains ``id: "<id>"`` and N4 requires a
  ``srv-<n>`` id when the request carried none; ``rest_resource`` has
  ``caps.id = False`` so the value is masked to ``<id>`` and never compared.

Status handling (catalog C3 - "legacy statuses win"):

  * The adapter asserts the real resource-route HTTP status equals the corpus
    step's declared ``rest_resource.status`` **and** that this equals the
    data-driven :data:`rest_resource_status.json` entry for the command+outcome,
    so a corpus author cannot silently weaken a status (e.g. claim 200 for a
    create that is really 201).
  * The status it reports to :func:`conformance_common.run_case` is the shared
    catalog status (``conformance_common.expected_http_status``), because the
    ``http_status`` override and the catalog status channel are shared across
    every REST transport (the generic endpoint returns 200 for a create that the
    resource route returns as 201). The legacy-exact status check above is what
    actually pins the resource-route status.

Setup steps without a ``rest_resource`` block fall back to the generic
``POST /api/v1/commands`` path on this adapter (corpus README: "absent = step
runs through rest_generic on that adapter"), reusing the embedded
:class:`conformance_rest.RestGenericAdapter`'s one-server-per-role lifecycle.

Security (threat brief ruling (c), same as subtask 08):

  * one server per role - a ``--command-api-role admin`` server for ``admin``
    steps/resource calls and a separate ``--command-api-role user`` server for
    ``user`` steps. The role comes only from the operator flag, never from corpus
    ``args`` / ``options``. The ``--command-api-role``/loopback gate only governs
    the generic ``POST /api/v1/commands`` endpoint and the gated read rows
    (``GET /api/v1/links``, ``/api/v1/jobs*``), so a ``user`` case on a gated row
    expects ``not_authorized``. The resource WRITE routes
    (``link.create``/``update``/``delete``/``enable``/``disable``/``restore``)
    run through ``resource_route_caller()``, which is hard-wired to
    ``ControlSet::Admin`` with **no** peer or role check
    (``src/http/command_bridge.cpp``) - the documented T-60 open-routes posture
    deferred to Milestone 0002. **This shim therefore cannot prove per-role write
    authorization on the resource routes, because the product does not gate them
    in 0006**; it exercises their result/status parity only, and per-role write
    denial is covered on the generic endpoint instead.
  * file paths stay default-deny (no ``--path-root`` / ``--no-file-paths``), so a
    ``path`` argument returns ``unsupported``; REST never enables file paths.
  * ``inmemory`` cases never reach this adapter: :func:`conformance_common.run_case`
    skips every out-of-process transport for an in-memory backend (lint rule 10).
  * every server is spawned with :func:`conformance_common.spawn` (list argv,
    never a shell) under :func:`conformance_common.conformance_env` and reaped
    with :func:`conformance_common.terminate_and_reap`; every HTTP call is
    timeout-bounded, targets only ``127.0.0.1`` and caps its response read.

The module is Python standard library only and runs under ``python3 -I``; the
sibling imports and the status table are resolved from ``__file__``.
"""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402
import conformance_rest as cr  # noqa: E402


def _load_sibling(stem: str, module_name: str) -> Any:
    """Import a sibling shim whose filename stem is not a valid identifier.

    The generic REST shim lives in ``13_rest_generic_corpus.py``; a leading digit
    makes a plain ``import`` impossible, so it is loaded by path from this file's
    directory (resolved from ``__file__`` for ``python3 -I``). Its
    ``if __name__ == "__main__"`` guard does not fire under a synthetic name.

    :param stem: The sibling filename stem (without ``.py``).
    :param module_name: A valid module name to register the loaded module under.
    :return: The imported module object.
    :raises ImportError: If the sibling file cannot be located or loaded.
    """
    path = _THIS.parent / f"{stem}.py"
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load sibling shim {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


#: The embedded generic REST adapter (subtask 08) reused for the one-server-per-
#: role lifecycle and the generic fallback of steps without a ``rest_resource``.
RestGenericAdapter = _load_sibling(
    "13_rest_generic_corpus", "conformance_rest_generic_corpus"
).RestGenericAdapter

#: The data-driven legacy status / body-code table (subtask 09), resolved from
#: ``__file__`` so it loads identically under ``python3 -I`` and on any OS.
_STATUS_TABLE_PATH = _THIS.parent / "rest_resource_status.json"

#: Max characters of a response body echoed into an assertion message, so a
#: failure stays readable even if a server returns an unexpectedly large body.
_RAW_SNIPPET_CHARS = 500

#: The synthesized top-level envelope id for a resource route (see module doc):
#: the minimal ``srv-<n>`` sentinel N4 accepts when the request carried no id.
#: ``caps.id = False`` means it is masked to ``<id>`` and never compared.
_SYNTHETIC_ENVELOPE_ID = "srv-0"


def _snippet(text: str) -> str:
    """Return *text* truncated to :data:`_RAW_SNIPPET_CHARS` with an elision tag."""
    if len(text) <= _RAW_SNIPPET_CHARS:
        return text
    return f"{text[:_RAW_SNIPPET_CHARS]}... (+{len(text) - _RAW_SNIPPET_CHARS} chars)"


def _load_status_table() -> Dict[str, Any]:
    """Load and shallow-validate :data:`rest_resource_status.json`.

    :return: The parsed table with ``error_status``, ``legacy_code_to_envelope``
        and ``commands`` sections.
    :raises AssertionError: If a required section is missing or malformed, so a
        corrupted table fails loudly rather than silently passing every status.
    """
    table = json.loads(_STATUS_TABLE_PATH.read_text(encoding="utf-8"))
    for section in ("error_status", "legacy_code_to_envelope", "commands"):
        assert isinstance(table.get(section), dict), (
            f"rest_resource_status.json: section {section!r} missing or not an "
            "object")
    return table


class RestResourceAdapter(cc.Adapter):
    """Replay a corpus case through the REST resource routes (and generic fallback).

    One adapter is built per case. It embeds a :class:`RestGenericAdapter` for the
    one-server-per-role lifecycle and for the generic fallback of steps without a
    ``rest_resource`` block; its own :class:`conformance_rest.RestClient` shares
    that adapter's live ``role -> port`` map so a resource call and a generic call
    for the same role hit the same server (one store per case).

    :ivar transport: Always ``"rest_resource"`` (selects
        ``caps = (id=False, raw=False, options=False)`` and the HTTP status
        channel in :data:`conformance_common.TRANSPORT_CAPS`).
    """

    transport = "rest_resource"

    def __init__(self) -> None:
        """Create an idle adapter; no server is spawned until the first step."""
        self._generic = RestGenericAdapter()
        # Share the generic adapter's live port map so resource calls and generic
        # fallback calls for one role target the same per-case server.
        self._client = cr.RestClient(self._generic._ports)
        self._table = _load_status_table()

    def begin_case(self, case: Dict[str, Any], ctx: cc.CaseContext) -> Optional[str]:
        """Materialise the backend and record the default role (delegated).

        :param case: The case about to run.
        :param ctx: The per-case context.
        :return: Whatever the embedded generic adapter returns (always ``None``;
            inmemory cases are skipped upstream).
        """
        return self._generic.begin_case(case, ctx)

    def _expected_table_status(
        self, command: Optional[str], expect: Dict[str, Any]
    ) -> Tuple[Optional[int], Optional[str]]:
        """Return the legacy status the table pins for *command* + *expect*.

        :param command: The step's ``request.command`` (table key), or ``None``.
        :param expect: The step's ``expect`` block (its ``ok`` / ``error.code``
            selects success vs which error status).
        :return: ``(status, finding)`` - *status* is the table status, or
            ``None`` with a *finding* when the command/code is not in the table.
        """
        if command is None:
            return None, "rest_resource step has no request.command to key the status table"
        if expect.get("ok") is False:
            error = expect.get("error")
            code = error.get("code") if isinstance(error, dict) else None
            status = self._table["error_status"].get(code)
            if status is None:
                return None, (
                    f"error code {code!r} is not in rest_resource_status.json "
                    "error_status")
            return status, None
        entry = self._table["commands"].get(command)
        if not isinstance(entry, dict) or "success" not in entry:
            return None, (
                f"command {command!r} has no success status in "
                "rest_resource_status.json commands")
        return entry["success"], None

    def _wrap(self, status: int, body: Any, raw: str) -> Dict[str, Any]:
        """Synthesize a command envelope from a legacy resource-route response.

        Ok-ness is decided by whether the legacy body carries ``error.code``, not
        by the status band, so an ``ok: true`` HTTP-200 error (``job_cancelled``)
        is wrapped as a failure envelope.

        :param status: The HTTP status the resource route returned.
        :param body: The parsed JSON body (``None`` if the body was not JSON).
        :param raw: The raw body text, for diagnostics.
        :return: A synthetic ``{"v","ok","id", "result"|"error"}`` envelope -
            ``ok: false`` with the translated ``error.code`` when the body carries
            one (any status, including a 200 ``job_cancelled``), else ``ok: true``
            with the body as ``result``.
        :raises AssertionError: If a success (no ``error.code``) body is not a 2xx
            JSON object - a non-2xx body with no recoverable ``error.code`` is a
            real divergence, not a silent pass.
        """
        envelope: Dict[str, Any] = {"v": 1, "id": _SYNTHETIC_ENVELOPE_ID}
        # Success vs failure is decided by whether the legacy body carries an
        # error code, NOT by the 2xx band: ``job_cancelled`` is HTTP 200 with an
        # error body (error_codes.cpp; HTTP_STATUS["job_cancelled"] == 200), so a
        # status-band test would mis-wrap a cancelled job as a success result.
        legacy_code = None
        if isinstance(body, dict) and isinstance(body.get("error"), dict):
            legacy_code = body["error"].get("code")
        if isinstance(legacy_code, str):
            envelope_code = self._table["legacy_code_to_envelope"].get(
                legacy_code, legacy_code)
            envelope["ok"] = False
            envelope["error"] = {"code": envelope_code}
            return envelope
        # No error code: a success body. A 2xx is the result payload; a non-2xx
        # with no recoverable error.code is a real divergence, not a silent pass.
        assert 200 <= status < 300, (
            f"rest_resource non-2xx status {status} carries no error.code "
            f"(body {_snippet(raw)!r})")
        assert isinstance(body, dict), (
            f"rest_resource 2xx body is not a JSON object (status {status}): "
            f"{_snippet(raw)!r}")
        envelope["ok"] = True
        envelope["result"] = body
        return envelope

    def send(
        self, step: Dict[str, Any], request_text: str, ctx: cc.CaseContext
    ) -> Dict[str, Any]:
        """Replay one step: the resource route when it carries ``rest_resource``.

        :param step: The substituted step.
        :param request_text: The substituted request text (passed to the generic
            fallback for a step without a ``rest_resource`` block).
        :param ctx: The per-case context.
        :return: ``{"envelope": <dict>, "http_status": int, "carries_id": bool}``
            for :func:`conformance_common.run_case` to assert. ``carries_id`` is
            decided per step by the path taken: a generic-fallback step is
            ``True`` (the ``POST /api/v1/commands`` endpoint echoes the request
            id, exactly like the ``rest_generic`` transport), a resource-route
            step is ``False`` (the synthesized envelope carries only the minimal
            ``srv-0`` sentinel, which N4 accepts as a server-minted id).
        :raises AssertionError: On a status-table mismatch, a server/corpus status
            disagreement, or an unrecoverable legacy error code.
        """
        rr = step.get("rest_resource")
        if rr is None:
            # Setup / non-resource step: run it through the generic endpoint on
            # this adapter (same per-role server), unchanged. That endpoint echoes
            # the request id, so this step is judged with the echo check on
            # (carries_id=True) even though the transport default is id=False.
            out = dict(self._generic.send(step, request_text, ctx))
            out["carries_id"] = True
            return out

        role = step.get("caller") or self._generic._case_caller
        self._generic._server_for(role)

        expect = step.get("expect") or {}
        command = None
        request = step.get("request")
        if isinstance(request, dict):
            command = request.get("command")
        declared = rr.get("status")

        # Guard 1: the corpus step's declared status must equal the data-driven
        # legacy table (a corpus author cannot silently weaken a status, C3).
        table_status, finding = self._expected_table_status(command, expect)
        assert finding is None, finding
        assert declared == table_status, (
            f"rest_resource.status {declared!r} for {command!r} disagrees with "
            f"rest_resource_status.json ({table_status!r}); legacy statuses win "
            "(plan.md C3)")

        status, body, raw = self._client.call(
            role, rr["method"], rr["path"], rr.get("query"), rr.get("body"))

        # Guard 2: the live resource route must return the declared legacy status.
        assert status == declared, (
            f"{rr['method']} {rr['path']} returned HTTP {status}, corpus "
            f"declared {declared} (body {_snippet(raw)!r})")

        envelope = self._wrap(status, body, raw)
        # Report the shared catalog status to run_case (200 for success /
        # HTTP_STATUS[code] for errors): the http_status channel is shared across
        # REST transports, and the legacy-exact status is pinned by the guards
        # above. For a resource create (really 201) the generic endpoint returns
        # 200, so this is the only value run_case can assert uniformly.
        # carries_id=False: the synthesized envelope carries only the srv-0
        # sentinel (a resource route has no envelope id to echo).
        return {"envelope": envelope,
                "http_status": cc.expected_http_status(step),
                "carries_id": False}

    def end_case(self) -> None:
        """Reap every server started for this case (delegated, always runs)."""
        self._generic.end_case()


# ---------------------------------------------------------------------------
# Route-table sanity + status-table drift tests (spec 09 "Tests" row).
#
# These drive one real admin server directly (resource routes run as Admin by
# design) to prove: the legacy status table does not drift from the live server
# or the shared catalog table; wrong methods and trailing-slash / URL-encoded
# slugs keep today's behavior. They complement the corpus replay above, which is
# currently only the schema self-test (the link/jobs corpus lands in 11-14).
# ---------------------------------------------------------------------------


class ResourceRouteSanity(unittest.TestCase):
    """Live route-table and status-table characterization for the resource routes.

    One ``--command-api-role admin`` server is spawned for the whole class over a
    throwaway sqlite store and reaped in :meth:`tearDownClass`. Every request goes
    through :class:`conformance_rest.RestClient` (loopback, timeout-bounded,
    capped read), spawned via :class:`conformance_rest.ServerProcess` under
    :func:`conformance_common.conformance_env`.
    """

    _server: Optional[cr.ServerProcess] = None
    _client: Optional[cr.RestClient] = None
    _tmp: Optional[tempfile.TemporaryDirectory] = None
    _table: Dict[str, Any] = {}

    @classmethod
    def setUpClass(cls) -> None:
        """Spawn the shared admin server over a fresh sqlite store and seed a link."""
        cls._table = _load_status_table()
        binary = str(Path(cc.find_binary()).resolve())
        cls._tmp = tempfile.TemporaryDirectory(prefix="conf_rr_sanity_")
        tmp = Path(cls._tmp.name)
        db = (tmp / "case.sqlite3").as_posix()
        storage_yaml = tmp / "storage.yaml"
        storage_yaml.write_text(
            f"metadata:\n  backend: sqlite\n  sqlite_path: {db}\n",
            encoding="utf-8")
        ports: Dict[str, int] = {}
        cls._client = cr.RestClient(ports)
        cls._server = cr.ServerProcess(
            binary, ["--storage-config", str(storage_yaml)], "admin",
            tmp / "server_admin.log", cwd=str(tmp), env=cc.conformance_env())
        cls._server.start()
        ports["admin"] = cls._server.port
        # Seed a known-good link so the resource read/wrong-method rows have a
        # real target; creation itself is asserted in the status-table test.
        status, _, raw = cls._client.call(
            "admin", "POST", "/api/v1/links", None,
            {"url": "https://example.com/docs", "slug": "docs"})
        assert status == 201, f"seed create returned {status}: {_snippet(raw)!r}"

    @classmethod
    def tearDownClass(cls) -> None:
        """Reap the server and remove the throwaway store (always, both paths)."""
        if cls._server is not None:
            cls._server.stop()
            cls._server = None
        if cls._tmp is not None:
            cls._tmp.cleanup()
            cls._tmp = None

    def _call(
        self, method: str, path: str, query: Any = None, body: Any = None
    ) -> Tuple[int, Any, str]:
        """Issue one admin resource-route request (test helper)."""
        assert self._client is not None
        return self._client.call("admin", method, path, query, body)

    @staticmethod
    def _error_code(body: Any) -> Optional[str]:
        """Return the legacy ``error.code`` of a parsed error body, or ``None``."""
        if isinstance(body, dict) and isinstance(body.get("error"), dict):
            return body["error"].get("code")
        return None

    def test_error_status_table_equals_shared_catalog(self) -> None:
        """error_status must equal the shared catalog HTTP status table (no drift).

        Type: unit (data equality).
        Scenario: every envelope code in ``error_status`` maps to the same HTTP
          status as ``conformance_common.HTTP_STATUS`` (the exact catalog copy
          subtask 16 pins to the characterization tests).
        Boundaries: proves the resource-route status table cannot drift from the
          single catalog error table it was copied from.
        On failure first check: did error_codes.cpp legacy_rest_status or the
          catalog error table change without updating rest_resource_status.json?
        """
        for code, status in self._table["error_status"].items():
            self.assertIn(code, cc.HTTP_STATUS, f"unknown error code {code!r}")
            self.assertEqual(
                status, cc.HTTP_STATUS[code],
                f"error_status[{code!r}]={status} != catalog {cc.HTTP_STATUS[code]}")

    def test_translation_map_targets_known_envelope_codes(self) -> None:
        """Every legacy_code_to_envelope target must be a known envelope code.

        Type: unit (data integrity).
        Scenario: each value of ``legacy_code_to_envelope`` is present in
          ``error_status`` (hence a real envelope error code), and the three
          legacy promotions error_codes.cpp defines are mapped.
        Boundaries: proves the wrapper can never translate a legacy body code into
          a code the matcher would reject as unknown.
        On failure first check: did a legacy_rest_code in error_codes.cpp change?
        """
        for legacy, envelope in self._table["legacy_code_to_envelope"].items():
            self.assertIn(
                envelope, self._table["error_status"],
                f"legacy_code_to_envelope[{legacy!r}] -> unknown {envelope!r}")
        for legacy, envelope in (
            ("invalid_request", "invalid_field"),
            ("repository", "storage_failure"),
            ("internal_error", "internal"),
        ):
            self.assertEqual(
                self._table["legacy_code_to_envelope"].get(legacy), envelope,
                f"missing legacy promotion {legacy!r} -> {envelope!r}")

    def test_live_status_table_has_no_drift(self) -> None:
        """Live resource-route statuses must equal the table (success and errors).

        Type: integration (live characterization).
        Scenario: against the real server, link.create success is 201,
          invalid_url is 400/``invalid_url``, a reserved slug is 409/
          ``reserved_slug``, and an unknown slug read is 404/``not_found`` - each
          equal to the status rest_resource_status.json claims.
        Boundaries: pins the table to live behavior (= the HTTP characterization
          tests 02/03), so the table cannot silently diverge from the server.
        On failure first check: did statusForAppError/codeForAppError or the
          create handler status change?
        """
        create_ok = self._table["commands"]["link.create"]["success"]
        status, _, raw = self._call(
            "POST", "/api/v1/links", None,
            {"url": "https://example.com/t", "slug": "drift-ok"})
        self.assertEqual(status, create_ok, _snippet(raw))

        status, body, raw = self._call(
            "POST", "/api/v1/links", None, {"url": "not a url"})
        self.assertEqual(status, self._table["error_status"]["invalid_url"],
                         _snippet(raw))
        self.assertEqual(self._error_code(body), "invalid_url", _snippet(raw))

        status, body, raw = self._call(
            "POST", "/api/v1/links", None,
            {"url": "https://example.com/r", "slug": "api"})
        self.assertEqual(status, self._table["error_status"]["reserved_slug"],
                         _snippet(raw))
        self.assertEqual(self._error_code(body), "reserved_slug", _snippet(raw))

        status, body, raw = self._call("GET", "/api/v1/links/ghost-xyz")
        self.assertEqual(status, self._table["error_status"]["not_found"],
                         _snippet(raw))
        self.assertEqual(self._error_code(body), "not_found", _snippet(raw))

    def test_error_body_code_key_is_error_dot_code(self) -> None:
        """The legacy error body carries its code at error.code (verifies spec UNVERIFIED).

        Type: integration (contract).
        Scenario: an invalid create's body is ``{"error":{"code":...}}`` - the
          nested ``error.code`` key the wrapper reads, not a top-level ``code``.
        Boundaries: resolves spec 09's UNVERIFIED note on the body code key by
          inspecting the live handler output (makeApiErrorResponse).
        On failure first check: did makeApiErrorResponse change the error body shape?
        """
        _, body, raw = self._call("POST", "/api/v1/links", None, {"url": "nope"})
        self.assertIsInstance(body, dict, _snippet(raw))
        self.assertIn("error", body, _snippet(raw))
        self.assertIsInstance(body["error"], dict, _snippet(raw))
        self.assertIn("code", body["error"], _snippet(raw))
        self.assertNotIn("code", body, "legacy error code is nested under error, not top-level")

    def test_field_level_code_maps_to_invalid_field(self) -> None:
        """A per-field REST decoder code translates to the envelope invalid_field.

        Type: integration (contract / parity).
        Scenario: a PATCH with a non-boolean ``enabled`` returns the legacy
          field-level body code ``invalid_enabled`` with status 400; the wrapper's
          ``legacy_code_to_envelope`` map translates it to ``invalid_field`` - the
          same failure class the generic/registry transport reports - so a corpus
          case written once as ``error.code: invalid_field`` matches on both REST
          transports (subtask 09 implementation step 3).
        Boundaries: the field-level 400 codes the body decoder emits
          (``src/http/handlers/link_handlers.cpp``) are reconciled test-side, never
          by reformatting the legacy body.
        On failure first check: did the PATCH body decoder change its field code,
          or is a field-level code missing from legacy_code_to_envelope?
        """
        status, body, raw = self._call(
            "PATCH", "/api/v1/links/docs", None, {"enabled": "false"})
        self.assertEqual(status, self._table["error_status"]["invalid_field"],
                         _snippet(raw))
        legacy = self._error_code(body)
        self.assertEqual(legacy, "invalid_enabled", _snippet(raw))
        self.assertEqual(
            self._table["legacy_code_to_envelope"].get(legacy), "invalid_field",
            f"field-level code {legacy!r} must map to invalid_field")

    def test_wrong_method_returns_invalid_method_not_405(self) -> None:
        """Wrong methods keep today's 400 invalid_method shape (no 405 / no Allow).

        Type: integration (route-table sanity).
        Scenario: a PUT on a multi-method path and a GET on a POST-only path both
          return 400 ``invalid_method``; the single-method case names the allowed
          method in the message ("Only POST is supported"), the multi-method case
          says "Unsupported operation". No ``Allow`` header and no 405 are emitted.
        Boundaries: the router (``src/http/router.cpp``) answers a method mismatch
          with 400 ``invalid_method`` and conveys the allowed method in the
          message, not via a 405 + ``Allow`` header. This deliberately diverges
          from spec 09 step 4's "405 + Allow" wording, which assumed standard REST
          405 semantics this server does not implement.
        On failure first check: did router.cpp start emitting 405/Allow, or change
          the invalid_method message?
        """
        status, body, raw = self._call("PUT", "/api/v1/links/docs")
        self.assertEqual(status, 400, _snippet(raw))
        self.assertEqual(self._error_code(body), "invalid_method", _snippet(raw))
        self.assertEqual(body["error"].get("message"), "Unsupported operation",
                         _snippet(raw))

        status, body, raw = self._call("GET", "/api/v1/links/docs/enable")
        self.assertEqual(status, 400, _snippet(raw))
        self.assertEqual(self._error_code(body), "invalid_method", _snippet(raw))
        self.assertEqual(body["error"].get("message"), "Only POST is supported",
                         _snippet(raw))

    def test_trailing_slash_is_not_found(self) -> None:
        """A trailing slash on a resource path keeps today's 404 (route not found).

        Type: integration (route-table sanity).
        Scenario: ``GET /api/v1/links/docs/`` does not match the slug route and
          falls through to the router's 404 ``not_found`` "Route not found",
          unchanged from today.
        Boundaries: proves trailing-slash normalization is not silently added.
        On failure first check: did matchPath start trimming a trailing slash?
        """
        status, body, raw = self._call("GET", "/api/v1/links/docs/")
        self.assertEqual(status, 404, _snippet(raw))
        self.assertEqual(self._error_code(body), "not_found", _snippet(raw))
        self.assertEqual(body["error"].get("message"), "Route not found",
                         _snippet(raw))

    def test_url_encoded_slug_is_not_decoded(self) -> None:
        """A percent-encoded slug is matched verbatim, not decoded (unchanged).

        Type: integration (route-table sanity).
        Scenario: ``GET /api/v1/links/%64ocs`` (``%64`` == ``d``) does not resolve
          to the seeded ``docs`` link; the slug is looked up verbatim and returns
          404 ``not_found`` "Link not found" (the link-level not-found, distinct
          from the route-not-found above).
        Boundaries: proves the slug route does not percent-decode before lookup.
        On failure first check: did the slug matcher start percent-decoding?
        """
        status, body, raw = self._call("GET", "/api/v1/links/%64ocs")
        self.assertEqual(status, 404, _snippet(raw))
        self.assertEqual(self._error_code(body), "not_found", _snippet(raw))
        self.assertEqual(body["error"].get("message"), "Link not found",
                         _snippet(raw))


class EnvelopeSynthesisUnit(unittest.TestCase):
    """Server-free unit tests of :meth:`RestResourceAdapter._wrap`.

    These need no HTTP server: they call the pure synthesis function on crafted
    ``(status, body)`` pairs. The adapter is constructed once (resolving the
    binary path and loading the status table, but spawning nothing).
    """

    _adapter: Optional[RestResourceAdapter] = None

    @classmethod
    def setUpClass(cls) -> None:
        """Build one adapter (no server) to exercise its _wrap in isolation."""
        cls._adapter = RestResourceAdapter()

    def test_job_cancelled_200_with_error_body_is_not_ok(self) -> None:
        """An HTTP-200 job_cancelled error body wraps as ok=false, not a result.

        Type: unit (envelope synthesis).
        Scenario: ``job_cancelled`` is HTTP 200 with an error body
          (HTTP_STATUS["job_cancelled"] == 200); _wrap must key ok-ness on the
          presence of error.code, not the 2xx band, so it yields
          ``ok=false, error.code=job_cancelled`` and never ``ok=true`` with a
          result containing an error object.
        Boundaries: the single security-review case (07.0/09) that proves the
          status-band shortcut is not used.
        On failure first check: did _wrap revert to deciding ok-ness by status?
        """
        assert self._adapter is not None
        env = self._adapter._wrap(200, {"error": {"code": "job_cancelled"}}, "{}")
        self.assertIs(env["ok"], False)
        self.assertEqual(env["error"], {"code": "job_cancelled"})
        self.assertNotIn("result", env)

    def test_2xx_body_without_error_is_ok_result(self) -> None:
        """A 2xx body with no error.code wraps as ok=true with the body as result.

        Type: unit (envelope synthesis).
        Scenario: a 201 create view (no error key) becomes
          ``ok=true, result=<body>``.
        On failure first check: did _wrap stop treating a plain 2xx body as a result?
        """
        assert self._adapter is not None
        view = {"slug": "docs", "url": "https://example.com/docs"}
        env = self._adapter._wrap(201, view, "{}")
        self.assertIs(env["ok"], True)
        self.assertEqual(env["result"], view)
        self.assertNotIn("error", env)

    def test_4xx_error_code_is_translated(self) -> None:
        """A 4xx legacy code is translated through legacy_code_to_envelope.

        Type: unit (envelope synthesis).
        Scenario: a 400 ``invalid_request`` body wraps as
          ``ok=false, error.code=invalid_field`` (the envelope vocabulary).
        On failure first check: did the translation map or _wrap lookup change?
        """
        assert self._adapter is not None
        env = self._adapter._wrap(400, {"error": {"code": "invalid_request"}}, "{}")
        self.assertIs(env["ok"], False)
        self.assertEqual(env["error"], {"code": "invalid_field"})

    def test_non_2xx_without_error_code_raises(self) -> None:
        """A non-2xx body with no recoverable error.code is a divergence, not a pass.

        Type: unit (envelope synthesis, negative).
        Scenario: a 500 body that is not an error object fails loudly rather than
          silently wrapping as a success.
        On failure first check: did _wrap start swallowing unrecoverable bodies?
        """
        assert self._adapter is not None
        with self.assertRaises(AssertionError):
            self._adapter._wrap(500, {"unexpected": True}, "{}")


def _main() -> int:
    """Run the corpus replay and the route-table sanity tests; write the summary.

    :return: ``0`` when every test passed, else ``1`` (so CTest marks the shim).
    """
    collector = cc._SummaryCollector("rest_resource")
    corpus = cc.build_suite(lambda: RestResourceAdapter(), "rest_resource", collector)
    loader = unittest.TestLoader()
    sanity = loader.loadTestsFromTestCase(ResourceRouteSanity)
    wrap_unit = loader.loadTestsFromTestCase(EnvelopeSynthesisUnit)
    suite = unittest.TestSuite([corpus, sanity, wrap_unit])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    collector.write()
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(_main())

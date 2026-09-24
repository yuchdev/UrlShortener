# 03 - Wire Fault States into the Shorten UI

**Parent task:** 02.0 Fault Modes, Validation, and Public Create Rate Limiting
**State:** ⬜ Not started
**Depends on:** 02
**Blocks:** 04

## Objective

Render each `FaultState` from subtask 02 in the shorten flow, choosing the
right presentation per state: inline field errors, toasts, or a full-surface
error, and wiring the clipboard fallback.

## Files to add / edit

```text
web/app/src/components/ui/Toast.tsx           # add (if not already present)
web/app/src/components/ui/Toaster.tsx         # add
web/app/src/components/shorten/ShortenForm.tsx        # edit
web/app/src/components/shorten/ShortenResultCard.tsx  # edit
web/app/src/hooks/useCopyToClipboard.ts               # edit (clipboard fault)
web/app/src/pages/ShortenPage.tsx                     # edit (page-level errors)
```

## Requirements

1. Consume the `FaultState` presentation hint to route rendering:
   - `field` states (`invalid_url`, `slug_conflict`, `target_too_long`) →
     inline error under the relevant form field. `invalid_url` attaches to the
     URL field with the single shared, non-leaky message (see
     [plan.md SC3](/docs/roadmap/0004-web-ui/plan.md)); `slug_conflict`
     attaches to the slug field.
   - `toast` states (`rate_limited`, `network_error`, `timeout`,
     `clipboard_unavailable`) → transient toast; `rate_limited` shows retry
     guidance derived from `retryAfter` when present.
   - `page` states (`server_error`, `unknown`) → a full `ErrorState` surface
     with the safe message plus a copyable `request_id` ("include this when
     reporting").
2. `body_too_large` (`413`) surfaces as a field or toast (as chosen in the
   model's hint) with guidance to shorten the URL.
3. The clipboard-copy fallback: when the Clipboard API is unavailable or the
   copy throws, `useCopyToClipboard` yields `clipboard_unavailable`, and the
   result card falls back to a pre-selected, manually-copyable text field plus
   a toast explaining the fallback.
4. Empty and loading states remain correct: the form's empty state, the
   in-flight loading state (from task 01.0), and a clean reset after a new
   submission.
5. Error copy is rendered verbatim from the `FaultState` message — components
   do not re-derive or embellish messages, keeping SC3's opacity guarantee in
   one place.

## Constraints

- No component may branch on raw HTTP status or backend `code` directly — it
  consumes `FaultState` only, so the mapping stays single-sourced in subtask
  02.
- Never render internal error detail (DSNs, IPs, stack traces); only the safe
  `message` and the opaque `request_id`.
- Toasts must be accessible (role/aria-live) and auto-dismiss without trapping
  focus.

## Success criteria

- [ ] Each `FaultState` renders in its intended surface (field / toast /
      page).
- [ ] Malformed and blocked-target URLs both show the same inline URL-field
      message.
- [ ] `409` shows an actionable slug-field error; `429` shows a retry toast;
      `5xx`/`unknown` show a full error surface with a copyable `request_id`.
- [ ] Clipboard failure falls back to selectable text plus an explanatory
      toast.
- [ ] Components branch only on `FaultState`, never on raw status/code.

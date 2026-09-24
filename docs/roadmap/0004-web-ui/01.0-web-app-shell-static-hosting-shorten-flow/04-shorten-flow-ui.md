# 04 - Shorten Flow UI

**Parent task:** 01.0 Web App Shell, Static Hosting, and Shorten Flow
**State:** ⬜ Not started
**Depends on:** 03
**Blocks:** 05

## Objective

Build the actual shorten flow on `/app`: a form to submit a long URL (with an
optional custom slug), a loading state, and a result card showing the short
URL with a copy-to-clipboard button.

## Files to add

```text
web/app/src/components/shorten/ShortenForm.tsx
web/app/src/components/shorten/ShortenResultCard.tsx
web/app/src/components/ui/CopyButton.tsx
web/app/src/model/shortenForm.ts
web/app/src/hooks/useCopyToClipboard.ts
```

Plus edits (existing files):

```text
web/app/src/pages/ShortenPage.tsx   # replace placeholder with the real flow
```

## Requirements

1. `shortenForm.ts` defines the React Hook Form + Zod form schema: a required
   long-URL field and an optional custom-slug field. Client-side validation
   catches obviously-invalid input (empty URL, non-`http(s)` scheme, slug
   with illegal characters/length) for fast feedback — but must not claim to
   know *why* the server would reject a syntactically plausible URL (see
   [plan.md SC3](/docs/roadmap/0004-web-ui/plan.md)).
2. `ShortenForm` uses React Hook Form with the Zod resolver, renders inline
   field validation, disables submit while the `useCreateLink` mutation is in
   flight, and shows a `Spinner`/loading state.
3. On success, render `ShortenResultCard` with the returned `short_url`, the
   original `url`, and a `CopyButton`. The card should make the short URL the
   prominent, selectable element.
4. `CopyButton` + `useCopyToClipboard` copy the short URL to the clipboard and
   show a transient "Copied" confirmation. A basic fallback (select-the-text)
   for environments without the Clipboard API is acceptable here; the full
   clipboard-failure fault state is refined in task 02.0.
5. On error, show a **minimal** single error surface for now (e.g. an
   `ErrorState` with the envelope `message`). The complete, per-code
   fault-state model is task 02.0 — this subtask should not try to enumerate
   every failure, only fail visibly and safely.
6. Submitting again (new URL) resets the result cleanly.

## Constraints

- Public page: no auth, no stored history, no "your links" list. After a
  successful shorten, the result is shown but not persisted anywhere
  client-side (see [plan.md SC6](/docs/roadmap/0004-web-ui/plan.md)).
- Use the internal UI primitives from subtask 01; do not pull in a
  third-party component library directly.
- Keep the exhaustive fault handling out of this subtask (task 02.0 owns it).

## Success criteria

- [ ] `/app` renders a form with a long-URL input and an optional custom-slug
      input.
- [ ] Client-side Zod validation blocks empty / non-http(s) input with inline
      messages before any request is sent.
- [ ] Submitting a valid URL shows a loading state, then a result card with
      the short URL and a working copy button.
- [ ] The copy button copies the short URL and shows a transient confirmation.
- [ ] A backend error is shown visibly and safely (minimal surface; full
      model in task 02.0).
- [ ] Nothing about the result is persisted client-side.

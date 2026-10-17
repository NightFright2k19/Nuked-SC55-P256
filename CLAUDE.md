# Binding project rules for changes and documentation

These rules apply permanently to all tasks and changes in this repository.
Apply them proactively, without needing to be reminded.

## 1. Documentation on every change

- For every change, check whether `README.md` and/or the Autobuild Release Notes
  need to be updated. The release notes are the `body` of the "Create GitHub
  release" step in `.github/workflows/build.yml`.
- Required documentation changes belong in the same change (the same PR).
- Never skip a documentation update without checking first. State the result of
  the check (updated, or not needed and why) in the PR.

## 2. Changes to the GUI, the SETUP menu or the patcher

Applies to the plugin panels and user interface (`src/`, panel scripts under
`tools/`), the SETUP menu, and the patcher (`patcher.py`, `patcher_i18n.py`).

- Before implementing, create preview screenshots and present them for approval.
- Do not start the implementation until the screenshots have been explicitly
  approved.
- After implementing, check whether existing screenshots in the README
  (`docs/images/` and embedded images) are outdated, and update them if needed.

## 3. Binding nature

- If it is unclear whether something has been approved, ask before implementing.

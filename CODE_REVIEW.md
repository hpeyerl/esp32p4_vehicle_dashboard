# evj55-dashboard - code review findings

Human review by Herb, 2026-09-01. Actionable items; check off as fixed.
General style rules extracted to CLAUDE.md; the sweeps below apply them to existing code.

## Correctness / design
- [ ] **`src/bms_http.c:29` - drop the default hostname.** Require the env var to be set;
  if it is not, error out. Alternative: move it into `secrets.h`. No baked-in default.
- [ ] **Stale-value handling on long CAN failure.** The glitch gates don't account for a
  LONG period of CAN failure - a reading will display forever as a stale value. Add a
  staleness timeout that blanks or flags the value as stale after N seconds without an update.

## Structure
- [ ] **`src/dashboard_ui.cpp` is too large - split it up.** Likely per-screen (one file per
  screen/view). Unwieldy as a single file.

## Style cleanup (see CLAUDE.md for the rules)
- [ ] **`src/dashboard_ui.cpp:318` - comment is a "conversation with yourself."** Reduce it
  to the conclusion/answer, not the reasoning back-and-forth.
- [ ] **Unicode -> ASCII** sweep in comments (and code).
- [ ] **Variable-declaration hygiene:** move scattered locals to top of function/scope;
  remove duplicate declarations (e.g. repeated `int err;`).
- [ ] **Reformat to BSD KNF.**

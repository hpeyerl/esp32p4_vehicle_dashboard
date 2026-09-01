# evj55-dashboard - coding conventions

Follow these when writing or editing code in this repo (Herb's stated preferences,
from his 2026-09-01 review). See CODE_REVIEW.md for the outstanding cleanup pass.

- **Style: BSD KNF** (Kernel Normal Form; FreeBSD `style(9)`). Match it for indentation,
  brace placement, spacing, and function layout.
- **Declare local variables at the top of the function, or at least the top of each scope
  block** (the body of a for/while/if). Do NOT scatter ("aerosolize") declarations through
  the code.
- **No duplicate declarations within a function** - declare a thing once (e.g. one `int err;`,
  not a fresh one in every block).
- **ASCII only, in comments and code** - no Unicode (no em-dashes, arrows, smart quotes,
  box-drawing). Terminals garble them. (Matches Herb's global rule.)
- **Comments state the conclusion, not the debate** - don't leave a back-and-forth argument
  with yourself in a comment; write the answer.
- **Keep files focused** - prefer splitting large UI files per-screen over one giant file.

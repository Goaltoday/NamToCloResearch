# P/K refinement v1.9.7 — Candidate Audition

This version keeps the v1.9.6 research search, metrics and final safety gate unchanged.

New behavior:

- The unconstrained best search result is ALWAYS exported as:
  - `_Ampero_2048_BEST.clo`
  - `_GP200_1024_BEST.clo`
- `_BEST` is intended only for A/B listening and measurement against the NAM.
- `_REFINE` remains protected by the conservative final safety gate. If the best candidate is rejected, `_REFINE` remains equivalent to the official/original P/K result.
- The completion dialog reports absolute original -> BEST values for NMSE, MR-STFT and envelope error in addition to percentage diagnostics.
- Application title/header/footer derive from `ntc::kVersion` and should all display `1.9.7`.

Suggested test:

1. Convert the same Fender or 5150 case with refinement enabled.
2. Compare the original GP200 CLO against `_BEST` and `_REFINE` at matched output level.
3. Use `_BEST` to judge whether the current combined research loss correlates with perceived similarity to the NAM.

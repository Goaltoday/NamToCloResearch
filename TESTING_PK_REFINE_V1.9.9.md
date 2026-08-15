# P/K refinement v1.9.9 - constrained search

This version changes the optimiser, not the CLO player architecture.

The complete render is still evaluated (normally 50 s stimulus + 20 s tail),
and output scale is fitted once from the original CLO and then frozen.

For P/K optimisation, candidate points are now searched only inside a feasible
region:

- global NMSE must not worsen vs. the original CLO;
- low/mid/high excitation NMSE may regress by at most 0.50% each;
- MR-STFT and multi-scale envelope may regress by at most 5% as secondary guards.

Within that feasible region, the existing composite P/K loss chooses the best
candidate. The coarse Halton exploration is increased from 24 to 64 points,
followed by constrained local refinement.

`_BEST` now means the best *feasible* audition candidate. `_REFINE` additionally
requires the final composite improvement threshold, otherwise the official CLO
is retained.

First regression test: repeat the Fender Clean case that previously produced
an audibly over-distorted unconstrained `_BEST` candidate.

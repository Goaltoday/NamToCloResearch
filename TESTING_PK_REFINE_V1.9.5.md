# P/K refinement v1.9.5 — Free Research Search + Final Safety Gate

Purpose: avoid the v1.9.4 local trap caused by enforcing all metric guards on every intermediate coordinate step.

## Kept from v1.9.4
- Full render (normally 50 s stimulus + 20 s tail)
- One fixed output scale fitted from the original CLO
- Temporal NMSE
- Multi-resolution STFT (512/2048/8192)
- Multi-scale RMS envelope (256/2048/8192)
- Only Ppos/Pneg/Kpos/Kneg are modified

## New search
1. 24 deterministic Halton candidates in log P/K space (coarse exploration).
2. Local free composite-loss pattern refinement.
3. Intermediate candidates are NOT rejected just because one metric temporarily regresses.
4. The final best candidate is accepted only if it improves composite loss and passes conservative final guards.
5. If rejected, the REFINE CLO is byte-equivalent in DSP P/K to the original.

First regression tests: 5150 Andy Sneap and FNDR BFDRI VB Clean BAL2 CAB using Legacy + Original Preset Audio + Corrective IR OFF.

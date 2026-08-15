# NAM to CLO v1.9.2 — full-render P/K refinement test

Use the same conversion settings as the baseline conversion.

Recommended first regression cases:

- 5150 Andy Sneap Unboosted
- FNDR BFDRI VB Clean BAL2 CAB

Settings:

- Stimulus: Original / Legacy
- Tail: Original Preset Audio
- Corrective IR: OFF
- CLO refinement: ON

v1.9.2 evaluates the complete render (normally 50 s stimulus + 20 s tail).
The output-level calibration is fitted once from the original CLO and is frozen
for every P/K candidate.

Record the three reported improvements:

- Full-render NMSE
- Stimulus NMSE (first 50 s)
- Tail NMSE (remaining audio)

Compare the original and `_REFINE` CLO by ear and/or through CloPlayer. A lower
full-render NMSE should no longer be obtainable merely by increasing saturation
and compensating it with a new output gain for each candidate.

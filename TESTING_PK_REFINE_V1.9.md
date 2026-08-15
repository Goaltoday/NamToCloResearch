# NAM to CLO v1.9 — P/K refinement test

Recommended first reference case:

- NAM: `5150 Andy Sneap Unboosted - jp_is_out_of_tune.nam`
- Stimulus: `Original / Legacy`
- Tail: `Original Preset Audio`
- Corrective IR: OFF
- CLO refinement: ON

Expected outputs:

1. Original `*_Ampero_2048.clo`
2. Original `*_GP200_1024.clo`
3. Experimental `*_Ampero_2048_REFINE.clo`
4. Experimental `*_GP200_1024_REFINE.clo`

The original outputs must remain identical to a v1.8 conversion using the same
runtime and settings. The refined CLO must differ only at P/K (`0x68..0x77`)
before GP-200 compacting; PRE/A/POST/B are intentionally fixed in this first
stage.

Record the NMSE improvement shown by the application. A positive value means
the refined P/K model reproduced HTUSBTools' NAM render more closely on the
analysis windows. A zero/negative opportunity is also a useful result: it means
P/K alone is not the dominant limitation for that NAM and the next experiment
should free FIR A.

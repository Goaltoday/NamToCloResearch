# NAM to CLO v2.6.6 - current implementation

Current production path only.

## Base conversion
- NAM -> HTUSBTools -> Hotone/Ampero CLO with A=128, B=2048.
- A2 SlimmableContainer NAMs use the verified embedded submodel with the highest `max_value` (Full).
- GP-200 output is compacted to B=1024 only at the end.
- Historical manual Corrective IR behavior is unchanged.

## Optional refinement
- Block B only; PRE/A/P-K/POST remain unchanged.
- VST-style Tone Match on the final 20 seconds.
- If no target WAV is selected, HTUSBTools' NAM render is the target.
- If a target WAV is selected, its final 20 seconds are the Tone Match target. It must be readable as mono 44.1 kHz by the current refiner and contain at least 20 seconds.
- The generated correction is applied through the existing Corrective IR path with 0 dB refinement post-gain (the manual Corrective IR keeps its historical -6 dB default).
- Comparison metrics remain internal and are not shown to the user.

## User-visible output files
Without refinement:
1. `<name>_Ampero_2048.clo`
2. `<name>_GP200_1024.clo`

With refinement:
1. `<name>_Ampero_2048.clo`
2. `<name>_GP200_1024.clo`
3. `<name>_GP200_1024_REFINED.clo`

Candidate CLOs and automatic Tone Match IRs are temporary working files and are not exported.

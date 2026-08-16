# NAM to CLO v2.6.7 - current implementation

## Base conversion
- NAM -> HTUSBTools -> Hotone/Ampero CLO with A=128, B=2048.
- A2 SlimmableContainer NAMs use the verified highest-`max_value` embedded model (Full).
- GP-200 output is compacted to B=1024 only at the end.
- Manual Corrective IR behavior is unchanged.

## Optional refinement
- Block B only; PRE/A/P-K/POST remain unchanged.
- If the refinement test WAV field is blank, the original conversion stimulus is used for both sides of the comparison, as in v2.6.6.1.
- If a refinement test WAV is selected, the first 20 seconds are adapted with the existing Recorded Audio adapter and inserted as the tail of a second otherwise-identical 70-second stimulus.
- That exact second stimulus is rendered through the NAM Full path by HTUSBTools (TARGET) and through the original first-pass Ampero B2048 CLO by the offline CLO renderer (SOURCE).
- Tone Match compares the same final 20-second performance on both sides.
- The second CLO produced by HTUSBTools during the NAM render pass is ignored; refinement always modifies the original first-pass CLO.
- The generated correction is applied through the existing Corrective IR path with 0 dB refinement post-gain. Manual Corrective IR keeps -6 dB.
- Comparison metrics remain internal and are not shown.

## User-visible output files
Without refinement:
1. `<name>_Ampero_2048.clo`
2. `<name>_GP200_1024.clo`

With refinement:
1. `<name>_Ampero_2048.clo`
2. `<name>_GP200_1024.clo`
3. `<name>_GP200_1024_REFINED.clo`

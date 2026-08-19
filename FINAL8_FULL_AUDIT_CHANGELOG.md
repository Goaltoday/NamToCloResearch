# FINAL8 FULL EXE AUDIT — changelog

FINAL8 replaces the incremental “candidate fix” approach with a closed static audit of the GP-200.exe NAM→CLO conversion call graph.

## Changes since FINAL6

- Corrected the minimum-phase port to follow the actual equal-length branch; removed a previously introduced reversal that is not executed for the trainer's equal input/output full-spectrum lengths.
- Ported P/K estimation from `0x558c30` with its first-five-seconds boundary, 100-ms extrema arrays, signed dataset order, double regression, float K search and float SSE accumulation.
- Fixed the NAM host sequence to 1024-sample process calls, including the zero-padded final partial block, and fixed the official target scale at `0.31f` rather than allowing a runtime override.
- Reworked the 2048 spectral estimator around the executable's Ooura RDFT wrapper, persistent transform work state per estimator, exact Hamming arithmetic, ×1000 preprocessing, half-hop frames and forced final frame.
- Ported the executable's direct-DFT minimum-phase path and exact trig/lifter behavior instead of using an “equivalent” generic FFT path.
- Replaced the FIR renderer with the executable's 64/128 partitioned engine structure, 65-bin storage, Q15 twiddle table and audited ring/overlap order.
- Closed optimizer update order, including step decay location, two conditioning passes, A/B redistribution, candidate rebuild, loss and rollback state.
- Closed final B folding/refinement, full DFT behavior, separate target/model conditioning, 256-point corrector, direct B convolution, DC removal and energy normalization.
- Extracted and embedded the exact two CRC tables from the executable and matched its state/order/storage convention.
- Rewrote GP-200 compacting from `0x4818f0`: source CRC validation, flag validation, exact 0x88 header copy, 0x1200 payload clamp, B count cap, 0x1288 declared size, CRC recomputation and physical 0x2288 zero tail.
- Retained NAMCore v0.5.4 deliberately for A2/SlimmableContainer compatibility. This is the only intentional core-runtime deviation from the executable.

## Validation policy

CLO references are not inputs to the implementation. They are used only after compilation to compare P/K, A/B coefficients, spectral response, CRC/header bytes and final audio behavior. No `A *= constant`, sample shift, or other golden-fitted workaround has been introduced.

# v2.7.0-NATIVE9

Strict optimizer-fidelity update driven by the NATIVE8/official golden comparison and direct GP-200.exe disassembly.

- Reproduces the two consecutive `0x554f00` conditioning passes on each stepped correction before A/B redistribution.
- Uses float32 factor state for A/B, correction arrays, frequency weighting, step size and loss accumulation, matching the official SSE path.
- Keeps the official float step constant `0.8999999761581421f` and rollback factor `0.5f`.
- Keeps the B residual ratio in double for the explicit `x1e6/(x1e6+FLT_EPSILON)` operation, then stores it back to float.
- Keeps initial factorization and spectral estimator outputs in float32.
- Uses float32 final-tail correction arrays through the FIR256 minimum-phase stage.
- No change to the legacy HTUSBTools tab, Stimulus Profile/Tail architecture, r8brain SRC, container layout or GP-200 compacting.

This version implements the newly confirmed optimizer details but is not labelled byte-identical until validated against a newly generated official/native golden pair.

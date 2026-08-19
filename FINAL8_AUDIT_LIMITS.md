# FINAL8 audit limits / external provenance

The GP-200.exe conversion-domain call graph itself has zero PENDING routines in `FINAL8_EXE_AUDIT.md`. The following boundaries are deliberately documented because static reverse engineering of one binary cannot turn them into source-version provenance claims that the binary does not contain.

1. **NeuralAmpModelerCore** — intentional project deviation. The official executable identifies an older NAMCore generation; this project uses v0.5.4 so A2/SlimmableContainer and modern NAM formats remain supported. The surrounding host sequence is ported from the EXE.
2. **r8brain** — the executable identifies the old `CDSPResampler24` / `CDSPFracInterpolator<24,673>` family and the wrapper call sequence is ported. The binary does not expose a Git tag proving one unique source release. The project pins the matching historical template-era family (`version-3.7`).
3. **CRT elementary math** — the executable calls its compiler/runtime implementations of exp/log/log10/pow/sqrt/sin/cos/floor. FINAL8 reproduces the exact audited argument/result float/double boundaries, but a modern compiler's CRT is not asserted to be binary-identical to the EXE's statically linked runtime implementation.

These are not places where the CLO golden was used to infer behavior. They are external implementation provenance boundaries. Bit-identical validation remains a separate test from static algorithm closure.

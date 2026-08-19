# FINAL8 — GP-200.exe NAM→CLO conversion audit

## Scope and rule of evidence

This audit treats the shipped `GP-200.exe` as the specification for the Valeton NAM→CLO conversion path. Golden `.clo` files are **not** used to derive constants, gains, shifts, filters or formulas; they are only validation material after the implementation has been derived from the executable.

The only intentional algorithmic deviation is the NAM model runtime: this project keeps NeuralAmpModelerCore v0.5.4 so current A2/SlimmableContainer/WaveNet models can be loaded. The GP-200 executable identifies an older NAMCore generation. The host-side sequence around NAM execution (sample-rate conversion, Reset, 1024-sample processing, padding and 0.31f target scale) follows the executable.

Status meanings:

- **LITERAL** — translated from the executable's conversion-domain instructions/data flow, including relevant float/double boundaries and order.
- **BEHAVIOR-PROVEN** — compiler/vector implementation differs, but the dependency order and resulting algorithm used by the executable are reproduced; no conversion rule is inferred from a CLO.
- **EXTERNAL-MATCHED** — the executable resolves to an identifiable external algorithm/class family and the project uses that implementation/configuration family.
- **INTENTIONAL DEVIATION** — explicitly retained project functionality that differs from the executable.
- **CRT PRIMITIVE** — the executable delegates an elementary function to its C/C++ runtime; the project calls the corresponding standard primitive with the audited conversion boundaries.

## Closed conversion call graph

| EXE routine / region | Conversion role | FINAL8 implementation | Status |
|---|---|---|---|
| `NamConvertThread` / `getConvertNormalWav` call path | top-level NAM conversion orchestration | `convertNamToBothIndependent()` | LITERAL |
| NAM render call path (`0x47ecxx` family observed in caller trace) | load model, expected Fs, Reset/process | `prepareFullA2()`, `renderNam()` | INTENTIONAL DEVIATION for NAMCore version; host sequence LITERAL |
| `0x5a70a0` | `CDSPResampler24` float↔double SRC wrapper | `resampleR8Brain24()` | EXTERNAL-MATCHED + literal wrapper |
| loader/preprocess around trainer input | exactly 70 s rendered, then `Fs*70+600` with zero guard | `renderNam()` | LITERAL |
| target scale in NAM path | target output `×0.31f` | fixed `constexpr 0.31f` in `renderNam()` | LITERAL |
| `0x559d80` | mean removal + linear detrend, float32 | `detrend()` | LITERAL |
| latency search in trainer caller | start at `6*int(Fs)`, scan 600, `abs>0.01` | latency alignment helper | LITERAL |
| `0x558c30` | P+/P−/K+/K− estimation | `fitPk()` | LITERAL |
| `0x422650` + `0x422710/0x422990` + `0x422c20/0x422cc0` | 4× polyphase up/down allpass network | `Poly`/oversampling path | BEHAVIOR-PROVEN |
| `0x559490` | asymmetric exponential nonlinearity + oversampling path | model NL render | LITERAL |
| PRE/POST biquad path | trainer biquad recurrence and float/double boundaries | `Biquad::p()` | LITERAL |
| `0x5589d0` | initial 6–21 s and 23–28 s measurements | initial measurement path | LITERAL |
| `0x5580e0` | initial conditioned spectra and A/B factorization | initialization path | LITERAL |
| embedded 50-tap tables | initial sweep conditioning FIR for supported Fs | exact tables in trainer | LITERAL |
| `0x5557c0` | 125 ms spectral ratio estimator | `ratioSpectrumF()` | LITERAL |
| `0x423180` and descendants | 2048-point Ooura RDFT wrapper | `OouraRfft2048Official` + `fft4g.c` | EXTERNAL-MATCHED + literal wrapper |
| `0x4225b0` | incremental float linspace | `linspaceF()` | LITERAL |
| `0x553c60` | slope/intercept interpolation and segment search | `interpolateOfficialF()` | LITERAL |
| `0x555460` | Gaussian conditioning kernel/edges | Gaussian helper | LITERAL |
| `0x554f00` | Hz↔Mel magnitude conditioning sequence | `conditionMagnitude()` | LITERAL |
| `0x553400` | direct-DFT trigonometric table generation | trig-table helper | LITERAL |
| `0x5548f0` | cepstral lifter | lifter helper | LITERAL |
| `0x554af0` | direct-DFT minimum-phase core | `minimumPhaseF()` core | LITERAL |
| `0x554420` | minimum-phase wrapper, floor/truncate/DC/norm | `minimumPhaseF()` wrapper path | LITERAL |
| `0x553aa0`, `0x55b120`, `0x55b2e0` | FIR plan/setup wrappers | `FirPlan` construction/setup | LITERAL |
| `0x55b460` | 64-sample partitioned FIR execution | `FirPlan::process` path | LITERAL |
| `0x55b830`, `0x55b9a0`, `0x55bf10` | FFT128 setup/forward/inverse | exact Q15-table FFT128 helper | LITERAL |
| `0x557380` | iterative A/B optimizer, 3+2+5 schedule, rollback/loss | `optimizePhase()` and caller | LITERAL |
| `0x553f90` | complete-block folding used by final B refinement | final-B folding helper | LITERAL |
| `0x554150` | correction FIR construction + direct B convolution | `convolveTruncate()` + correction path | BEHAVIOR-PROVEN |
| `0x556670` region | 50–70 s final B refinement and energy normalization | final-B refinement path | LITERAL |
| `0x553150/0x553190` | table CRC with 0xff/0xff state | `crc16Official()` / `crc16Gp200Official()` | LITERAL, tables copied from EXE |
| serializer tail around `0x55a45d–0x55a804` | VTSI header, A128/B2048, B×4, SRC, CRC | `writeTrainerClo()` | LITERAL |
| `0x4818f0` | GP-200 B1024 compact conversion | `makeGp200CompactClo()` | LITERAL |
| executable CRT math wrappers (`0x4223xx/0x423000`) | `floor`, `pow`, `sqrt`, `exp`, log/trig primitives | standard CRT calls with audited cast boundaries | CRT PRIMITIVE |

**No conversion-domain routine in the statically traversed NAM→CLO call graph remains marked PENDING.**

## Exact conversion sequence reproduced

1. Load the selected stimulus base. The official profile is the 70-second GP-200 stimulus at 44.1 kHz; project stimulus/tail controls remain an intentional UI capability.
2. Feed only the first 70 seconds through `CDSPResampler24` from 44.1 kHz to the NAM expected sample rate.
3. Reset the NAM DSP with block size 1024 and process in 1024-sample calls. The final partial source block is zero padded to 1024 and only its valid output samples are copied.
4. Multiply NAM output by the fixed `0.31f` target scale.
5. Allocate trainer input and target at `70*Fs + 600`, copy the rendered 70 seconds and leave the last 600 samples zero.
6. Detrend in float32: remove mean, fit a line using x=1..N, subtract it.
7. Detect/compensate latency from the 6-second position over a 600-sample search using `abs(sample)>0.01`.
8. Estimate Ppos/Pneg/Kpos/Kneg from the first five seconds using the EXE's 100-ms extrema arrays, signed data ordering, double regression and float SSE searches.
9. Build the trainer PRE/POST paths and the 4× allpass/nonlinearity engine.
10. Measure 6–21 s through PRE→NL→POST. Measure 23–28 s through the official 50-tap conditioning FIR→PRE→NL→POST.
11. Smooth/regularize those spectra, set `Bstate=sweepSpec`, and set `Astate=lowSpec*1e6/(sweepSpec*1e6+FLT_EPS)`.
12. Execute the three optimizer phases: 23–28 s ×3, 6–21 s ×2, 30–50 s ×5. Each phase starts with step=1 and bestLoss=100, uses the EXE's correction weighting/clamps/conditioning order, updates A/B, creates minimum-phase FIRs, renders through the partitioned FIR engine, computes the fresh correction/loss, snapshots best state and performs the >1.2× rollback/extra-half-step rule.
13. Execute the 50–70 s B refinement using complete 100-ms blocks, target/model conditioning separately, ratio/clamps/Gaussian/256-point conditioning, minimum phase, direct B convolution, DC removal and final target/model energy normalization.
14. Multiply internal B2048 by four **before** final FIR SRC.
15. SRC A128 and B2048 to 44.1 kHz with the audited `CDSPResampler24` wrapper semantics, including process-then-zero-flush behavior.
16. Serialize the trainer VTSI object and calculate the EXE table CRC.
17. Produce the GP-200 compact form through the `0x4818f0` data path: verify source CRC/flags, copy the 0x88-byte header, clamp/copy 0x1200 payload, cap B count to 0x400, set declared size 0x1288, recalculate CRC and retain physical 0x2288 zero padding.

## Numeric implementation notes derived from the executable

- PRE is identity for this path but is still executed because its float/double rounding is observable.
- Hamming uses float phase/cos and single-precision multiply/subtract constants.
- Spectral estimator scales target/model samples by 1000 before mean removal, forces a final window at `totalLength-L`, folds into 2048 and uses `Sxx=model²`, `Sxy=conj(model)*target`.
- Minimum phase uses direct O(N²) DFTs in the executable path, not an interchangeable generic FFT shortcut.
- FIR rendering uses 64-sample partitions, FFT128 and only 65 stored positive-frequency bins; accumulation order is partition-major and float32.
- The FFT128 twiddle source is the executable's embedded Q15 table, not runtime `sin/cos`.
- No model-specific A gain, B shift, or golden-derived correction exists in this implementation.

## What “100% audited” means, and what it does not mean

The conversion **sequence, internal conversion routines, constants and data-flow reachable in the executable have been closed with zero PENDING conversion-domain routines**. That is the 100% static audit target.

It is not yet a claim that a newly compiled x64 executable will produce a byte-identical CLO for every NAM. Three provenance differences remain outside the recovered conversion logic: modern NAMCore is intentionally retained for A2 support; the GP executable does not carry a source-control tag that proves one precise historical r8brain release even though the `CDSPResampler24`/`CDSPFracInterpolator<24,673>` family and wrapper are identified; and elementary CRT math implementations can differ at last-bit level across compiler/runtime generations. These are documented rather than hidden or tuned with golden CLOs.

The golden CLOs are the next **validation** step only. If they differ, the difference must be traced to an audited implementation mismatch or one of the documented external-runtime provenance boundaries—not corrected by fitting the golden output.

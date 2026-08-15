# NAM to CLO 2.2.0

## v2.2 Block-B spectral branch

v2.2 freezes PRE, Block A, P/K and POST and iterates only the 2048-tap Block B. Its primary objective is a direct NAM-vs-CLO output-spectrum shape error over the first 50-second stimulus (4096 Hann FFT, 2048 hop, 48 log-frequency bands from 30 Hz to 20 kHz). A broad level offset is removed because output volume is not the target.

Each iteration measures the residual spectral curve, smooths it, limits the requested correction to +/-1.5 dB per pass and +/-6 dB total, and uses a short line search before accepting a B update. `_B_BEST` is the best spectral candidate found; `_B_REFINE` is accepted only if spectral-shape error improves by at least 0.25% and full-render NMSE does not regress by more than 5%.

The older v2.1 A+P/K experiment is retained only as historical context below.


Windows GUI utility that converts one Neural Amp Model (`.nam`) or batch-converts all `.nam` files in a selected folder into two CLO files per model:

- `<name>_Ampero_2048.clo` — the 2048-coefficient VTSI generated through Hotone `HTUSBTools.dll`.
- `<name>_GP200_1024.clo` — an experimental GP-200 compact serialization using the 1024-coefficient structure observed in the official Valeton editor.

## Historical v2.1 A+P/K spectral-response guard

v2.1 keeps the v2.0 joint Block-A + P/K search, but adds a second spectral metric designed to follow the broad transfer-function curve seen in external analysers. The metric uses the known 50-second stimulus as a reference, estimates Welch input/output power spectra at 4096 samples / 2048 hop, removes the stimulus spectral tilt, and collapses 30 Hz-20 kHz into 96 equal-log-frequency bands. Error is the mean absolute dB difference in response shape after removing one global level offset.

The v2.1 composite objective is 35% full-render NMSE, 20% level-conditioned NMSE, 20% input-referenced response-profile error, 15% multi-resolution STFT and 10% envelope error. The optimiser may traverse unconstrained A/P-K points internally, but `_BEST` and `_REFINE` are selected only from candidates whose response-profile error is no more than 0.10% worse than the original CLO. This specifically addresses cases where MR-STFT improved while an external spectral analyser showed the original CLO was still closer to the NAM.

## What is confirmed

- `namConvertCloData` can generate a 0x2288-byte VTSI without Ampero hardware in the tested runtime.
- The Ampero result uses a 2048-coefficient Block B (`modelField = 0x800`).
- The official GP-200 editor internally creates a 2048 model and then serializes the GP-200 form by keeping Block A plus the first 1024 Block B coefficients, changing the size fields/model count, recalculating CRC16/MODBUS, and zero-padding the physical file to 0x2288 bytes.

## Still experimental

The generated GP-200 1024 file has the observed official GP-200 structure, but the Ampero-generated coefficients are not byte-identical to Valeton's coefficients. Hardware validation on a real GP-200 is still pending.

## Runtime files required

The repository does **not** redistribute Hotone proprietary files. Before running the application, create:

```text
NamToClo.exe
runtime\
  ampero\
    HTUSBTools.dll
    nam_input_wav.wav
```

Copy `HTUSBTools.dll` and `nam_input_wav.wav` from your legally obtained Ampero II installation/package. In the runtime already tested during this research, no additional third-party files were needed beside these two.

## Usage

Double-click `NamToClo.exe`.

1. For a single conversion, click **Load NAM...** or drag a `.nam` file.
2. For batch conversion, click **Load Folder...** or drag a folder containing `.nam` files. Batch scanning is non-recursive.
3. Choose an output folder.
4. Click **Convert**.
5. The application generates both CLO files for every selected NAM automatically.

Batch mode continues after an individual conversion failure and shows a final success/failure summary.

If a destination filename already exists, the application creates ` (1)`, ` (2)`, etc. instead of silently overwriting it.

## Build

Requirements:

- Windows x64
- Visual Studio 2022/2026 Build Tools with Desktop C++ workload
- CMake 3.24+

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The executable will normally be at:

```text
build\Release\NamToClo.exe
```

Copy it to a clean folder together with `runtime\ampero\HTUSBTools.dll` and `runtime\ampero\nam_input_wav.wav`.

## Experimental P/K refinement (v1.9.4)

Version 1.9.4 includes an optional **CLO refinement (experimental)** stage. The normal
HTUSBTools conversion is preserved and the original Ampero B2048 / GP-200 B1024
files are still generated unchanged.

When **Refine P/K against the NAM render** is enabled, the converter also uses:

- the exact stimulus WAV staged for the conversion;
- `outputFile.wav`, rendered by HTUSBTools from the source NAM;
- a functional offline CLO player reconstructed from the GP-200 V1.8.0 DSP;

and performs a bounded coordinate search over only the four static nonlinear
parameters at CLO offsets `0x68..0x74`:

- `Ppos`
- `Pneg`
- `Kpos`
- `Kneg`

PRE, POST, FIR A and FIR B are kept byte-for-byte unchanged in this first
experiment. Output level is calibrated once from the original CLO against the NAM render and then frozen for every candidate. This prevents the optimiser from hiding excess gain/compression behind a new per-candidate normalisation.

Additional outputs:

- `*_Ampero_2048_REFINE.clo`
- `*_GP200_1024_REFINE.clo`

The UI reports the baseline and refined normalized mean-square error (NMSE) and
the percentage improvement. If the search does not improve the objective, the
`_REFINE` CLO is written with the original P/K values rather than making the
model worse.

### Scope of the v1.9.2 experiment

The refinement objective is evaluated on the complete rendered conversion audio
(normally 50 seconds of stimulus plus the 20-second tail), not on a short window
subset. The UI reports global NMSE plus separate stimulus and tail improvements.

For each conversion the original CLO is rendered first. A single output-level
calibration is fitted from that original render to the NAM target. That exact
scale is then frozen for all P/K candidates, so changes in saturation, compression
or effective gain are penalised instead of being normalised away.

FIR B remains fixed during P/K refinement but is evaluated efficiently over the
full signal with FFT overlap-save convolution. Quality is prioritised over speed.
PRE, POST, FIR A and FIR B remain unchanged in the generated refined CLO.


### v1.9.3 multi-metric acceptance

The experimental P/K refiner still evaluates the complete render (normally 50 s stimulus + 20 s tail) and keeps the original output calibration fixed. Candidate P/K values are now accepted only if three full-render metrics do not worsen simultaneously:

- temporal NMSE,
- 48-band logarithmic spectral error from 30 Hz to 18 kHz,
- RMS-envelope error in 2048-sample windows.

The optimizer uses a normalized composite score only after those hard guards pass. This is intentionally conservative: a lower sample-domain NMSE can no longer be accepted if it makes the spectrum or dynamics worse. PRE, POST, FIR A and FIR B remain unchanged.


### v1.9.4 research loss

The full-render refiner now uses a research-oriented objective over the complete rendered audio:

- raw time-domain NMSE with one fixed output calibration derived from the original CLO,
- multi-resolution STFT loss at FFT sizes 512, 2048 and 8192 (spectral convergence + log magnitude),
- multi-scale RMS-envelope error at 256, 2048 and 8192 samples.

The optimizer only writes a refined P/K set when the candidate does not regress in raw NMSE or MR-STFT versus the original CLO, stays within a very small envelope tolerance, and improves the normalized composite objective. PRE, POST, FIR A and FIR B remain unchanged.

A-weighted ESR was reviewed but intentionally deferred in this revision so low-frequency/low-mid mismatches are not de-emphasized while validating the new objective.


### v2.0.0 free-search + final safety gate

Version 2.0.0 keeps the v1.9.4 research metrics and full 70-second comparison,
but changes the optimiser. Intermediate P/K candidates are ranked by the
combined research loss and are allowed to trade metrics temporarily. A
deterministic 24-point Halton coarse search in log-parameter space is followed
by local pattern refinement. Strict temporal/spectral/envelope guards are
applied only to the final candidate; if it does not pass, the original CLO is
written unchanged. This is intended to avoid the 0% local-trap behaviour seen
with v1.9.4 while retaining conservative output validation.


## v2.0.0 P/K nonlinearity-specific refinement

The experimental P/K refinement now prioritises temporal waveform fidelity at the actual input excitation levels. The full 70 s render is split into non-silent 2048-sample windows and classified into low/mid/high RMS thirds. The loss weights are 35% global NMSE, 35% level-balanced NMSE, 15% MR-STFT and 15% multi-scale RMS envelope. This is intentionally a P/K-specific loss; later A/B refinement will use a different balance.

## v2.0.0 constrained P/K search

P/K optimisation now enforces waveform fidelity during the search itself rather
than optimising an unconstrained scalar loss and rejecting the result only at
the end. Global NMSE cannot worsen, low/mid/high excitation NMSE may regress by
at most 0.50%, and MR-STFT/envelope remain secondary guards. The deterministic
coarse search uses 64 Halton points, followed by constrained local refinement.
`_BEST` is now the best feasible candidate, not the unconstrained candidate.


## v2.0 experimental branch
The old v1.9 P/K-only refiner is retired. v2.0 jointly refines Block A (smooth 10-band frequency envelope) and P/K against the full NAM render while keeping PRE/POST/B fixed.

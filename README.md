# NAM to CLO v2.7.0-NATIVE5

NATIVE5 replaces the simplified NATIVE4 trainer math with the complete reconstructed baseline currently established in the research: direct exponential K fitting, 125 ms folded spectral estimator, Mel/Gaussian conditioning, accumulated A/B factorization state and the dedicated 100 ms final-B estimator.

This build keeps the existing conversion path intact and adds a second, parallel **Independent / Native** tab.

## Two independent tabs

### Current / HTUSBTools

This is the existing v2.6.7/v1.8 research workflow. It uses the user-supplied `HTUSBTools.dll` conversion path and keeps the existing Corrective IR and Tone Match refinement options.

### Independent / Native

This new path does **not** load `GP-200.exe`, `HTUSBTools.dll`, or an Ampero/Valeton conversion DLL. The NAM is rendered locally with the open-source NeuralAmpModelerCore library compiled into the application, and the NAM -> CLO identification/training logic is implemented in this project.

It currently reconstructs the researched baseline pipeline:

1. Build the same 70-second stimulus from the selected 50-second stimulus profile + selected 20-second tail + 600 trailing zero samples.
2. Load/render the NAM at its expected sample rate in 1024-sample blocks.
3. Apply the reconstructed NAM target scale (`0.31`).
4. Remove DC/linear trend and align the response from the impulse marker.
5. Estimate independent positive/negative P/K parameters.
6. Start Block A (128) and Block B (2048) as impulses.
7. Run the reconstructed A/B identification schedule over the 23-28 s, 6-21 s and 30-50 s regions.
8. Build minimum-phase FIR candidates from magnitude-domain corrections.
9. Refine Block B with the final 50-70 s tail.
10. Resample FIR coefficients to the 44.1 kHz CLO domain, serialize the B2048 VTSI container, and generate the GP-200 B1024 compact variant.

The independent output filenames are deliberately different so they can be compared side-by-side with the current converter:

```text
<name>_NATIVE_2048.clo
<name>_NATIVE_GP200_1024.clo
```

## Validation status

The Independent / Native tab now implements the full algorithmic baseline reconstructed in the research, including the 50-tap sample-rate-specific conditioning stage and `r8b::CDSPResampler24`. It is still **not a claim of byte-identical output**: the historical executable can differ in FFT/libm/compiler accumulation order and in the exact historical r8brain revision. Those are numerical-identity questions, not missing trainer blocks.

The intended validation is field-by-field against known NAM + converter-CLO pairs: P/K first, then A, B and finally rendered response.

Corrective IR and Tone Match refinement are disabled on the Independent / Native tab so the first comparisons measure only the reconstructed conversion algorithm.

## NAM A2

The native backend supports current NAM formats through NeuralAmpModelerCore. For an A2 `SlimmableContainer`, this research build extracts the embedded submodel with the highest `max_value` (Full) before rendering, matching the decision already used by the existing converter.

## Building

Windows x64 and CMake 3.24+ are required.

By default CMake fetches NeuralAmpModelerCore v0.5.4 and its submodules at configure time and statically compiles it into `NamToClo.exe`:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

For an offline/reproducible build, clone NeuralAmpModelerCore v0.5.4 including submodules and provide it explicitly:

```powershell
cmake -S . -B build -A x64 -DNAM_CORE_SOURCE_DIR=D:\path\to\NeuralAmpModelerCore
cmake --build build --config Release
```

The old tab can be built without the native trainer with:

```powershell
cmake -S . -B build -A x64 -DNTC_ENABLE_INDEPENDENT_TRAINER=OFF
```

## Runtime assets

The Independent / Native conversion does not need `HTUSBTools.dll`, but the stimulus builder still uses the selected research stimulus/tail WAV assets from the application's `runtime` folder unless a Custom stimulus or Recorded Audio source is selected.

The Current / HTUSBTools tab continues to require the legally obtained runtime files described in `THIRD_PARTY.md`.

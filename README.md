# NAM to CLO v2.7.0-FINAL

This build keeps the existing **Current / HTUSBTools** path untouched and updates the parallel **Independent / Native** backend with the complete trainer flow reconstructed from the GP-200 v1.8 converter analysis.

## v2.7.0-FINAL — completed official-converter reconstruction pass

This pass closes the two trainer-loader details that were still unresolved after NATIVE9: the exact `Fs*70 + 600` buffer semantics and the exact float32 target detrend/latency preprocessing used by `GP-200.exe`.

Confirmed from the official executable and implemented here:

- Only the first **70.000 s** of the selected 44.1 kHz stimulus are sent through sample-rate conversion and through NAM inference.
- The trainer then allocates **`Fs*70 + 600` float samples** for both input and target, copies the 70-second data, and leaves the final **600 samples as literal zeros**. The 600 guard samples are therefore **not resampled and not processed by the NAM**.
- The NAM target is scaled by the official **0.31f** factor before the 600 zero guard is appended.
- Target preprocessing first removes the mean with float32 accumulation, then performs a second float32 linear regression over **x = 1..N** and subtracts `slope*x + intercept`.
- Latency search begins at `6 * int(Fs)`, tests exactly 600 samples against `fabs(sample) > 0.01`, and returns **600** if no crossing is found.
- Target alignment remains equivalent to the official pointer-offset method because the 600-sample guard is now in the correct post-render location.
- The prior NATIVE10 reverse-engineering corrections remain: float/double boundaries, official POST recurrence, exact conditioning order, Gaussian behavior, minimum-phase boundaries, partitioned 64/128 FIR engine, official optimizer phases/loss/rollback, final B correction, r8brain `CDSPResampler24`, and GP-200 B1024 serialization.

The legacy HTUSBTools tab and the existing independent Stimulus Profile / Tail selection architecture are unchanged.

## NATIVE9 official-optimizer fidelity pass

NATIVE9 incorporates two details confirmed directly in the GP-200.exe optimizer after the NATIVE8 golden comparison: the per-iteration correction is passed through the official magnitude-conditioning routine **twice consecutively** before A/B redistribution, and the factor/correction/weight/step/loss working state is kept in **float32**, matching the `movss/mulss/divss` path in the official binary. The independent trainer still requires a fresh golden CLO comparison before byte-identity can be claimed.

## Independent / Native conversion

The native path does not load `GP-200.exe`, `HTUSBTools.dll` or an Ampero/Valeton conversion DLL. NAM rendering uses NeuralAmpModelerCore compiled into the application; CLO identification and serialization are implemented in this project.

The implemented baseline is:

1. Build the selected 50 s Stimulus Profile + selected 20 s Tail. The shared StimulusBuilder may also carry its legacy 600-sample guard for the HTUSBTools path; the Native backend explicitly removes that pre-SRC guard.
2. Render the NAM at its declared rate (48 kHz fallback), in 1024-sample blocks, then apply target scale `0.31`.
3. Remove DC/linear trend and align from the 6 s impulse marker.
4. Estimate `Ppos/Pneg/Kpos/Kneg` from 100 ms measurements in 0-5 s. P comes from extrema; K is seeded from the <=0.5*P slope and searched with the official 0.80..1.20 multipliers.
5. Start runtime A128/B2048 as impulses.
6. Build the initial spectral A/B factor state from 6-21 s and the sample-rate-specific 50-tap-conditioned 23-28 s sweep.
7. Run three independent optimizer phases: 23-28 s x3, 6-21 s x2, 30-50 s x5. Each phase resets step/best state and restores its best candidate on exit.
8. Apply the reconstructed 80 Hz -> Nyquist frequency weighting, Mel/dB conditioning, exact Gaussian kernel layout, sequential low-frequency A smoothing, and variable-size cepstral minimum-phase reconstruction.
9. Evaluate candidates with the 512-point uniform-Mel 80 Hz..10 kHz mean absolute log-ratio loss and the reconstructed rollback/step rules.
10. Refine B from 50-70 s using 100 ms periodic summation, explicit positive-frequency DFT, [0.1,10] correction, exact conditioning to 256 magnitudes, a 256-tap minimum-phase corrector, convolution and energy normalization.
11. Resample A/B to the 44.1 kHz CLO domain with `r8b::CDSPResampler24`, serialize B2048, apply the B x4 file scale, and generate the GP-200 B1024 compact variant.

Output files remain separate from the current converter:

```text
<name>_NATIVE_2048.clo
<name>_NATIVE_GP200_1024.clo
```

## Validation status

NATIVE8 is the strict-equivalence pass driven by the NATIVE7/official golden comparison. In addition to the previously reconstructed trainer flow, it reproduces the float32 arithmetic visible in the official POST, conditioning, minimum-phase and final-B paths; the final tail stage conditions target and model spectra independently before ratio; and FIR SRC output is truncated before zero padding. It remains a **golden-reference candidate** until a newly generated CLO is compared against the official output; no byte-identity claim is made before that test.

Corrective IR and Tone Match remain outside the Independent / Native tab so validation measures only the reconstructed converter.

## NAM A2

For an A2 `SlimmableContainer`, the independent backend extracts the embedded submodel with the highest `max_value` (Full) before rendering, preserving the existing project decision.

## Building

Windows x64 and CMake 3.24+ are required.

```powershell
cmake --preset windows-x64
cmake --build build --config Release --parallel
```

By default CMake fetches NeuralAmpModelerCore v0.5.4 and **r8brain-free-src `version-3.7`**. The r8brain pin is deliberate: `GP-200.exe` contains RTTI for the old template-era `CDSPResampler<CDSPFracInterpolator<24,673>>`, while r8brain 4.0 replaced that template architecture. Local source trees can be supplied with `NAM_CORE_SOURCE_DIR` and `R8BRAIN_SOURCE_DIR`; for equivalence testing the local r8brain tree should match the pinned template-era branch.

The old tab can be built without the native trainer with:

```powershell
cmake -S . -B build -A x64 -DNTC_ENABLE_INDEPENDENT_TRAINER=OFF
```

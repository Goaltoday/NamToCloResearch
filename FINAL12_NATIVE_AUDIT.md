# FINAL12-NATIVE — NAM -> CLO audit status

## Scope

Goal: reproduce the GP-200/HTUSBTools NAM -> CLO conversion natively, with no runtime dependency on `HTUSBTools.dll` or `GP-200.exe`. The proprietary binaries are treated only as reverse-engineering specifications. Official CLO files are validation vectors, not sources for inferred gains or corrections.

NeuralAmpModelerCore is intentionally kept on the modern project version so A2/Slimmable/WaveNet NAM files remain supported. The surrounding stimulus, trainer and CLO construction path follows the audited converter behavior.

## Audited conversion chain

1. Read the 70 s identification/reamp stimulus.
2. Resample the 70 s source to the NAM/trainer rate with the historical `CDSPResampler24` process path when needed.
3. Reset/process NAM in 1024-sample blocks; the final partial block is zero padded for the process call and only valid output samples are retained.
4. Apply the official target scale `0.31f`.
5. Append the 600-sample guard region after NAM rendering/resampling.
6. Detrend target and perform the audited latency search/alignment.
7. Fit asymmetric Ppos/Pneg/Kpos/Kneg from the first five seconds using the converter's float/double boundaries and K search.
8. Build PRE/POST sections with the converter's numerical boundaries.
9. Initial identification:
   - 6–21 s low excitation transfer estimate;
   - 23–28 s sweep after the exact 50-tap sample-rate-specific conditioning FIR;
   - initialize `Bstate = sweepSpec`, `Astate = lowSpec / sweepSpec` with the audited regularization.
10. Execute optimizer phases 23–28 s x3, 6–21 s x2, 30–50 s x5 with the audited step decay, conditioning, A/B factorization, best-state and rollback logic.
11. Spectral estimator: 125 ms windows, 50% hop, forced final frame, x1000 boundary, per-frame mean removal, Hamming, fold to 2048, Ooura RDFT, Sxx/Sxy transfer magnitude.
12. Conditioning: Gaussian/Hz/Mel interpolation path and frequency weighting as reconstructed from the converter.
13. Minimum phase: direct float DFT/cepstral lifter/direct DFT reconstruction, truncation, DC removal and energy normalization. For equal input/output lengths, the HTUSBTools wrapper uses the direct-copy path; no half-buffer reversal is applied.
14. Model render: PRE -> FIR A -> 4x polyphase interpolation -> asymmetric exponential P/K nonlinearity -> 4x decimation -> POST -> FIR B.
15. FIR engine: 64-sample partitions, FFT128, 65 nonredundant bins, partition ring, conjugate reconstruction and 64-sample overlap-add.
16. Final B refinement from the 50–70 s material, using complete 100 ms folded blocks, full direct DFT magnitude ratio, 256-tap minimum-phase corrector and final energy normalization.
17. Multiply B by `4.0f` before final FIR sample-rate conversion.
18. Serialize trainer A128/B2048 VTSI payload and convert to the GP-200 A128/B1024 compact layout.
19. Recompute the table-equivalent CRC and preserve the GP-200 physical padding layout.

## Native dependencies

The Independent / Native backend compiles the following into `NamToClo.exe`:

- NeuralAmpModelerCore (modern version retained for A2 support);
- r8brain-free-src historical template-era `CDSPResampler24` family;
- Ooura FFT4G RDFT source selected from SoXR 0.1.3.

It does not dynamically load `HTUSBTools.dll` and does not execute `GP-200.exe`.

The exact Legacy/Original stimulus still requires the legally obtained `nam_input_wav.wav` data asset. That WAV is input data, not an executable/DLL dependency, and is intentionally not included in the source archive.

## Validation status

The static audit and native implementation are complete for this revision, including correction of the FINAL9 minimum-phase regression and the missing Ooura build source. A Windows/MSVC build and a fresh conversion are still required to measure output equality against the official golden CLO. Until that fresh binary result is compared, this document does not claim byte-identical or perceptually identical output.

No golden-derived `A *= 0.619`, manual B sample shift, or model-specific correction exists in FINAL12.

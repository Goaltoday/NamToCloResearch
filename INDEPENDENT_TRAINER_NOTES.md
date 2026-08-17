# Independent NAM -> CLO trainer: NATIVE7

NATIVE7 replaces the remaining NATIVE5/NATIVE6 approximations with the details recovered directly from the official GP-200 v1.8 conversion flow.

## Confirmed flow now represented in code

- NAM render at expected sample rate; 48 kHz fallback; block size 1024; target scale 0.31.
- Target DC/trend removal and latency alignment from the 6 s impulse.
- P/K estimator: 100 ms windows over 0-5 s, P from extrema, zero-intercept slope up to 0.5*P, K seed = slope/P, multiplier search 0.80..1.20 in 0.05 steps, positive branch first and negative branch second.
- PRE identity and source-rate POST coefficients.
- Runtime A128/B2048 impulse initialization.
- Exact 50-tap conditioning tables for 44.1/48/96 kHz.
- Initial factorization: low-level response from 6-21 s; conditioned sweep response from 23-28 s; sweep Gaussian lengths int(1025*.001) and int(1025*.005); floor `f=.001*max(lowSpec)` with `v=f+v^2/(4f)` for `v<2f`; `Astate=lowSpec/conditionedSweep`, `Bstate=conditionedSweep` using the reconstructed 1e6/FLT_EPS scaling.
- Spectral estimator: `ceil(.125*Fs)` Hamming frames, 50% overlap, frame mean removal, fold modulo 2048, 2048 FFT, 1025 bins, `|Sxy|/(Sxx+FLT_EPS)`.
- Frequency weight: 1.0 below the reconstructed ~80 Hz start and linear 1.0 -> 0.5 to Nyquist; exponent is `weight*step`.
- Three separate optimizer invocations: 23-28 s x3, 6-21 s x2, 30-50 s x5. Each starts `step=1`, `bestLoss=100`, correction=1 and restores its own best state at exit.
- Correction clamp `[0.2,5]`, normal step decay x0.9 and rollback at `loss>1.2*best` with additional x0.5.
- Factor update `Astate/=C`, `Bstate*=C` after the exact 0x554f00 magnitude conditioning.
- A low-frequency smoothing is sequential/in-place up to ~60 Hz.
- Magnitude conditioner: dB, uniform Mel working grid, Gaussian #1 `int(N1*.002)`, interpolation to linear Hz, Gaussian #2 `2*(N1/N2)`, destination linear-Hz grid, dB->magnitude.
- Gaussian implementation preserves the official even-kernel behavior (N+1 storage with zero extra element), reversed normalized weights and edge renormalization.
- Minimum phase uses a mirrored spectrum of length `2*N-2`, therefore A128 uses 254 points, B1025 uses 2048 and the final 256-point corrector uses 510. Magnitude floor is `max*1e-5`; cepstrum is causalized; output taps have their mean removed and norm restored.
- B candidate is built from the fresh post-NL response divided by the updated B factor state.
- Loss is evaluated on 512 frequencies uniformly spaced in Mel from exactly 80 Hz to exactly 10 kHz: mean `abs(log(ratio+FLT_EPS))`.
- Final B: 50-70 s only; `ceil(.1*Fs)` periodic SUM (not average), mean removal, Hamming, explicit positive DFT, target/model x1e6 ratio, clamp [0.1,10], Gaussian `int(posBins*.1)`, exact conditioner to 256, 510-point minimum-phase reconstruction -> 256 taps, B convolution/truncation, B mean removal, tail energy normalization.
- r8brain `CDSPResampler24` for rate conversion.
- VTSI B2048 serialization, B x4 storage scale, GP-200 B1024 compact output, reconstructed CRC byte order.

## Validation rule

Do not label a new native result “100% equivalent” solely from code inspection. Equivalence is accepted only after golden-reference comparison of the same NAM + stimulus against the current/official converter. Compare P/K, POST, A, B and rendered response; byte identity is a stricter final check.


## NATIVE8 strict-equivalence corrections

The final B routine in GP-200.exe does **not** form a raw target/model magnitude ratio and then condition that ratio. It calls the magnitude-conditioning routine separately for target and candidate model, forms the ratio of those two conditioned curves, clamps/smooths it, then conditions that correction to 256 points and creates the minimum-phase FIR256. NATIVE8 follows that ordering.

Where the disassembly uses `movss/addss/mulss/divss`, NATIVE8 keeps float32 state instead of accumulating in double/long double. The POST coefficients are also computed as float32 and only then promoted to the CLO double fields.

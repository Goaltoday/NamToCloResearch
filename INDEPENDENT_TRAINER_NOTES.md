# Independent NAM -> CLO trainer: validation notes

## Confirmed/reconstructed baseline represented in code

- NAM is treated as a black-box processor driven by the 70 s research stimulus.
- NAM expected sample rate is used; invalid/missing rate falls back to 48 kHz.
- Render block size: 1024 samples.
- Target render scale: 0.31.
- Target detrending and impulse-based latency alignment.
- Separate positive/negative exponential P/K branches.
- PRE is identity for the reconstructed NAM conversion path.
- POST is the fixed source-rate-dependent high-pass reconstructed from the official converter.
- Block A starts as a 128-tap impulse; Block B starts as a 2048-tap impulse.
- Main schedule: 23-28 s x3, 6-21 s x2, 30-50 s x5.
- Magnitude/cross-spectrum-driven correction, minimum-phase FIR reconstruction, logarithmic spectral loss and rollback/step decay.
- Dedicated final Block-B correction over 50-70 s.
- Block B receives the reconstructed x4 serialization scale.
- VTSI B2048 container plus GP-200 B1024 compact output and CRC16/MODBUS formatting.

## Deliberate first-pass substitutions / not claimed bit-identical

- Local windowed-sinc SRC is used instead of reproducing the exact r8brain implementation/version/order used by the official binary.
- The exact 50-tap initial-conditioning stage is not asserted bit-identical in this first native implementation.
- FFT/exp/log/pow and floating-point accumulation order are not expected to match the official MSVC binary at the last bit.

## Golden-reference workflow

For each known NAM + official CLO pair, compare at minimum:

- PRE/POST coefficients
- Ppos/Pneg/Kpos/Kneg
- correlation, MAE and RMSE of A[0..127]
- correlation, MAE and RMSE of B[0..1023]
- frequency responses of A, B and A*B
- time-domain and harmonic response of the rendered native CLO vs NAM and official CLO

The native tab intentionally writes different output suffixes so both paths can be run without overwriting one another.

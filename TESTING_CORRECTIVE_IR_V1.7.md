# NAM to CLO v1.7 - Corrective IR test plan

## 1. Regression: correction OFF

1. Use exactly the same NAM, stimulus mode, tail and runtime as v1.6.
2. Leave **Apply corrective IR** unchecked.
3. Convert.
4. Compare both outputs with v1.6 using SHA-256 or a byte-by-byte comparison.

Expected: both the Ampero 2048 and GP-200 1024 files are byte-identical to v1.6.

## 2. Corrective IR ON

1. Check **Apply corrective IR**.
2. Select the validated 44.1 kHz corrective WAV.
3. Convert.

Processing order:

- HTUSBTools generates the normal Ampero 2048 CLO.
- Block A is preserved unchanged.
- Block B (2048 float32 values) is linearly convolved with the corrective IR.
- Only the first 2048 convolution samples are retained.
- The result is RMS-matched to the original Block B.
- A fixed additional -6 dB gain is applied.
- The Ampero CRC is recalculated.
- The corrected Ampero 2048 is then passed to the existing GP-200 2048 -> 1024 compacting function.

## 3. Accepted corrective WAVs

v1.7 intentionally requires 44.1 kHz. Mono and stereo WAVs are accepted. Stereo is downmixed as `(L + R) / 2`.

Supported encodings:

- PCM 8/16/24/32-bit
- IEEE float 32/64-bit

The IR is not peak-normalized and is not resampled in this version.

## 4. Useful verification

For a corrected conversion:

- Block A must be byte-identical to the uncorrected Ampero CLO.
- Block B should differ.
- The corrected Ampero file must remain 0x2288 bytes and validate as a 128 + 2048 VTSI.
- The GP-200 file must preserve all 128 values of corrected Block A and the first 1024 values of corrected Block B, using the existing compact serializer.

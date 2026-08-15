# NAM to CLO v1.8 - Unified Legacy stimulus test plan

## What changed

`Original / Legacy` no longer bypasses the StimulusBuilder by copying the full historical `nam_input_wav.wav`.

In v1.8 every stimulus profile uses the same construction path:

1. 50.000 s base stimulus
2. 20.000 s selected Tail / Reamp source
3. 600 zero samples

For `Original / Legacy`, the base is the first 50.000 seconds of `runtime/ampero/nam_input_wav.wav`.

The Tail / Reamp selector is now enabled for Legacy as well, including `Recorded Audio`.

## Important regression note

This is an intentional behavioral change. Earlier research found that the last 20 seconds embedded in the historical `nam_input_wav.wav` are extremely close to, but not necessarily byte-identical to, `PresetAudio.wav` (a small level difference was observed). Therefore:

- `Legacy + Original Preset Audio` is the intended v1.8 reconstruction of the Legacy profile through the unified path.
- It must be tested against the old v1.7 Legacy output before claiming byte-for-byte identity.
- A difference from v1.7 would not indicate that the v1.8 builder is broken; it may reflect the known difference between the historical embedded tail and `PresetAudio.wav`.

## Required tests

### 1. Legacy + Original Preset Audio

Convert the same reference NAM with:

- v1.7 `Original / Legacy`
- v1.8 `Original / Legacy` + `Original Preset Audio`

Compare both Ampero 2048 and GP-200 1024 CLO files byte-by-byte and compare Block A / Block B.

### 2. Legacy + Recorded Audio

Select `Original / Legacy` and `Recorded Audio` and confirm that the recorded WAV is accepted and adapted to 20.000 s exactly like Clean, Dist and Custom.

### 3. Existing modes

Verify that Clean, Dist and Custom produce the same outputs as v1.7 when using the same tail and options.

### 4. Corrective IR

Verify that Corrective IR OFF/ON behavior is unchanged from v1.7. The v1.8 change is only in stimulus assembly and GUI enablement for the Legacy tail.

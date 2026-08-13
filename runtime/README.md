# Runtime setup

Create this layout next to `NamToClo.exe`:

```text
runtime\
  ampero\
    HTUSBTools.dll
    nam_input_wav.wav
    inputSignalCleanSW.wav
    inputSignalDistSW.wav
    PresetAudio.wav
```

`HTUSBTools.dll` and all WAV files above are proprietary third-party runtime files and are intentionally not committed to this repository.

## Mode requirements

- **Legacy** requires only `HTUSBTools.dll` + `nam_input_wav.wav` and preserves the v1.1 path.
- **Clean tests** additionally require `inputSignalCleanSW.wav` + `PresetAudio.wav`.
- **Dist tests** additionally require `inputSignalDistSW.wav` + `PresetAudio.wav`.

The experimental Clean/Dist builder expects the Sound Clone source WAVs to be mono PCM16, 44.1 kHz, with exact durations of 50 s (Clean/Dist) and 20 s (`PresetAudio.wav`). No normalization, gain adjustment, resampling, fades, or other DSP is applied.

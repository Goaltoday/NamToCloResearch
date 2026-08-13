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

- **Original / Legacy** requires only `HTUSBTools.dll` + `nam_input_wav.wav` and preserves the validated v1.1 path byte-for-byte.
- **Clean** additionally requires `inputSignalCleanSW.wav` + `PresetAudio.wav`.
- **Dist** additionally requires `inputSignalDistSW.wav` + `PresetAudio.wav`.

For Clean and Dist, v1.3 builds the stimulus as observed in the official Sound Clone application:

```text
50 s Clean/Dist source
+ 20 s PresetAudio
+ 600 zero samples
```

The generated Sound Clone-style stimulus is mono PCM16 at 44.1 kHz. No normalization, gain adjustment, resampling, fades, or other DSP is applied.

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

- **Original / Legacy** uses the first 50.000 s of `nam_input_wav.wav` as its base stimulus, then appends the selected 20.000 s Tail / Reamp source through the same builder used by Clean, Dist and Custom.
- **Clean** requires `inputSignalCleanSW.wav` plus either `PresetAudio.wav` or a user-selected Recorded Audio WAV.
- **Dist** requires `inputSignalDistSW.wav` plus either `PresetAudio.wav` or a user-selected Recorded Audio WAV.

From v1.8 onward, every stimulus profile uses the same assembly path:

```text
50 s Legacy/Clean/Dist/Custom source
+ 20 s selected tail
+ 600 zero samples
```

The tail can be the original `PresetAudio.wav` or a user recording.

## Recorded Audio in v1.4

The user recording can have any practical duration. The program converts it to an **exact 20.000-second tail**: audio after 20 seconds is trimmed and a shorter recording is padded with digital silence at the end.

The converter automatically adapts the selected WAV to the format needed by the Sound Clone-style stimulus:

- any channel count -> mono (channel average)
- PCM 8/16/24/32-bit or IEEE float 32/64-bit -> PCM16
- any valid sample rate -> 44.1 kHz (linear interpolation)

No level normalization or gain compensation is applied.

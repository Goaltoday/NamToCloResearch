# NAM to CLO v2.7.0

Windows x64 application for converting Neural Amp Modeler (`.nam`) models to CLO files and uploading GP-200 CLO files over USB MIDI.

## Convert to CLO

The **Convert to CLO** tab converts a single NAM file or every NAM file in a selected folder.

Place `nam_input_wav.wav` in the same folder as `NamToClo.exe`. The original conversion stimulus is always used; no stimulus-profile selection is required.

Generated files:

```text
<name>_NATIVE_2048.clo
<name>_NATIVE_GP200_1024.clo
<name>_NATIVE_GP200_1024_REFINED.clo   (when Tone Match is enabled)
```

### Tail / Reamp

- **Original Preset Audio** uses the original final 20 seconds of `nam_input_wav.wav`.
- **Recorded Audio** lets you select a WAV file for the final 20-second tail. The audio is adapted automatically to the required format and duration.

### Corrective IR

Optionally apply a corrective IR WAV to the normal CLO output.

### Tone Match

Optionally refine the GP-200 CLO using Tone Match. A separate refined GP-200 CLO is generated, leaving the normal conversion output available as well.

An optional reference WAV can be selected for Tone Match; the first 20 seconds are used.

## GP-200 Uploader

The **GP-200 Uploader** tab sends an existing `.clo` file directly to a selected GP-200 SnapTone slot over USB MIDI.

## Build

Requirements: Windows x64, CMake 3.24+ and Visual Studio/MSVC.

```powershell
cmake --preset windows-x64
cmake --build build --config Release --parallel
```

## Licensing

See `LICENSE` and `THIRD_PARTY.md`.

This is an independent research/reimplementation project and is not affiliated with or endorsed by Valeton or Hotone.

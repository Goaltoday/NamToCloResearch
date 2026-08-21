# NAM to CLO v2.7.0

Windows x64 application for converting Neural Amp Modeler (`.nam`) models to CLO files and uploading GP-200 CLO files over USB MIDI.

## Conversion backends

### Independent / Native

The native backend is the reconstructed GP-200/Valeton conversion path. It does **not** load or execute `GP-200.exe` or `HTUSBTools.dll`. NAM inference is provided by NeuralAmpModelerCore and the CLO identification/serialization pipeline is implemented in this project.

The retained native flow includes the reconstructed official behavior used by the release build: 70-second stimulus handling, the 600-sample post-render guard, fixed NAM target scale, target detrend/latency preprocessing, P/K estimation, A/B factorization and optimizer phases, final B refinement, r8brain FIR sample-rate conversion, B2048 serialization and GP-200 B1024 compact output.

Native output files:

```text
<name>_NATIVE_2048.clo
<name>_NATIVE_GP200_1024.clo
```

For A2 `SlimmableContainer` NAMs, the embedded submodel with the highest `max_value` (Full) is used before rendering.

### Current / HTUSBTools

The existing HTUSBTools conversion backend is retained unchanged for compatibility. Corrective IR and CLO refinement remain available only on this backend.

### GP-200 Uploader

The uploader tab sends an existing `.clo` file directly to a selected GP-200 SnapTone slot over USB MIDI.

## Stimulus selection

The converter keeps the existing Stimulus Profile and Tail selection behavior. The native backend uses the same selected 50-second stimulus profile plus 20-second tail and then applies the reconstructed official trainer preprocessing internally.

## Build

Requirements: Windows x64, CMake 3.24+, Visual Studio/MSVC.

```powershell
cmake --preset windows-x64
cmake --build build --config Release --parallel
```

The default native build fetches:

- NeuralAmpModelerCore v0.5.4
- r8brain-free-src `version-3.7`
- Ooura FFT4G source from SoXR 0.1.3

Local source trees can be supplied with `NAM_CORE_SOURCE_DIR`, `R8BRAIN_SOURCE_DIR`, and `NTC_OOURA_SOURCE_DIR`.

## Release scope

This release intentionally contains the reconstructed native converter only as a production conversion path. The later experimental NAM→CLO improvement phases, their diagnostic WAV/CSV generation, harmonic/IMD fitting experiments, DI-guided B fitting, and associated research result files are not part of this branch.

## Licensing

See `LICENSE` and `THIRD_PARTY.md`. This is an independent research/reimplementation project and is not affiliated with or endorsed by Valeton or Hotone.

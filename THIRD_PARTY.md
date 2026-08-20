# Third-party components

## NeuralAmpModelerCore — Independent / Native tab

The Independent / Native backend uses **NeuralAmpModelerCore v0.5.4**, the open-source NAM DSP library, compiled statically into the application at build time. NeuralAmpModelerCore is MIT-licensed and has its own third-party dependencies/submodules (including Eigen and nlohmann/json) under their respective licenses.

The CMake project can fetch this source at configure time or use a local checkout via `NAM_CORE_SOURCE_DIR`. No NeuralAmpModelerCore DLL is required at application runtime.

## Proprietary Hotone/Ampero runtime — Current / HTUSBTools tab only

This project does not include or redistribute proprietary Hotone/Ampero files.

The existing Current / HTUSBTools tab expects user-supplied runtime assets such as:

```text
runtime\ampero\HTUSBTools.dll
runtime\ampero\nam_input_wav.wav
```

Those files must come from a legally obtained installation/package and remain subject to their original license terms.

The source code dynamically loads `HTUSBTools.dll` only when the existing conversion path is used. The Independent / Native tab does not call that conversion DLL.


## r8brain-free-src

The independent trainer uses `r8b::CDSPResampler24` from r8brain-free-src (MIT), pinned to the historical `version-3.7` template-era branch because the official GP-200 converter contains RTTI for `CDSPResampler<CDSPFracInterpolator<24,673>>`. Releases before r8brain 4.0 use this templated architecture; 4.0 replaced it with runtime-calculated interpolation parameters. The source is compiled into the executable (including `r8bbase.cpp` when present) and is not a runtime DLL dependency. Credit: Sample rate converter designed by Aleksey Vaneev of Voxengo.

## Ooura FFT4G — Independent / Native spectral estimator

The independent trainer uses Takuya Ooura's FFT4G RDFT numerical path for the 2048-point transfer-function estimator. The build fetches the `fft4g.c` implementation shipped by SoXR 0.1.3, or accepts a local source tree through `NTC_OOURA_SOURCE_DIR`, and compiles that C source directly into `NamToClo.exe`.

This is a build-time source dependency only: the Native backend does not require a SoXR DLL, HTUSBTools DLL, or GP-200 executable at runtime. Preserve the upstream licence/notices when distributing compiled builds.

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

The independent trainer uses the header-only `r8b::CDSPResampler24` sample-rate converter from r8brain-free-src (MIT). It is compiled into the executable and is not a runtime DLL dependency. Credit: Sample rate converter designed by Aleksey Vaneev of Voxengo.

# FINAL12-NATIVE changelog

This revision is the consolidation pass after the EXE and HTUSBTools DLL audits. The DLL/EXE are reverse-engineering references only; the Independent / Native backend does not load or call them.

## Correctness fixes

- Removed the erroneous equal-length minimum-phase half-buffer reversal introduced in FINAL9. HTUSBTools takes the direct-copy branch when input and output lengths are equal, which is the normal A/B trainer path.
- Kept the audited r8brain `CDSPResampler24` streaming `process()` + zero-flush path. `oneshot()` is not used.
- Added the Ooura/FFT4G C implementation to the CMake target. `independent_trainer.cpp` already called `lsx_rdft`; FINAL12 now links the matching source rather than leaving the implementation unresolved.
- Preserved the DLL-audited 64/128 partitioned FIR structure, 65 positive bins, Q15 FFT twiddles, overlap-add, PRE/POST numerical boundaries, 4x polyphase path, P/K fitter, optimizer, final-B correction, FIR SRC, B x4 serialization order, and GP-200 compact container/CRC behavior.
- Kept NeuralAmpModelerCore modern enough for A2/Slimmable/WaveNet support. This is the one intentional compatibility extension relative to the old NAMCore embedded in the official converter.

## Exact default stimulus

- `Original / Legacy + Original Preset Audio` now reads the full first 70.000 seconds of the official `nam_input_wav.wav` directly and appends the 600-sample trainer guard area.
- This exact default no longer requires a separately exported `PresetAudio.wav`.
- The independent Stimulus Profile and Tail / Reamp controls remain separate for all other combinations.

## Build/runtime independence

- Native conversion has no runtime call to `HTUSBTools.dll` or `GP-200.exe`.
- NeuralAmpModelerCore, r8brain and Ooura FFT4G are compiled into the executable.
- Proprietary Valeton/Hotone binaries and WAV assets are not redistributed in this source package.

## Validation rule

No coefficient scale, delay, FIR shift, or other compensation has been fitted from a golden CLO. Golden CLO files are used only to compare the compiled native result with the official output.

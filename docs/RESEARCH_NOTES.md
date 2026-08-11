# Research notes - v0.2

## Confirmed

### Valeton side

- GP-200 NAM -> CLO conversion is local on the PC.
- The final CLO size observed is fixed at `0x2288` / 8840 bytes.
- The post-NAM stage is real DSP/identification work, not simple WAV packaging.

### Ampero package

- `HTUSBTools.dll` is Windows x64.
- It exports `namConvertClo`, `namConvertCloData`, `getNormalWav`, `initWithWaveDicPath`, `initWithWaveDicPathJson`, and `InitDartApiDL`.
- The bundled `nam_input_wav.wav` SHA-256 matches the Valeton stimulus previously measured:
  `9bb6c1b136dfbeb7538a6060499d98c89342b76ec568b76836e36ab98b29aa1a`.
- `namConvertClo` and `namConvertCloData` contain the `0x2288` result/size constant.

### Dynamic v0.1 experiment

An external process successfully called `namConvertClo` with:

```text
arg1 = nullptr
arg2 = supplied nam_input_wav.wav
arg3 = outputFile.wav
arg4 = input.nam
arg5 = output.clo path
```

Hotone's own log confirmed:

```text
namConvertClo
startThread
NamConvertThread-----
[NAM] convertType=1
[NAM] namFile: ...input.nam
[NAM] inputFile: ...nam_input_wav.wav
[NAM] outputFile: ...outputFile.wav
[NAM] model sampleRate: 48000 in_channels=1 out_channels=1
[NAM] audioread: fs=48000 samples=3360000
[NAM] audiowrite done: ...outputFile.wav
[NAM] ========== getNamOutput end ==========
```

The generated WAV was 70 s / 48 kHz / stereo PCM16 with identical L/R channels. The worker then ended with `0xC0000409`, and the file CLO remained empty.

Therefore arguments 2-4 are no longer merely guessed: their meaning is confirmed by the DLL's own diagnostic log.

## Strongly supported but not yet dynamically proven

Static analysis of `namConvertCloData` supports:

```cpp
uint32_t namConvertCloData(
    void* context,
    const char* inputWav,
    const char* outputWav,
    const char* inputNam,
    void* outputBuffer,
    uint64_t arg6);
```

The fifth argument is used by the CLO-data path as a destination for a fixed-size `0x2288` copy. v0.2 therefore supplies a parent-owned shared-memory buffer there.

## Still unknown

- Exact meaning of `arg6` in `namConvertCloData`.
- Whether `arg1` becomes mandatory only after `getNamOutput`.
- Whether Dart is required for the CLO stage or only for progress/completion callbacks.
- Whether Ampero-generated VTSI data is functionally accepted by GP-200 hardware.

## v0.2 experiment

The parent creates a named Windows file mapping of 8840 bytes, fills it with `0xCC`, and launches a worker. The worker opens the mapping and passes it as arg5 to `namConvertCloData`, with arg6 set to zero.

The parent owns the mapping, so its contents remain readable even if the worker fast-fails. The parent always stores the final mapping as `captured-buffer.bin`. A `VTSI` prefix causes the complete 8840-byte buffer to be materialized as `output.clo`.

The legacy `namConvertClo` route remains available through `--mode file` as a control.


## v0.3 arg6 experiment

Confirmed live result from v0.2: `namConvertCloData(nullptr, inputWav, outputWav, inputNam, outputBuffer, 0)` generated a stable `VTSI`-shaped 8840-byte buffer externally. Comparing the same NAM against Valeton showed Ampero-shaped fields `0x2288 / 0x2200 / 0x0800` versus Valeton `0x1288 / 0x1200 / 0x0400`. v0.3 exposes arg6 and adds a controlled sweep. The hypothesis that arg6 selects model capacity/target remains unconfirmed until the sweep changes these fields or other output characteristics.

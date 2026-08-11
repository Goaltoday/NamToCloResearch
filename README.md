# NamToCloResearch

Experimental Windows x64 project for calling the NAM -> CLO conversion code already present in the official Ampero II `HTUSBTools.dll`, without launching the Ampero editor.

> Status: **research / ABI probe v0.2**. This is not yet a validated public API or a finished converter.

## v0.3: arg6 experiments

The successful v0.2 live test confirmed that `namConvertCloData` can generate a complete `VTSI` buffer of `0x2288` bytes outside the Ampero II editor. v0.3 exposes the still-unknown sixth native argument so its effect can be measured without changing the confirmed path arguments or output buffer.

Single-value test:

```powershell
.\NamToClo.exe .\modelo.nam .\arg6_1.clo --mode data --arg6 1 --keep-temp --verbose
```

Inspect the structural fields relevant to the Valeton comparison:

```powershell
.\NamToClo.exe --inspect .\arg6_1.clo
```

The inspector reports the physical size plus little-endian fields at `0x04`, `0x14`, `0x84`, and the last non-zero byte. The current Ampero-shaped result is `0x2288 / 0x2200 / 0x0800`; the observed GP-200 target shape is `0x1288 / 0x1200 / 0x0400` inside the same physical `0x2288` allocation.

Automated sweep:

```powershell
.\scripts\sweep-arg6.ps1 -Nam .\modelo.nam -Start 0 -End 15
```

The script creates one CLO and one log per value plus `sweep-summary.csv`. Use a smaller range such as `0..3` first if desired. Each conversion still runs through the isolated worker.


## Current evidence

Confirmed experimentally with the v0.1 probe:

- `HTUSBTools.dll` loads correctly outside Ampero II.
- `namConvertClo` starts its internal `NamConvertThread`.
- The DLL reads the supplied `input.nam` and supplied `nam_input_wav.wav`.
- The DLL produces a valid 70 s / 48 kHz / stereo PCM16 `outputFile.wav`.
- The Hotone log reaches `[NAM] ========== getNamOutput end ==========`.
- The legacy file route then terminates with Windows fast-fail `0xC0000409` before a non-empty CLO is written.

Confirmed by static analysis and used by v0.2 as the next experiment:

- `namConvertCloData` exists and returns `0x2288` (`8840`).
- Its fifth argument is strongly supported as a caller-owned destination buffer used by the CLO path.
- The exact role of argument 6 is not confirmed; v0.2 deliberately passes `0`.

The project remains separate from GP200 Studio VST.

## What changed in v0.2

The default mode is now:

```text
namConvertCloData
        |
        +-- arg1 = nullptr
        +-- arg2 = nam_input_wav.wav
        +-- arg3 = outputFile.wav
        +-- arg4 = input.nam
        +-- arg5 = shared 0x2288-byte buffer
        +-- arg6 = 0  (experimental)
```

The shared buffer is created by the parent process and survives a worker crash. It is initialized to `0xCC`, so the parent can report how many bytes the DLL changed. The buffer is always preserved as:

```text
captured-buffer.bin
```

inside the research work directory.

If the captured buffer begins with `VTSI`, the parent materializes all `0x2288` bytes as `output.clo` and treats that as an experimental success even if the worker crashes immediately afterwards.

The old v0.1 route remains available with:

```powershell
--mode file
```

## Repository layout

```text
NamToCloResearch/
├─ .github/workflows/
│  ├─ build-windows.yml
│  └─ release.yml
├─ docs/
│  └─ RESEARCH_NOTES.md
├─ runtime/
│  └─ README.md
├─ scripts/
│  ├─ setup-runtime.ps1
│  └─ run-probe.ps1
├─ src/
│  ├─ common.cpp
│  ├─ common.hpp
│  └─ main.cpp
├─ tools/
│  └─ compare_clo.py
├─ CMakeLists.txt
├─ LICENSE
├─ THIRD_PARTY.md
└─ README.md
```

## 1. Build with GitHub Actions

Push the repository to GitHub. `.github/workflows/build-windows.yml` builds Windows x64 Release automatically on `windows-latest`.

After the workflow finishes, download the artifact:

```text
NamToCloResearch-windows-x64
```

and extract it anywhere, for example:

```text
C:\CNamToCloTest
```

## 2. Prepare the Ampero runtime

The repository intentionally does **not** redistribute `HTUSBTools.dll` or `nam_input_wav.wav`.

Using your own Ampero II installation:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\scripts\setup-runtime.ps1 -AmperoDir "C:\Program Files\Hotone\Ampero II"
```

The corrected script resolves its destination inside the script body, so it does not suffer the earlier empty `$PSScriptRoot` / `Join-Path` default-parameter bug.

It creates:

```text
runtime\HTUSBTools.dll
runtime\nam_input_wav.wav
```

If you do not want to change PowerShell policy even for the current process, copy those two files manually into `runtime\`.

## 3. Verify the runtime

```powershell
.\NamToClo.exe --check-runtime --verbose
```

Expected exports include:

```text
namConvertClo: present
namConvertCloData: present
getNormalWav: present
initWithWaveDicPath: present
initWithWaveDicPathJson: present
InitDartApiDL: present
```

## 4. Run the v0.2 experiment

Use the same known NAM that already produced `outputFile.wav` in the previous experiment:

```powershell
.\NamToClo.exe ".\modelo.nam" ".\ampero.clo" --keep-temp --verbose
```

`--mode data` is now the default. It can also be written explicitly:

```powershell
.\NamToClo.exe ".\modelo.nam" ".\ampero.clo" --mode data --keep-temp --verbose
```

The console should show the shared-memory hypothesis, for example:

```text
arg1 context      = nullptr
arg2 inputWav     = ...\nam_input_wav.wav
arg3 outputWav    = ...\outputFile.wav
arg4 inputNam     = ...\input.nam
arg5 outputBuffer = shared mapping @ ...
arg6 unknown      = 0 (experimental)
```

The parent reports every observed change to the `0x2288` shared buffer.

### Files to keep after the test

With `--keep-temp`, keep the entire work directory. The important files are:

```text
input.nam
nam_input_wav.wav
outputFile.wav             # if NAM stage succeeds
captured-buffer.bin        # always 0x2288 bytes in data mode
output.clo                 # appears if VTSI is detected/materialized
```

If the worker crashes, the work directory is preserved automatically even without `--keep-temp`.

## 5. How to interpret v0.2

### Best case

Console contains something like:

```text
[parent] shared CLO buffer changed: ... [VTSI detected]
[parent] VTSI buffer materialized as: ...\output.clo
```

and `ampero.clo` is produced. Then run:

```powershell
.\NamToClo.exe --inspect ".\ampero.clo"
```

and compare it with a Valeton CLO generated from the same NAM:

```powershell
python .\tools\compare_clo.py valeton.clo ampero.clo
```

### Worker still crashes, but buffer changed

This is still useful. Send/inspect:

```text
captured-buffer.bin
outputFile.wav
console log
```

The parent preserves the shared buffer after the worker exits, including after `0xC0000409`.

### Buffer stays entirely `0xCC`

Then `namConvertCloData` did not reach the copy into arg5. The next target becomes arg6 / context / the exact point where the CLO stage starts.

## 6. Re-run the old v0.1 path

For comparison:

```powershell
.\NamToClo.exe ".\modelo.nam" ".\legacy.clo" --mode file --keep-temp --verbose
```

This keeps the previous `namConvertClo` behavior unchanged as a control experiment.

## 7. Helper script

```powershell
.\scripts\run-probe.ps1 `
    -Nam ".\modelo.nam" `
    -Output ".\ampero.clo" `
    -Mode data `
    -KeepTemp `
    -VerboseLog
```

## Safety / scope

- The proprietary Hotone binaries are not included.
- The native call is undocumented and experimental.
- Conversion is isolated in a child process because malformed ABI assumptions may trigger process termination.
- This repository is for interoperability/research and is not integrated into GP200 Studio VST.

## Experimental GP-200 length probe (v0.4)

After comparing Ampero and Valeton CLO files generated from the same NAM, the current working model is:

- Ampero: 0x88-byte header + 0x800 float32 coefficients + 0x200-byte tail = 0x2288 bytes.
- Valeton: 0x88-byte header + 0x400 float32 coefficients + 0x200-byte tail = 0x1288 meaningful bytes, padded to 0x2288 on disk.

The analyzed Ampero DLL computes the model-count field using a 2048.0f constant. `--gp200-probe` changes that constant to 1024.0f in the worker's loaded DLL image only.

Run:

```powershell
.\NamToClo.exe ".\modelo.nam" ".\gp200-probe.clo" --mode data --gp200-probe --keep-temp --verbose
```

Then inspect:

```powershell
.\NamToClo.exe --inspect ".\gp200-probe.clo"
```

The key field is `model-field @0x84`. The desired experimental result is `0x400` instead of the normal Ampero `0x800`.

Do **not** load the experimental file into hardware yet. Even if the model field becomes `0x400`, the Ampero serializer still writes its normal `0x2288/0x2200` container sizes. The next step would be to compare the recalculated coefficients against a Valeton CLO from the exact same NAM before adapting the container header/checksum.

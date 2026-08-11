# NamToCloResearch

Experimental Windows x64 research project to determine whether the **NAM → CLO** conversion code already present in the official Ampero II `HTUSBTools.dll` can be called outside the Ampero editor and produce CLO files compatible with the Valeton GP-200.

> Status: **research / ABI probe**. Do not treat the current five-argument call as a fully validated public API yet.

## Goal

The end goal is a standalone command such as:

```powershell
NamToClo.exe input.nam output.clo
```

without launching the Ampero II editor and, if the current hypothesis is correct, without requiring Ampero hardware.

This repository is separate from GP200 Studio VST. It does not modify or integrate with that plugin.

## Why the Ampero path is promising

The supplied Ampero II package gives us unusually strong evidence:

- `HTUSBTools.dll` is a Windows x64 DLL.
- It exports `namConvertClo` and `namConvertCloData` directly.
- Static analysis shows both functions returning `0x2288` (`8840`) — exactly the fixed CLO size observed in the Valeton GP-200 conversion pipeline.
- The Ampero package includes the exact same `nam_input_wav.wav` stimulus hash previously measured in the Valeton editor:
  `9bb6c1b136dfbeb7538a6060499d98c89342b76ec568b76836e36ab98b29aa1a`.
- The DLL contains `VTSI` / `VTSIL` strings.

These facts make reuse of the existing Ampero conversion engine a much better first experiment than reimplementing Valeton's proprietary HTKPA/DSP identification stage.

See [`docs/RESEARCH_NOTES.md`](docs/RESEARCH_NOTES.md) for the confirmed/inferred split.

## Repository layout

```text
NamToCloResearch/
├─ .github/workflows/
│  ├─ build-windows.yml
│  └─ release.yml
├─ docs/
│  └─ RESEARCH_NOTES.md
├─ runtime/
│  └─ README.md              # vendor binaries are deliberately excluded
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

## 1. Create the GitHub repository

Create an empty repository on GitHub, for example `NamToCloResearch`, then upload/push the contents of this folder.

From a local Git shell:

```powershell
git init
git add .
git commit -m "Initial external Ampero NAM-to-CLO research probe"
git branch -M main
git remote add origin https://github.com/YOUR_USER/NamToCloResearch.git
git push -u origin main
```

Do **not** commit `HTUSBTools.dll`, `nam_input_wav.wav`, `.nam`, `.wav` or `.clo` files. `.gitignore` already excludes them.

## 2. Build online with GitHub Actions

The workflow `.github/workflows/build-windows.yml` runs on `windows-latest` and builds an x64 Release executable with the Visual Studio generator.

After pushing:

1. Open the repository on GitHub.
2. Open **Actions**.
3. Choose **Build Windows x64**.
4. Run the workflow manually, or simply push a commit.
5. When the job finishes, download the artifact `NamToCloResearch-windows-x64`.
6. Inside it is a ZIP containing `NamToClo.exe`, docs, scripts and the empty runtime instructions.

GitHub Actions compiles only our source code. It never needs or uploads the proprietary Hotone DLL.

## 3. Optional local build

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 Build Tools or Visual Studio with C++ workload
- CMake 3.24+

Build:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The executable will be:

```text
build\Release\NamToClo.exe
```

Sanity test:

```powershell
.\build\Release\NamToClo.exe --version
.\build\Release\NamToClo.exe --help
```

## 4. Prepare the vendor runtime locally

Use your own legally obtained Ampero II installation/package. The analyzed package stores the files at approximately:

```text
Ampero II\assets\HTUSBTools.dll
Ampero II\data\flutter_assets\assets\wavs\nam_input_wav.wav
```

You can copy them automatically:

```powershell
.\scripts\setup-runtime.ps1 -AmperoDir "D:\Tools\Ampero II"
```

This creates locally:

```text
runtime\HTUSBTools.dll
runtime\nam_input_wav.wav
```

The files remain ignored by Git.

You can also avoid copying them and pass the original Ampero directory directly with `--ampero-dir`.

## 5. First test: verify the runtime without converting

Run:

```powershell
.\build\Release\NamToClo.exe --check-runtime --ampero-dir "D:\Tools\Ampero II" --verbose
```

or, if `runtime/` is populated:

```powershell
.\build\Release\NamToClo.exe --check-runtime --verbose
```

Expected useful output includes:

```text
namConvertClo: present
namConvertCloData: present
getNormalWav: present
initWithWaveDicPath: present
initWithWaveDicPathJson: present
InitDartApiDL: present
```

If `LoadLibraryExW` fails, ensure the Microsoft Visual C++ runtime required by the official Ampero software is installed and try pointing `--dll` directly to the DLL inside a working Ampero installation.

## 6. First real conversion experiment

Use a known NAM file. Prefer a simple path for the very first run.

```powershell
.\build\Release\NamToClo.exe `
    "D:\NamTest\model.nam" `
    "D:\NamTest\ampero.clo" `
    --ampero-dir "D:\Tools\Ampero II" `
    --keep-temp `
    --verbose
```

Or with the helper:

```powershell
.\scripts\run-probe.ps1 `
    -Nam "D:\NamTest\model.nam" `
    -Output "D:\NamTest\ampero.clo" `
    -KeepTemp `
    -VerboseLog
```

### What the executable does

The public process:

1. Finds `HTUSBTools.dll` and `nam_input_wav.wav`.
2. Creates a temporary research directory.
3. Copies the stimulus and NAM to predictable filenames.
4. Launches a child copy of itself in `--worker` mode.
5. The worker loads `HTUSBTools.dll` and resolves `namConvertClo` dynamically.
6. It calls the current five-argument ABI hypothesis with `context = nullptr`.
7. It keeps the process alive while watching for `outputFile.wav` and `output.clo`.
8. An `8840 / 0x2288` byte CLO is considered the primary success condition.
9. The parent copies the result to the requested output path.

If the DLL crashes because the ABI hypothesis is incomplete, the child process contains the failure and the parent reports it.

## 7. Current experimental call

Static analysis directly confirms five arguments and a nullable first argument. The current semantic mapping is still a strong inference:

```cpp
uint32_t namConvertClo(
    void* context,
    const char* inputWavPath,
    const char* outputWavPath,
    const char* namPath,
    const char* cloPath);
```

The probe uses:

```text
context       = nullptr
inputWavPath  = ...\nam_input_wav.wav
outputWavPath = ...\outputFile.wav
namPath       = ...\input.nam
cloPath       = ...\output.clo
```

It intentionally does **not** initialize Dart yet. That is part of the experiment: we want to find out whether Dart is only for progress/callback delivery or is actually required for conversion.

## 8. Interpret the result

### A. `outputFile.wav` and a `0x2288` CLO appear

This is the ideal result. Keep the temp directory and immediately compare the CLO against one generated by the GP-200 editor using the **same NAM**.

Inspect it:

```powershell
.\build\Release\NamToClo.exe --inspect "D:\NamTest\ampero.clo"
```

Compare it:

```powershell
python .\tools\compare_clo.py `
    "D:\NamTest\valeton.clo" `
    "D:\NamTest\ampero.clo"
```

Important fields to record:

- size
- first four bytes / magic
- first 16-32 bytes
- SHA-256
- number and location of differing bytes
- common prefix/suffix

If both files are `0x2288` and structurally close, the next step is a controlled import/upload test on the GP-200.

### B. `outputFile.wav` appears but no CLO appears

This would strongly suggest the NAM inference stage is working but the final clone/encoder stage requires another initialization, callback, resource path or argument adjustment.

Next investigation:

1. ProcMon on `NamToClo.exe` / worker.
2. Check accesses to `Temporary.wav`, `.wav`, `.clo`, JSON/resource paths.
3. Debug the worker in x64dbg at `HTUSBTools!namConvertClo` and the thread it starts.
4. Investigate `initWithWaveDicPath` / `initWithWaveDicPathJson` before adding Dart.

### C. No output WAV and no CLO

Likely possibilities:

- argument semantic order is wrong;
- initialization is required before `namConvertClo`;
- a resource path is required;
- the DLL expects another execution context.

Do not jump directly to reimplementing HTKPA. Capture the worker's file activity and call path first.

### D. Worker crashes with an access violation

That is useful evidence that some part of the ABI/context assumption is wrong. Because the call is isolated, the repository remains a safe place to iterate on the signature.

## 9. Compare Ampero vs Valeton correctly

Use exactly the same NAM:

```text
                    ┌─ GP-200 editor ─> valeton.clo
same model.nam ─────┤
                    └─ NamToClo      ─> ampero.clo
```

Do not require byte-for-byte identity as the first criterion. Different embedded NAM-core versions or normalization details may alter model coefficients. First establish whether both files have the same container/header layout and whether the GP-200 accepts the Ampero-generated result.

## 10. Physical GP-200 compatibility test

Only after an external `0x2288` CLO has been generated and inspected:

1. Preserve the original Valeton-generated CLO and preset backups.
2. Transfer/import the Ampero-generated CLO using the existing separate research tooling.
3. Confirm whether the GP-200 accepts it.
4. Confirm that the resulting clone produces plausible audio.
5. Compare against the Valeton-generated clone from the same NAM.

Do not integrate anything into GP200 Studio VST at this stage.

## 11. GitHub release builds

The included `release.yml` workflow triggers on tags such as:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

It builds Windows x64, creates `NamToCloResearch-windows-x64.zip`, and attaches it to the GitHub release. The archive still contains no proprietary Hotone files.

## Known reference hashes from the analyzed package

```text
HTUSBTools.dll
SHA-256: 5ef8ac398ed0c00ca5d350ddfe8b94d8eefca58c5998c658bb50aecaca257626
Size:    3470336 bytes

nam_input_wav.wav
SHA-256: 9bb6c1b136dfbeb7538a6060499d98c89342b76ec568b76836e36ab98b29aa1a
Size:    12348104 bytes
```

The stimulus hash is particularly significant because it matches the stimulus already observed in the Valeton GP-200 research.

## License and vendor files

Our source code is MIT licensed. Vendor binaries are not.

Read [`THIRD_PARTY.md`](THIRD_PARTY.md) before publishing the repository.

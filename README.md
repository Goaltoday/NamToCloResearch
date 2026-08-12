# NAM to CLO 1.0

Windows GUI utility that converts a Neural Amp Model (`.nam`) into two CLO files:

- `<name>_Ampero_2048.clo` — the 2048-coefficient VTSI generated through Hotone `HTUSBTools.dll`.
- `<name>_GP200_1024.clo` — an experimental GP-200 compact serialization using the 1024-coefficient structure observed in the official Valeton editor.

## What is confirmed

- `namConvertCloData` can generate a 0x2288-byte VTSI without Ampero hardware in the tested runtime.
- The Ampero result uses a 2048-coefficient Block B (`modelField = 0x800`).
- The official GP-200 editor internally creates a 2048 model and then serializes the GP-200 form by keeping Block A plus the first 1024 Block B coefficients, changing the size fields/model count, recalculating CRC16/MODBUS, and zero-padding the physical file to 0x2288 bytes.

## Still experimental

The generated GP-200 1024 file has the observed official GP-200 structure, but the Ampero-generated coefficients are not byte-identical to Valeton's coefficients. Hardware validation on a real GP-200 is still pending.

## Runtime files required

The repository does **not** redistribute Hotone proprietary files. Before running the application, create:

```text
NamToClo.exe
runtime\
  ampero\
    HTUSBTools.dll
    nam_input_wav.wav
```

Copy `HTUSBTools.dll` and `nam_input_wav.wav` from your legally obtained Ampero II installation/package. In the runtime already tested during this research, no additional third-party files were needed beside these two.

## Usage

Double-click `NamToClo.exe`.

1. Load or drag a `.nam` file.
2. Choose an output folder.
3. Click **Convert**.
4. The application generates both CLO files automatically.

If a destination filename already exists, the application creates ` (1)`, ` (2)`, etc. instead of silently overwriting it.

## Build

Requirements:

- Windows x64
- Visual Studio 2022/2026 Build Tools with Desktop C++ workload
- CMake 3.24+

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The executable will normally be at:

```text
build\Release\NamToClo.exe
```

Copy it to a clean folder together with `runtime\ampero\HTUSBTools.dll` and `runtime\ampero\nam_input_wav.wav`.

# Runtime files (not distributed)

This repository intentionally does **not** contain Hotone/Ampero proprietary files.

For local experiments, copy your legally obtained files from your own Ampero II installation/package into this directory:

```text
runtime/
  HTUSBTools.dll
  nam_input_wav.wav
```

The helper script can do this for you:

```powershell
.\scripts\setup-runtime.ps1 -AmperoDir "C:\path\to\Ampero II"
```

Files observed in the analyzed Ampero II package:

- `HTUSBTools.dll`: 3,470,336 bytes; SHA-256 `5ef8ac398ed0c00ca5d350ddfe8b94d8eefca58c5998c658bb50aecaca257626`
- `nam_input_wav.wav`: 12,348,104 bytes; SHA-256 `9bb6c1b136dfbeb7538a6060499d98c89342b76ec568b76836e36ab98b29aa1a`

A different Ampero II release may legitimately have a different DLL hash. Do not replace or redistribute vendor files through this repository.

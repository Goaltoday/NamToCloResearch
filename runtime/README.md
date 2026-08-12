# Runtime files (not redistributed)

This repository does not contain proprietary vendor binaries or WAV assets.
Keep all third-party runtime files below this directory:

```text
runtime/
  ampero/
    HTUSBTools.dll
    nam_input_wav.wav
  sonicake/
    5868USB.dll
    nam_input_wav.wav
    mfc140.dll          # if shipped/needed by your Sonicake package
    mfc140u.dll         # if shipped/needed
    msvcp140.dll        # if shipped/needed
    vcruntime140.dll    # if shipped/needed
```

With that layout, no vendor paths are needed on the command line.

Simplest full experiment:

```powershell
.\NamToClo.exe --cross-runtime ".\modelo.nam"
```

Output defaults to `cross-runtime-results\`.

The CLI still supports original application roots with `--ampero-dir` / `--sonicake-dir`, or explicit `--dll` / `--stimulus` paths.

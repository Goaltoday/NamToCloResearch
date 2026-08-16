# NamToClo v2.6.7-DIAG3

Diagnostic build only. Conversion/refinement DSP is unchanged from v2.6.7.

## Extra diagnostics

In addition to the DIAG2 crash log and minidump, DIAG3 records:

- process token elevation state and integrity level;
- current working directory and TEMP/TMP/LOCALAPPDATA/APPDATA/PROGRAMDATA/USERPROFILE;
- write probes for relevant directories;
- FNV-1a hashes for the exact DLL, staged input WAV and staged NAM;
- RIFF/WAVE header fields for the exact stimulus passed to HTUSBTools;
- preserved copies of the staged input as `*_DIAG_INPUT.wav` and `*_DIAG_INPUT.nam`.

Run the same conversion once as a normal user and, if useful, once elevated. Compare the two diagnostic logs and preserved WAV/NAM files.

If a crash occurs, also keep `*_HTUSBTools_CRASH.dmp`.

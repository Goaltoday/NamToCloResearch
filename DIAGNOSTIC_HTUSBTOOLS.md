# HTUSBTools diagnostic build

This build is functionally based on v2.6.7. It adds persistent logging around every HTUSBTools worker stage.

For each conversion it creates, in the selected output folder:

`<NAM name>_HTUSBTools_DIAGNOSTIC.log`

The log is intentionally kept both on success and failure. It records the first/base HTUSBTools pass and, when matched-input refinement with an external WAV is enabled, the second HTUSBTools pass.

Key markers:

- WORKER [1]: worker process started
- WORKER [2]-[4]: DLL load and export resolution
- WORKER [5]: shared buffer mapping
- WORKER [6]-[7]: A2 detection / Full submodel preparation
- WORKER [8]: immediately before namConvertCloData
- WORKER [9]: immediately after namConvertCloData
- WORKER [10]: output-buffer observation
- WORKER [11]-[14]: output WAV and cleanup / FreeLibrary

If Windows terminates the worker with 0xC0000005, the last completed marker identifies the stage in which the access violation occurred.

## Test matrix

Use the same NAM on the affected PC:

1. Refinement OFF.
2. Refinement ON, reference WAV blank.
3. Refinement ON, external matched-input WAV selected.
4. If the NAM is A2, optionally repeat test 1 with a non-A2 NAM.

Send the generated diagnostic log(s) back for analysis.

# Research notes — active baseline v0.8.0

## Confirmed

- Ampero `HTUSBTools.dll` exports `namConvertCloData` and returns `0x2288` on the successful data path.
- The working ABI used by this tool is:
  - arg1: `nullptr` context
  - arg2: stimulus WAV path
  - arg3: output WAV path
  - arg4: NAM path
  - arg5: caller-owned `0x2288` destination buffer
  - arg6: fixed `0`
- The normal VTSI shape is physical/declared `0x2288`, payload `0x2200`, Block A 128 float32 and Block B 2048 float32.
- The GP-200 internal large VTSI also uses 2048 Block-B floats.
- GP-200 compaction is serialization only: preserve Block A + first 1024 Block-B floats; declared `0x1288`, payload `0x1200`, model field `0x400`, zero physical tail to `0x2288`, recalculate CRC16/MODBUS.
- Ampero and Sonicake produced byte-identical VTSI for tested NAM inputs.
- Sonicake `cloConvertSampleRate` was successfully tested; it did not remove the Valeton/Ampero coefficient difference.
- Patching Ampero's internal 2048 constant to 1024 recalculates a different model and does not reproduce Valeton.

## Discarded probes

The following are intentionally removed from the active code:

- legacy `namConvertClo` file-mode call;
- arg6 sweep;
- 2048->1024 DSP constant patch;
- 44.1->48 kHz VTSI-stage instruction patch;
- Sonicake provider and sample-rate matrices.

They remain historical research conclusions, not production paths.

## Current question

Why does Valeton's **large 2048-coefficient identification result** differ slightly from the byte-identical Ampero/Sonicake result when the NAM response WAV is effectively the same?

Dynamic work in GP-200.exe has already located real intermediate float-processing code in the path before VTSI compaction. Future probes should target identification parameters/state and the first buffer that matches the final internal large VTSI, rather than revisiting container serialization.

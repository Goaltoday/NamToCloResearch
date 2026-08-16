# v2.6.3 A2 Full/Lite verification

For every A2 `SlimmableContainer`, conversion writes three diagnostics beside the normal outputs:

- `<name>_A2_FULL_EXTRACTED.nam`: the embedded model with the highest `max_value`. This exact model is supplied to `HTUSBTools.dll::namConvertCloData`.
- `<name>_A2_LITE_EXTRACTED.nam`: the embedded model with the lowest `max_value`.
- `<name>_A2_SUBMODEL_DIAGNOSTICS.txt`: every discovered `max_value`, byte size and FNV-1a 64-bit hash, plus the exact selected model.

## Verification test

1. Convert the original A2 NAM once with v2.6.3.
2. Open the original A2 NAM in a NAM player that exposes A2 Slim/Full selection. Set it to Full.
3. Open `_A2_FULL_EXTRACTED.nam` in the same player. It is a standalone embedded model and needs no Slim control.
4. Feed exactly the same dry WAV/stimulus and use the same input/output gain. Record both renders.
5. They should null or be numerically extremely close if highest `max_value` really is Full.
6. Repeat with original A2 set to Lite/Slim=0 versus `_A2_LITE_EXTRACTED.nam`.
7. Send the diagnostics TXT and the four rendered WAVs back for correlation/NMSE analysis if desired.

The converter itself does not infer the result of this acoustic test: it records exactly what it extracts and exactly which submodel it sends to HTUSBTools.

# v2.6.1 - NAM A2 Full mode

## Purpose

Ensure A2 `SlimmableContainer` models are rendered as the **Full** model rather than the smallest Lite/Slim model before `namConvertCloData` creates `outputFile.wav` and the CLO.

## Confirmed from HTUSBTools.dll static analysis

- The DLL exports `convertNamToNamb` and `convertNamToNambWithSlim`.
- `convertNamToNamb` forces the slim factor to `0.0`.
- `convertNamToNambWithSlim` forwards the caller supplied floating-point slim factor.
- `SlimmableContainer` selection walks ascending thresholds. Factor `0.0` selects the first/smallest submodel; factor `1.0` selects the last/full submodel.
- `namConvertCloData` argument 6 is stored by the wrapper but is not read by the conversion worker branch inspected, so changing arg6 is not a supported Full/Slim selector.
- The DLL contains native `.namb` handling.

## v2.6.1 behavior

For a staged `.nam` containing `SlimmableContainer`, the hidden worker now:

1. resolves `convertNamToNambWithSlim`;
2. calls it with factor `1.0`;
3. locates the generated `.namb`;
4. passes that Full `.namb` model to the otherwise unchanged `namConvertCloData` path.

For non-slimmable NAMs, the input model is passed through unchanged.

There is deliberately **no fallback to the original A2 NAM** if Full preprocessing fails, because falling back could silently use the Lite/Slim=0 model and defeat the purpose of this branch.

## Runtime validation requested

Use an A2 NAM whose Full and Slim versions are audibly/measurably different. After conversion, compare `outputFile.wav` against external renders of the same stimulus in Full and Slim. The v2.6.1 output should match Full.

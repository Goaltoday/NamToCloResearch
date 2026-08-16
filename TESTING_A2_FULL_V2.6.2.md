# Testing A2 Full v2.6.2

## Why v2.6.1 crashed
v2.6.1 converted a SlimmableContainer `.nam` to `.namb` with
`convertNamToNambWithSlim(..., 1.0)` and then passed that `.namb` path to
`namConvertCloData`. On the tested HTUSBTools.dll that path raises Windows
exception `0xC0000005`.

Static analysis of `namConvertCloData` confirms its six-argument ABI but does
not show a slim-factor input. Its fifth and sixth arguments are stored in the
conversion context before the clone routine is started; the sixth remains the
existing fixed zero.

## v2.6.2 strategy
A2 `SlimmableContainer` NAM files contain complete NAM models inside
`config.submodels[]`. v2.6.2 selects the entry with the highest `max_value`
(normally `1.0`) and copies its embedded `model` object verbatim to a temporary
`.nam` file. That standalone normal NAM is passed to the unchanged
`namConvertCloData` path.

This avoids `.namb` entirely while forcing conversion of the embedded Full
submodel rather than the first/Lite submodel.

For the 5150 A2 test file used during development:
- top-level architecture: `SlimmableContainer`
- submodel max values: `0.5`, `1.0`
- selected Full entry: `max_value = 1.0`
- embedded architecture: `WaveNet`
- embedded sample rate: `48000`

## Expected UI status
`A2 SlimmableContainer detected: extracting embedded FULL submodel (max_value 1.0)...`

Normal non-A2 NAM files are unchanged.

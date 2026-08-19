# FINAL9 – HTUSBTools.dll cross-audit

## Scope

This revision cross-audits the independent trainer against the proprietary
`HTUSBTools.dll` supplied from the original conversion path. CLO files are not
used to derive DSP steps; they are validation artifacts only.

Analyzed DLL SHA-256:
`5ef8ac398ed0c00ca5d350ddfe8b94d8eefca58c5998c658bb50aecaca257626`

Relevant conversion chain recovered statically:

- export `namConvertClo` RVA 0xCA830
- export `namConvertCloData` RVA 0xCA600
- worker RVA 0x4C980
- NAM render/preprocess RVA 0x4A990
- trainer wrapper RVA 0x4BCB0
- core trainer RVA 0x9F500
- optimizer RVA 0x9B890
- spectral estimator RVA 0x98930
- minimum-phase wrapper RVA 0x96F70
- minimum-phase spectrum builder RVA 0x97960

## Confirmed correction in FINAL9

The previous FINAL8 audit incorrectly concluded that the minimum-phase
log-spectrum reversal did not run when input and output lengths are equal.
`HTUSBTools.dll` disproves that conclusion directly.

At `0x180097a65`, equal lengths skip only the interpolation block. Execution
then reaches `0x180097ae7`, where the first half of the working log-magnitude
array is overwritten from the mirrored end:

```
for (i = 0; i < N/2; ++i)
    work[i] = work[N - 1 - i];
```

For the symmetric magnitude vector built by the wrapper this changes the
positive-side sequence from roughly
`m0,m1,...,mNyquist` to `m1,m2,...,mNyquist` before the cepstral DFT. This is
not an algebraically neutral operation. It affects every reconstruction of A
and B throughout initialization/optimization and the final B correction.

FINAL9 restores this operation literally. No CLO-derived gain, manual delay,
or model-specific correction is present.

## Other major points independently reconfirmed from this DLL

- NAM render scale = 0.3100000023841858f.
- NAM processing block = 1024.
- Trainer wrapper initializes at 48000 Hz.
- Optimizer updates `Bstate *= C` and `Astate /= C`.
- Fresh B estimate is divided by the already-updated B state.
- Phase order/count is 23–28 s ×3, 6–21 s ×2, 30–50 s ×5.
- B is multiplied by exactly 4.0 before final serialization SRC.
- No final scalar is applied to A.

## Why this correction is materially different from FINAL4–FINAL8

Most prior changes adjusted numerically equivalent implementations and had
almost no effect on the produced CLO. The operation above was actually absent
from FINAL8 and changes the magnitude sequence supplied to every minimum-phase
reconstruction. It is therefore a central trainer semantic discrepancy, not a
last-bit implementation detail.

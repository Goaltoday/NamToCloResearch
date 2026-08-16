# NAM to CLO v2.6.6.1

## v2.6.6.1

- Refinement can optionally use a user-selected WAV as the Tone Match target. The final 20 seconds of that WAV are used; leaving the field blank keeps the NAM render as target.
- Refinement comparison diagnostics are no longer shown.
- With refinement enabled, the output folder receives only the normal Ampero B2048 CLO, the normal GP-200 B1024 CLO, and the refined GP-200 B1024 CLO.
- Intermediate BEST/candidate CLOs and `auto_tonematch_ir.wav` are no longer exported.


Clean baseline containing only the conversion paths and conclusions currently in use.

## Current conversion flow

1. Build the selected 44.1 kHz stimulus/tail.
2. Render the NAM and generate the intermediate Ampero-style CLO with `HTUSBTools.dll`.
3. Keep the Ampero Block B at 2048 taps while optional post-processing is performed.
4. Optionally apply either the existing manual Corrective IR or the current VST-style Block-B refinement.
5. Compact the final CLO to the GP-200 1024-tap Block B format.

## NAM A2: always use Full

For an A2 `SlimmableContainer`, the converter parses `config.submodels[]`, selects the embedded model with the highest `max_value`, writes it only to the temporary work directory as a standard `.nam`, and supplies that model to the unchanged `namConvertCloData` path.

This behavior was verified with `Modelo4.nam`:

- `max_value = 0.5`: 3-channel WaveNet, Lite.
- `max_value = 1.0`: 8-channel WaveNet, Full.
- The original A2 rendered in Full correlated about 0.9999 with the extracted highest-`max_value` model.
- The Lite render differed clearly (about 0.9947 correlation to the Full/original-Full render).

The temporary diagnostic exports from v2.6.3 (`*_A2_FULL_EXTRACTED.nam`, `*_A2_LITE_EXTRACTED.nam`, and `*_A2_SUBMODEL_DIAGNOSTICS.txt`) have been removed. They are no longer needed in normal use.

Non-slimmable NAM files continue through the original path unchanged.

## Corrective IR

The existing Corrective IR implementation is unchanged. A selected WAV is convolved into the intermediate 2048-tap Block B before final GP-200 compaction.

## Current automatic refinement

The optional refiner modifies Block B only. PRE, Block A, P/K and POST are not changed.

Current flow:

- SOURCE: official CLO render.
- TARGET: NAM render generated for the same conversion.
- Analysis: final 20 seconds, SOURCE_latest_19-style robust spectrum comparison.
- Tone curve: RAW `TARGET - SOURCE`, Smooth 5%.
- Output: 2048-sample minimum-phase `auto_tonematch_ir.wav`.
- Application: the generated WAV is applied through the existing Corrective IR code path.

Important limitation: this is currently **VST-style**, not an exact replica of the VST Tone Match export. `SOURCE_latest_19` uses a 1024-sample SolverV1 export path with a 2048-point internal FFT, crest optimisation and peak-normalised PCM24 export. The current converter still generates a 2048-sample minimum-phase IR with a 4096-point internal FFT and does not yet reproduce those final VST export stages exactly.

## Runtime

`HTUSBTools.dll` is proprietary and is not redistributed by this project. Place the required DLL in the runtime location expected by the application.

## Build

The project targets Windows and is built with CMake/MSVC. See `CMakePresets.json` and the runtime setup script for the existing build/runtime layout.

### Refinement level handling

Automatic refinement uses 0 dB post-correction gain after Block B RMS normalization. Manual Corrective IR retains the historical -6 dB post-correction gain.

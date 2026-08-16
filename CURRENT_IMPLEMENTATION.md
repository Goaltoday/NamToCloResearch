# Current implementation decisions — v2.6.4

- A2 `SlimmableContainer`: use the embedded submodel with the highest `max_value`; verified as Full in the tested A2 model.
- Do not export A2 verification/diagnostic files during normal conversion.
- Keep normal NAM conversion behavior unchanged.
- Keep the existing Corrective IR path unchanged.
- Work on Ampero Block B 2048 first and compact to GP-200 Block B 1024 only at the end.
- Current automatic refinement is Block-B-only and uses the final 20 s with SOURCE_latest_19-style Tone Match analysis.
- Do not describe the current automatic refinement as an exact VST export replica; exact 1024-sample SolverV1 / crest-optimisation / PCM24 replication remains separate future work.

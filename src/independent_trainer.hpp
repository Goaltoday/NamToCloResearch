#pragma once

#include "conversion.hpp"

namespace ntc {

struct IndependentTrainerConfig {
    // NATIVE15 is the frozen reverse-engineered baseline.  The Independent
    // tab now layers experiments on top of that baseline without touching the
    // Current/HTUSBTools path or the GP-200 uploader.
    double namTargetScale = 0.31;
    int blockSize = 1024;

    // Experimental phase 1: after producing the normal GP-200-compatible CLO,
    // render a deterministic sine matrix through both the NAM target and the
    // exact serialized GP-200 CLO and write a harmonic fingerprint CSV.
    // This is diagnostic only: it does not alter A, B, P/K, PRE or POST.
    bool generateHarmonicReport = true;
};

// Independent / Experimental NAM -> CLO path.  NATIVE15 remains the conversion
// baseline; experimental phases are added here and must remain GP-200-playable.
// No GP-200/Ampero executable or proprietary DLL is loaded.
ConversionResult convertNamToBothIndependent(const fs::path& inputNam,
                                             const fs::path& outputDirectory,
                                             StimulusConfig stimulus = {},
                                             IndependentTrainerConfig trainer = {},
                                             const StatusCallback& status = {});

BatchConversionResult convertNamFolderIndependent(const fs::path& inputDirectory,
                                                   const fs::path& outputDirectory,
                                                   StimulusConfig stimulus = {},
                                                   IndependentTrainerConfig trainer = {},
                                                   const StatusCallback& status = {});

} // namespace ntc

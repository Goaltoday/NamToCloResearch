#pragma once

#include "conversion.hpp"

namespace ntc {

struct IndependentTrainerConfig {
    // Kept for source/API compatibility with earlier native builds. The
    // EXE-audited conversion path is fixed to 0.31f and 1024 samples exactly;
    // independent_trainer.cpp intentionally ignores overrides of these fields.
    double namTargetScale = 0.31;
    int blockSize = 1024;
};

// Independent NAM -> CLO path. No GP-200/Ampero executable or proprietary DLL
// is loaded. NAM rendering is statically compiled from NeuralAmpModelerCore;
// CLO training/serialization is implemented in this project.
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

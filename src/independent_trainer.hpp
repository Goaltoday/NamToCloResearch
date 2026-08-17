#pragma once

#include "conversion.hpp"

namespace ntc {

struct IndependentTrainerConfig {
    // Reconstructed Valeton baseline constants. Kept explicit so future
    // experiments can fork this backend without touching the legacy/DLL path.
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

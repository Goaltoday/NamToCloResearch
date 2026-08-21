#pragma once

#include "conversion.hpp"

namespace ntc {

struct IndependentTrainerConfig {
    // Official converter NAM block size. The reconstructed target scale is
    // fixed in the native implementation and is not user-configurable.
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

#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace ntc {
namespace fs = std::filesystem;

struct CloRefineConfig {
    bool enabled = false;
    int passes = 4;
};

struct CloRefineStats {
    bool improved = false;
    double originalNmse = 0.0;
    double refinedNmse = 0.0;
    double improvementPercent = 0.0;

    double originalStimulusNmse = 0.0;
    double refinedStimulusNmse = 0.0;
    double stimulusImprovementPercent = 0.0;

    double originalTailNmse = 0.0;
    double refinedTailNmse = 0.0;
    double tailImprovementPercent = 0.0;

    // Full-render secondary metrics. Spectral error is mean squared dB error
    // across 48 logarithmic bands (30 Hz..18 kHz). Envelope error is mean
    // squared dB RMS error in 2048-sample windows.
    double originalSpectralError = 0.0;
    double refinedSpectralError = 0.0;
    double spectralImprovementPercent = 0.0;
    double originalEnvelopeError = 0.0;
    double refinedEnvelopeError = 0.0;
    double envelopeImprovementPercent = 0.0;

    // One calibration derived from the ORIGINAL CLO only and then frozen for
    // every candidate. It is intentionally not re-fitted during optimisation.
    double outputScale = 1.0;

    float pPosBefore = 0.0f, pNegBefore = 0.0f, kPosBefore = 0.0f, kNegBefore = 0.0f;
    float pPosAfter = 0.0f, pNegAfter = 0.0f, kPosAfter = 0.0f, kNegAfter = 0.0f;
};

using RefineStatusCallback = std::function<void(const std::wstring&)>;

// Experimental full-length P/K refiner. PRE/A/POST/B stay fixed and only
// Ppos/Pneg/Kpos/Kneg are optimised. The complete conversion render is used
// (normally 50 s stimulus + 20 s tail). Output level is calibrated ONCE from
// the original CLO and frozen. Candidates are accepted only when temporal NMSE,
// logarithmic-band spectral error and RMS-envelope error all do not worsen.
// This prevents a lower time-domain NMSE from hiding worse tone or dynamics.
bool refineCloPk(const fs::path& inputClo2048,
                 const fs::path& stimulusWav,
                 const fs::path& targetWav,
                 const fs::path& outputClo2048,
                 const CloRefineConfig& config,
                 CloRefineStats& stats,
                 std::string& error,
                 const RefineStatusCallback& status = {});

} // namespace ntc

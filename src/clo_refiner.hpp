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

    // Full-render secondary metrics. Spectral error is a multi-resolution
    // STFT loss (FFT 512/2048/8192, spectral convergence + log magnitude).
    // Envelope error is mean absolute dB RMS mismatch at 256/2048/8192 samples.
    double originalSpectralError = 0.0;
    double refinedSpectralError = 0.0;
    double spectralImprovementPercent = 0.0;
    double originalEnvelopeError = 0.0;
    double refinedEnvelopeError = 0.0;
    double envelopeImprovementPercent = 0.0;

    // One calibration derived from the ORIGINAL CLO only and then frozen for
    // every candidate. It is intentionally not re-fitted during optimisation.
    double outputScale = 1.0;

    // v1.9.7 candidate audition: keep the best point found by the free search even
    // when the final safety gate rejects it. This avoids reporting a misleading
    // 0% across all metrics just because the accepted output falls back to the
    // official CLO.
    bool searchedCandidateAccepted = false;
    double searchedComposite = 1.0;
    double searchedCompositeImprovementPercent = 0.0;
    double searchedNmse = 0.0;
    double searchedNmseImprovementPercent = 0.0;
    double searchedStimulusNmse = 0.0;
    double searchedStimulusImprovementPercent = 0.0;
    double searchedTailNmse = 0.0;
    double searchedTailImprovementPercent = 0.0;
    double searchedSpectralError = 0.0;
    double searchedSpectralImprovementPercent = 0.0;
    double searchedEnvelopeError = 0.0;
    double searchedEnvelopeImprovementPercent = 0.0;
    float searchedPPos = 0.0f, searchedPNeg = 0.0f, searchedKPos = 0.0f, searchedKNeg = 0.0f;
    std::string searchedDecisionReason;

    float pPosBefore = 0.0f, pNegBefore = 0.0f, kPosBefore = 0.0f, kNegBefore = 0.0f;
    float pPosAfter = 0.0f, pNegAfter = 0.0f, kPosAfter = 0.0f, kNegAfter = 0.0f;
};

using RefineStatusCallback = std::function<void(const std::wstring&)>;

// Experimental full-length P/K refiner. PRE/A/POST/B stay fixed and only
// Ppos/Pneg/Kpos/Kneg are optimised. The complete conversion render is used
// (normally 50 s stimulus + 20 s tail). Output level is calibrated ONCE from
// the original CLO and frozen. The optimiser minimises a combined full-render research loss (temporal NMSE,
// multi-resolution STFT, and multi-scale RMS envelope) without hard guards on
// intermediate steps. Strict safety guards are applied only to the FINAL
// candidate, preventing tonal/dynamic regressions while avoiding local traps.
bool refineCloPk(const fs::path& inputClo2048,
                 const fs::path& stimulusWav,
                 const fs::path& targetWav,
                 const fs::path& outputClo2048,
                 const fs::path& bestClo2048,
                 const CloRefineConfig& config,
                 CloRefineStats& stats,
                 std::string& error,
                 const RefineStatusCallback& status = {});

} // namespace ntc

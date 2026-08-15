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

    // v2.1 input-referenced spectral-profile error. Unlike MR-STFT, this
    // compares a log-frequency transfer-magnitude curve derived from the
    // known stimulus and output, so Block-A changes cannot improve the broad
    // audio loss while moving the static spectral contour away from the NAM.
    double originalResponseSpectralError = 0.0;
    double refinedResponseSpectralError = 0.0;
    double responseSpectralImprovementPercent = 0.0;

    // One calibration derived from the ORIGINAL CLO only and then frozen for
    // every candidate. It is intentionally not re-fitted during optimisation.
    double outputScale = 1.0;

    // v2.0 A+P/K search: keep the best feasible point found inside
    // global and low/mid/high temporal-error limits. This avoids reporting a misleading
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
    double searchedResponseSpectralError = 0.0;
    double searchedResponseSpectralImprovementPercent = 0.0;

    // P/K-specific level-conditioned temporal errors. Windows are classified
    // from the actual stimulus RMS into low/mid/high excitation groups. This
    // prevents the optimiser from trading a closer average spectrum for an
    // audibly wrong amount of saturation.
    double originalLowLevelNmse = 0.0, originalMidLevelNmse = 0.0, originalHighLevelNmse = 0.0;
    double searchedLowLevelNmse = 0.0, searchedMidLevelNmse = 0.0, searchedHighLevelNmse = 0.0;
    double searchedLowLevelImprovementPercent = 0.0;
    double searchedMidLevelImprovementPercent = 0.0;
    double searchedHighLevelImprovementPercent = 0.0;
    double searchedLevelBalancedImprovementPercent = 0.0;
    float searchedPPos = 0.0f, searchedPNeg = 0.0f, searchedKPos = 0.0f, searchedKNeg = 0.0f;
    std::string searchedDecisionReason;

    float pPosBefore = 0.0f, pNegBefore = 0.0f, kPosBefore = 0.0f, kNegBefore = 0.0f;
    float pPosAfter = 0.0f, pNegAfter = 0.0f, kPosAfter = 0.0f, kNegAfter = 0.0f;
};

using RefineStatusCallback = std::function<void(const std::wstring&)>;

// Experimental v2.2 Block-B-only spectral refiner. PRE, A, P/K and POST stay fixed.
// B is iteratively reshaped in frequency while preserving its phase as far as
// possible. The optimisation target is the direct NAM-vs-CLO output spectrum
// over the first 50 s stimulus; the 20 s tail and temporal metrics are reported
// only as diagnostics. Output level is calibrated once from the original CLO.
bool refineCloBOnly(const fs::path& inputClo2048,
                 const fs::path& stimulusWav,
                 const fs::path& targetWav,
                 const fs::path& outputClo2048,
                 const fs::path& bestClo2048,
                 const CloRefineConfig& config,
                 CloRefineStats& stats,
                 std::string& error,
                 const RefineStatusCallback& status = {});

} // namespace ntc

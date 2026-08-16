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

    // Final-20-s VST-style Tone Match spectral error.
    double originalResponseSpectralError = 0.0;
    double refinedResponseSpectralError = 0.0;
    double responseSpectralImprovementPercent = 0.0;

    // One calibration derived from the ORIGINAL CLO only and then frozen for
    // every candidate. It is intentionally not re-fitted during optimisation.
    double outputScale = 1.0;

    // Candidate metrics used by the current Block-B Tone Match validation.
    bool searchedCandidateAccepted = false;
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

    // Level-conditioned temporal errors (low/mid/high stimulus excitation).
    double originalLowLevelNmse = 0.0, originalMidLevelNmse = 0.0, originalHighLevelNmse = 0.0;
    double searchedLowLevelNmse = 0.0, searchedMidLevelNmse = 0.0, searchedHighLevelNmse = 0.0;
    double searchedLowLevelImprovementPercent = 0.0;
    double searchedMidLevelImprovementPercent = 0.0;
    double searchedHighLevelImprovementPercent = 0.0;
    std::string searchedDecisionReason;

};

using RefineStatusCallback = std::function<void(const std::wstring&)>;

// Current VST-style CAB Tone Match refinement on the final 20 seconds.
// Generates auto_tonematch_ir.wav (2048-sample minimum-phase), then applies it through
// the existing Corrective IR implementation. The analysis matches SOURCE_latest_19 style,
// but the exported solver is not yet an exact replica of the VST 1024-sample export path.
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

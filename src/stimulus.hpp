#pragma once

#include <filesystem>
#include <string>

namespace ntc {

namespace fs = std::filesystem;

struct RuntimePaths;

enum class StimulusMode {
    Legacy = 0,
    CleanMono,
    CleanDualMono,
    DistMono,
    DistDualMono
};

const wchar_t* stimulusModeDisplayName(StimulusMode mode);

// Builds the exact WAV that will be handed to namConvertCloData.
// Legacy mode is intentionally a byte-for-byte copy of nam_input_wav.wav.
// Sound Clone test modes concatenate the 50 s Clean/Dist source with the
// 20 s PresetAudio source, without gain changes or sample processing.
bool buildStimulus(const RuntimePaths& runtime,
                   StimulusMode mode,
                   const fs::path& destination,
                   std::string& error);

} // namespace ntc

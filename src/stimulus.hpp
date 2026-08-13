#pragma once

#include <filesystem>
#include <string>

namespace ntc {

namespace fs = std::filesystem;

struct RuntimePaths;

enum class StimulusMode {
    Legacy = 0,
    Clean,
    Dist,
    Custom
};

enum class TailMode {
    PresetAudio = 0,
    RecordedAudio
};

struct StimulusConfig {
    StimulusMode mode = StimulusMode::Legacy;
    TailMode tailMode = TailMode::PresetAudio;
    fs::path customStimulus;
    fs::path recordedAudio;
};

const wchar_t* stimulusModeDisplayName(StimulusMode mode);
const wchar_t* tailModeDisplayName(TailMode mode);

// Builds the exact WAV that will be handed to namConvertCloData.
// Legacy mode is intentionally a byte-for-byte copy of nam_input_wav.wav.
// Sound Clone modes concatenate a 50 s base stimulus (Clean, Dist or a user
// Custom WAV) with either the original 20 s PresetAudio or a user recording,
// then append the 600 zero samples observed in the official Sound Clone
// inputSignal.wav.
//
// Custom stimulus audio is adapted automatically to mono PCM16 44.1 kHz and
// exactly 50.000 seconds: longer files are trimmed and shorter files are
// zero-padded. No gain normalization is applied.
//
// Recorded audio is adapted automatically to an exact 20.000-second tail:
// longer files are trimmed and shorter files are zero-padded. Channel count,
// supported PCM/float sample format and sample rate are also adapted to mono
// PCM16 44.1 kHz. No gain normalization or other DSP is applied.
bool buildStimulus(const RuntimePaths& runtime,
                   const StimulusConfig& config,
                   const fs::path& destination,
                   std::string& error);

} // namespace ntc

#include "stimulus.hpp"
#include "conversion.hpp"
#include "common.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace ntc {
namespace {

constexpr std::uint32_t kExpectedSampleRate = 44100;
constexpr std::uint16_t kExpectedBitsPerSample = 16;
constexpr std::uint16_t kExpectedSourceChannels = 1;
constexpr std::uint64_t kBaseFrames = 50ull * kExpectedSampleRate;
constexpr std::uint64_t kTailFrames = 20ull * kExpectedSampleRate;
constexpr std::size_t kSoundClonePaddingFrames = 600;

struct Pcm16MonoWav {
    std::vector<std::int16_t> samples;
};

std::uint16_t readLe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0])
         | static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t readLe32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

void writeLe16(std::ostream& out, std::uint16_t value) {
    const std::array<char, 2> b = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu)
    };
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
}

void writeLe32(std::ostream& out, std::uint32_t value) {
    const std::array<char, 4> b = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu),
        static_cast<char>((value >> 24) & 0xFFu)
    };
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
}

bool readPcm16Mono44100(const fs::path& path,
                        std::uint64_t expectedFrames,
                        Pcm16MonoWav& wav,
                        std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open stimulus source WAV: " + pathToUtf8(path);
        return false;
    }

    std::array<std::uint8_t, 12> riff{};
    in.read(reinterpret_cast<char*>(riff.data()), static_cast<std::streamsize>(riff.size()));
    if (in.gcount() != static_cast<std::streamsize>(riff.size())
        || std::memcmp(riff.data(), "RIFF", 4) != 0
        || std::memcmp(riff.data() + 8, "WAVE", 4) != 0) {
        error = "Not a valid RIFF/WAVE file: " + pathToUtf8(path);
        return false;
    }

    bool haveFmt = false;
    bool haveData = false;
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::vector<std::uint8_t> data;

    while (in && !(haveFmt && haveData)) {
        std::array<std::uint8_t, 8> chunkHeader{};
        in.read(reinterpret_cast<char*>(chunkHeader.data()), static_cast<std::streamsize>(chunkHeader.size()));
        if (in.gcount() == 0) break;
        if (in.gcount() != static_cast<std::streamsize>(chunkHeader.size())) {
            error = "Truncated WAV chunk header: " + pathToUtf8(path);
            return false;
        }

        const std::uint32_t chunkSize = readLe32(chunkHeader.data() + 4);
        const bool isFmt = std::memcmp(chunkHeader.data(), "fmt ", 4) == 0;
        const bool isData = std::memcmp(chunkHeader.data(), "data", 4) == 0;

        if (isFmt) {
            if (chunkSize < 16) {
                error = "Invalid fmt chunk in WAV: " + pathToUtf8(path);
                return false;
            }
            std::vector<std::uint8_t> fmt(chunkSize);
            in.read(reinterpret_cast<char*>(fmt.data()), static_cast<std::streamsize>(fmt.size()));
            if (in.gcount() != static_cast<std::streamsize>(fmt.size())) {
                error = "Truncated fmt chunk in WAV: " + pathToUtf8(path);
                return false;
            }
            audioFormat = readLe16(fmt.data());
            channels = readLe16(fmt.data() + 2);
            sampleRate = readLe32(fmt.data() + 4);
            bitsPerSample = readLe16(fmt.data() + 14);
            haveFmt = true;
        } else if (isData) {
            data.resize(chunkSize);
            if (!data.empty()) {
                in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
                if (in.gcount() != static_cast<std::streamsize>(data.size())) {
                    error = "Truncated data chunk in WAV: " + pathToUtf8(path);
                    return false;
                }
            }
            haveData = true;
        } else {
            in.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
            if (!in) {
                error = "Invalid WAV chunk size: " + pathToUtf8(path);
                return false;
            }
        }

        if ((chunkSize & 1u) != 0u) {
            in.seekg(1, std::ios::cur);
        }
    }

    if (!haveFmt || !haveData) {
        error = "WAV is missing fmt or data chunk: " + pathToUtf8(path);
        return false;
    }
    if (audioFormat != 1 || channels != kExpectedSourceChannels
        || sampleRate != kExpectedSampleRate || bitsPerSample != kExpectedBitsPerSample) {
        error = "Expected mono PCM16 44.1 kHz WAV: " + pathToUtf8(path);
        return false;
    }
    if ((data.size() % sizeof(std::int16_t)) != 0u) {
        error = "PCM16 WAV has an odd data size: " + pathToUtf8(path);
        return false;
    }

    const std::uint64_t frames = data.size() / sizeof(std::int16_t);
    if (frames != expectedFrames) {
        error = "Unexpected duration for " + pathToUtf8(path)
              + ". Expected exactly " + std::to_string(expectedFrames)
              + " samples, got " + std::to_string(frames) + ".";
        return false;
    }

    wav.samples.resize(static_cast<std::size_t>(frames));
    for (std::size_t i = 0; i < wav.samples.size(); ++i) {
        const std::uint16_t raw = readLe16(data.data() + i * 2);
        wav.samples[i] = static_cast<std::int16_t>(raw);
    }
    return true;
}

bool writePcm16Wav(const fs::path& path,
                   const std::vector<std::int16_t>& monoSamples,
                   bool dualMono,
                   std::string& error) {
    const std::uint16_t channels = dualMono ? 2 : 1;
    const std::uint32_t bytesPerSample = kExpectedBitsPerSample / 8;
    const std::uint64_t dataBytes64 = static_cast<std::uint64_t>(monoSamples.size())
                                    * channels * bytesPerSample;
    if (dataBytes64 > std::numeric_limits<std::uint32_t>::max()) {
        error = "Generated stimulus WAV is too large.";
        return false;
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(dataBytes64);
    const std::uint32_t riffSize = 36u + dataBytes;
    const std::uint32_t byteRate = kExpectedSampleRate * channels * bytesPerSample;
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * bytesPerSample);

    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "Cannot create stimulus directory: " + ec.message();
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Cannot create stimulus WAV: " + pathToUtf8(path);
        return false;
    }

    out.write("RIFF", 4);
    writeLe32(out, riffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe32(out, 16);
    writeLe16(out, 1); // PCM
    writeLe16(out, channels);
    writeLe32(out, kExpectedSampleRate);
    writeLe32(out, byteRate);
    writeLe16(out, blockAlign);
    writeLe16(out, kExpectedBitsPerSample);
    out.write("data", 4);
    writeLe32(out, dataBytes);

    for (const std::int16_t sample : monoSamples) {
        const std::uint16_t raw = static_cast<std::uint16_t>(sample);
        writeLe16(out, raw);
        if (dualMono) writeLe16(out, raw);
    }

    if (!out) {
        error = "Failed while writing stimulus WAV: " + pathToUtf8(path);
        return false;
    }
    return true;
}

bool existsFile(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p, ec) && !ec && fs::is_regular_file(p, ec) && !ec;
}

} // namespace

const wchar_t* stimulusModeDisplayName(const StimulusMode mode) {
    switch (mode) {
    case StimulusMode::Legacy: return L"Original / Legacy";
    case StimulusMode::Clean:  return L"Clean";
    case StimulusMode::Dist:   return L"Dist";
    }
    return L"Unknown";
}

bool buildStimulus(const RuntimePaths& runtime,
                   const StimulusMode mode,
                   const fs::path& destination,
                   std::string& error) {
    if (mode == StimulusMode::Legacy) {
        if (!existsFile(runtime.legacyStimulus)) {
            error = "Missing runtime\\ampero\\nam_input_wav.wav";
            return false;
        }
        return copyFileCreatingParents(runtime.legacyStimulus, destination, error);
    }

    const bool clean = mode == StimulusMode::Clean;
    const fs::path& basePath = clean ? runtime.cleanStimulus : runtime.distStimulus;

    if (!existsFile(basePath)) {
        error = clean
            ? "Missing runtime\\ampero\\inputSignalCleanSW.wav"
            : "Missing runtime\\ampero\\inputSignalDistSW.wav";
        return false;
    }
    if (!existsFile(runtime.presetAudio)) {
        error = "Missing runtime\\ampero\\PresetAudio.wav";
        return false;
    }

    Pcm16MonoWav base;
    Pcm16MonoWav tail;
    if (!readPcm16Mono44100(basePath, kBaseFrames, base, error)) return false;
    if (!readPcm16Mono44100(runtime.presetAudio, kTailFrames, tail, error)) return false;

    std::vector<std::int16_t> combined;
    combined.reserve(base.samples.size() + tail.samples.size() + kSoundClonePaddingFrames);
    combined.insert(combined.end(), base.samples.begin(), base.samples.end());
    combined.insert(combined.end(), tail.samples.begin(), tail.samples.end());
    combined.insert(combined.end(), kSoundClonePaddingFrames, static_cast<std::int16_t>(0));

    // The official Sound Clone inputSignal.wav is mono. Previous v1.2 tests
    // confirmed that mono and dual-mono stimuli produce identical CLO data,
    // so v1.3 keeps the native mono representation.
    return writePcm16Wav(destination, combined, false, error);
}

} // namespace ntc
